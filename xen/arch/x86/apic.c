/*
 *      based on linux-2.6.17.13/arch/i386/kernel/apic.c
 *
 *  Local APIC handling, local APIC timers
 *
 *  (c) 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *  Fixes
 *  Maciej W. Rozycki   :   Bits for genuine 82489DX APICs;
 *                  thanks to Eric Gilmore
 *                  and Rolf G. Tews
 *                  for testing these extensively.
 *    Maciej W. Rozycki :   Various updates and fixes.
 *    Mikael Pettersson :   Power Management for UP-APIC.
 *    Pavel Machek and
 *    Mikael Pettersson    :    PM converted to driver model.
 */

#include <xen/config.h>
#include <xen/perfc.h>
#include <xen/errno.h>
#include <xen/init.h>
#include <xen/mm.h>
#include <xen/sched.h>
#include <xen/irq.h>
#include <xen/delay.h>
#include <xen/smp.h>
#include <xen/softirq.h>
#include <asm/mc146818rtc.h>
#include <asm/msr.h>
#include <asm/atomic.h>
#include <asm/mpspec.h>
#include <asm/flushtlb.h>
#include <asm/hardirq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/asm_defns.h> /* for BUILD_SMP_INTERRUPT */
#include <mach_apic.h>
#include <io_ports.h>

static struct {
    int active;
    /* r/w apic fields */
    unsigned int apic_id;
    unsigned int apic_taskpri;
    unsigned int apic_ldr;
    unsigned int apic_dfr;
    unsigned int apic_spiv;
    unsigned int apic_lvtt;
    unsigned int apic_lvtpc;
    unsigned int apic_lvtcmci;
    unsigned int apic_lvt0;
    unsigned int apic_lvt1;
    unsigned int apic_lvterr;
    unsigned int apic_tmict;
    unsigned int apic_tdcr;
    unsigned int apic_thmr;
} apic_pm_state;

/*
 * Knob to control our willingness to enable the local APIC.
 */
static int enable_local_apic __initdata = 0; /* -1=force-disable, +1=force-enable */

/*
 * Debug level
 */
int apic_verbosity;

int x2apic_enabled __read_mostly = 0;
int directed_eoi_enabled __read_mostly = 0;

/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs)
 */
BUILD_SMP_INTERRUPT(irq_move_cleanup_interrupt,IRQ_MOVE_CLEANUP_VECTOR)
BUILD_SMP_INTERRUPT(event_check_interrupt,EVENT_CHECK_VECTOR)
BUILD_SMP_INTERRUPT(invalidate_interrupt,INVALIDATE_TLB_VECTOR)
BUILD_SMP_INTERRUPT(call_function_interrupt,CALL_FUNCTION_VECTOR)

/*
 * Every pentium local APIC has two 'local interrupts', with a
 * soft-definable vector attached to both interrupts, one of
 * which is a timer interrupt, the other one is error counter
 * overflow. Linux uses the local APIC timer interrupt to get
 * a much simpler SMP time architecture:
 */
BUILD_SMP_INTERRUPT(apic_timer_interrupt,LOCAL_TIMER_VECTOR)
BUILD_SMP_INTERRUPT(error_interrupt,ERROR_APIC_VECTOR)
BUILD_SMP_INTERRUPT(spurious_interrupt,SPURIOUS_APIC_VECTOR)
BUILD_SMP_INTERRUPT(pmu_apic_interrupt,PMU_APIC_VECTOR)
BUILD_SMP_INTERRUPT(cmci_interrupt, CMCI_APIC_VECTOR)
#ifdef CONFIG_X86_MCE_THERMAL
BUILD_SMP_INTERRUPT(thermal_interrupt,THERMAL_APIC_VECTOR)
#endif

static int modern_apic(void)
{
    unsigned int lvr, version;
    /* AMD systems use old APIC versions, so check the CPU */
    if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
        boot_cpu_data.x86 >= 0xf)
        return 1;
    lvr = apic_read(APIC_LVR);
    version = GET_APIC_VERSION(lvr);
    return version >= 0x14;
}

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
    printk("unexpected IRQ trap at irq %02x\n", irq);
    /*
     * Currently unexpected vectors happen only on SMP and APIC.
     * We _must_ ack these because every local APIC has only N
     * irq slots per priority level, and a 'hanging, unacked' IRQ
     * holds up an irq slot - in excessive cases (when multiple
     * unexpected vectors occur) that might lock up the APIC
     * completely.
     * But only ack when the APIC is enabled -AK
     */
    if (cpu_has_apic)
        ack_APIC_irq();
}

void __init apic_intr_init(void)
{
#ifdef CONFIG_SMP
    smp_intr_init();
#endif
    /* self generated IPI for local APIC timer */
    set_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

    /* IPI vectors for APIC spurious and error interrupts */
    set_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
    set_intr_gate(ERROR_APIC_VECTOR, error_interrupt);

    /* Performance Counters Interrupt */
    set_intr_gate(PMU_APIC_VECTOR, pmu_apic_interrupt);

    /* CMCI Correctable Machine Check Interrupt */
    set_intr_gate(CMCI_APIC_VECTOR, cmci_interrupt);

    /* thermal monitor LVT interrupt, for P4 and latest Intel CPU*/
#ifdef CONFIG_X86_MCE_THERMAL
    set_intr_gate(THERMAL_APIC_VECTOR, thermal_interrupt);
#endif
}

/* Using APIC to generate smp_local_timer_interrupt? */
int using_apic_timer = 0;

static int enabled_via_apicbase;

void enable_NMI_through_LVT0 (void * dummy)
{
    unsigned int v, ver;

    ver = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(ver);
    v = APIC_DM_NMI;			/* unmask and set to NMI */
    if (!APIC_INTEGRATED(ver))		/* 82489DX */
        v |= APIC_LVT_LEVEL_TRIGGER;
    apic_write_around(APIC_LVT0, v);
}

int get_physical_broadcast(void)
{
    if (modern_apic())
        return 0xff;
    else
        return 0xf;
}

int get_maxlvt(void)
{
    unsigned int v, ver, maxlvt;

    v = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(v);
    /* 82489DXs do not report # of LVT entries. */
    maxlvt = APIC_INTEGRATED(ver) ? GET_APIC_MAXLVT(v) : 2;
    return maxlvt;
}

void clear_local_APIC(void)
{
    int maxlvt;
    unsigned long v;

    maxlvt = get_maxlvt();

    /*
     * Masking an LVT entry on a P6 can trigger a local APIC error
     * if the vector is zero. Mask LVTERR first to prevent this.
     */
    if (maxlvt >= 3) {
        v = ERROR_APIC_VECTOR; /* any non-zero vector will do */
        apic_write_around(APIC_LVTERR, v | APIC_LVT_MASKED);
    }
    /*
     * Careful: we have to set masks only first to deassert
     * any level-triggered sources.
     */
    v = apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT, v | APIC_LVT_MASKED);
    v = apic_read(APIC_LVT0);
    apic_write_around(APIC_LVT0, v | APIC_LVT_MASKED);
    v = apic_read(APIC_LVT1);
    apic_write_around(APIC_LVT1, v | APIC_LVT_MASKED);
    if (maxlvt >= 4) {
        v = apic_read(APIC_LVTPC);
        apic_write_around(APIC_LVTPC, v | APIC_LVT_MASKED);
    }

/* lets not touch this if we didn't frob it */
#ifdef CONFIG_X86_MCE_THERMAL
    if (maxlvt >= 5) {
        v = apic_read(APIC_LVTTHMR);
        apic_write_around(APIC_LVTTHMR, v | APIC_LVT_MASKED);
    }
#endif

    if (maxlvt >= 6) {
        v = apic_read(APIC_CMCI);
        apic_write_around(APIC_CMCI, v | APIC_LVT_MASKED);
    }
    /*
     * Clean APIC state for other OSs:
     */
    apic_write_around(APIC_LVTT, APIC_LVT_MASKED);
    apic_write_around(APIC_LVT0, APIC_LVT_MASKED);
    apic_write_around(APIC_LVT1, APIC_LVT_MASKED);
    if (maxlvt >= 3)
        apic_write_around(APIC_LVTERR, APIC_LVT_MASKED);
    if (maxlvt >= 4)
        apic_write_around(APIC_LVTPC, APIC_LVT_MASKED);

#ifdef CONFIG_X86_MCE_THERMAL
    if (maxlvt >= 5)
        apic_write_around(APIC_LVTTHMR, APIC_LVT_MASKED);
#endif
    if (maxlvt >= 6)
        apic_write_around(APIC_CMCI, APIC_LVT_MASKED);

    v = GET_APIC_VERSION(apic_read(APIC_LVR));
    if (APIC_INTEGRATED(v)) {  /* !82489DX */
        if (maxlvt > 3)        /* Due to Pentium errata 3AP and 11AP. */
            apic_write(APIC_ESR, 0);
        apic_read(APIC_ESR);
    }
}

void __init connect_bsp_APIC(void)
{
    if (pic_mode) {
        /*
         * Do not trust the local APIC being empty at bootup.
         */
        clear_local_APIC();
        /*
         * PIC mode, enable APIC mode in the IMCR, i.e.
         * connect BSP's local APIC to INT and NMI lines.
         */
        apic_printk(APIC_VERBOSE, "leaving PIC mode, "
                    "enabling APIC mode.\n");
        outb(0x70, 0x22);
        outb(0x01, 0x23);
    }
    enable_apic_mode();
}

void disconnect_bsp_APIC(int virt_wire_setup)
{
    if (pic_mode) {
        /*
         * Put the board back into PIC mode (has an effect
         * only on certain older boards).  Note that APIC
         * interrupts, including IPIs, won't work beyond
         * this point!  The only exception are INIT IPIs.
         */
        apic_printk(APIC_VERBOSE, "disabling APIC mode, "
                    "entering PIC mode.\n");
        outb(0x70, 0x22);
        outb(0x00, 0x23);
    }
    else {
        /* Go back to Virtual Wire compatibility mode */
        unsigned long value;

        /* For the spurious interrupt use vector F, and enable it */
        value = apic_read(APIC_SPIV);
        value &= ~APIC_VECTOR_MASK;
        value |= APIC_SPIV_APIC_ENABLED;
        value |= 0xf;
        apic_write_around(APIC_SPIV, value);

        if (!virt_wire_setup) {
            /* For LVT0 make it edge triggered, active high, external and enabled */
            value = apic_read(APIC_LVT0);
            value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
                       APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
                       APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED );
            value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
            value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_EXTINT);
            apic_write_around(APIC_LVT0, value);
        }
        else {
            /* Disable LVT0 */
            apic_write_around(APIC_LVT0, APIC_LVT_MASKED);
        }

        /* For LVT1 make it edge triggered, active high, nmi and enabled */
        value = apic_read(APIC_LVT1);
        value &= ~(
            APIC_MODE_MASK | APIC_SEND_PENDING |
            APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
            APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
        value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
        value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_NMI);
        apic_write_around(APIC_LVT1, value);
    }
}

void disable_local_APIC(void)
{
    unsigned long value;

    clear_local_APIC();

    /*
     * Disable APIC (implies clearing of registers
     * for 82489DX!).
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_SPIV_APIC_ENABLED;
    apic_write_around(APIC_SPIV, value);

    if (enabled_via_apicbase) {
        unsigned int l, h;
        rdmsr(MSR_IA32_APICBASE, l, h);
        l &= ~MSR_IA32_APICBASE_ENABLE;
        wrmsr(MSR_IA32_APICBASE, l, h);
    }
}

extern int ioapic_ack_new;
/*
 * This is to verify that we're looking at a real local APIC.
 * Check these against your board if the CPUs aren't getting
 * started for no apparent reason.
 */
int __init verify_local_APIC(void)
{
    unsigned int reg0, reg1;

    /*
     * The version register is read-only in a real APIC.
     */
    reg0 = apic_read(APIC_LVR);
    apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg0);

    /* We don't try writing LVR in x2APIC mode since that incurs #GP. */
    if ( !x2apic_enabled )
        apic_write(APIC_LVR, reg0 ^ APIC_LVR_MASK);
    reg1 = apic_read(APIC_LVR);
    apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg1);

    /*
     * The two version reads above should print the same
     * numbers.  If the second one is different, then we
     * poke at a non-APIC.
     */
    if (reg1 != reg0)
        return 0;

    /*
     * Check if the version looks reasonably.
     */
    reg1 = GET_APIC_VERSION(reg0);
    if (reg1 == 0x00 || reg1 == 0xff)
        return 0;
    reg1 = get_maxlvt();
    if (reg1 < 0x02 || reg1 == 0xff)
        return 0;

    /*
     * Detecting directed EOI on BSP:
     * If having directed EOI support in lapic, force to use ioapic_ack_old,
     * and enable the directed EOI for intr handling.
     */
    if ( reg0 & APIC_LVR_DIRECTED_EOI )
    {
        ioapic_ack_new = 0;
        directed_eoi_enabled = 1;
        printk("Enabled directed EOI with ioapic_ack_old on!\n");
    }

    /*
     * The ID register is read/write in a real APIC.
     */
    reg0 = apic_read(APIC_ID);
    apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg0);

    /*
     * The next two are just to see if we have sane values.
     * They're only really relevant if we're in Virtual Wire
     * compatibility mode, but most boxes are anymore.
     */
    reg0 = apic_read(APIC_LVT0);
    apic_printk(APIC_DEBUG, "Getting LVT0: %x\n", reg0);
    reg1 = apic_read(APIC_LVT1);
    apic_printk(APIC_DEBUG, "Getting LVT1: %x\n", reg1);

    return 1;
}

void __init sync_Arb_IDs(void)
{
    /* Unsupported on P4 - see Intel Dev. Manual Vol. 3, Ch. 8.6.1
       And not needed on AMD */
    if (modern_apic())
        return;
    /*
     * Wait for idle.
     */
    apic_wait_icr_idle();

    apic_printk(APIC_DEBUG, "Synchronizing Arb IDs.\n");
    apic_write_around(APIC_ICR, APIC_DEST_ALLINC | APIC_INT_LEVELTRIG
                      | APIC_DM_INIT);
}

extern void __error_in_apic_c (void);

/*
 * An initial setup of the virtual wire mode.
 */
void __init init_bsp_APIC(void)
{
    unsigned long value, ver;

    /*
     * Don't do the setup now if we have a SMP BIOS as the
     * through-I/O-APIC virtual wire mode might be active.
     */
    if (smp_found_config || !cpu_has_apic)
        return;

    value = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(value);
    
    /*
     * Do not trust the local APIC being empty at bootup.
     */
    clear_local_APIC();
    
    /*
     * Enable APIC.
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_VECTOR_MASK;
    value |= APIC_SPIV_APIC_ENABLED;
    
    /* This bit is reserved on P4/Xeon and should be cleared */
    if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) && (boot_cpu_data.x86 == 15))
        value &= ~APIC_SPIV_FOCUS_DISABLED;
    else
        value |= APIC_SPIV_FOCUS_DISABLED;
    value |= SPURIOUS_APIC_VECTOR;
    apic_write_around(APIC_SPIV, value);

    /*
     * Set up the virtual wire mode.
     */
    apic_write_around(APIC_LVT0, APIC_DM_EXTINT);
    value = APIC_DM_NMI;
    if (!APIC_INTEGRATED(ver))              /* 82489DX */
        value |= APIC_LVT_LEVEL_TRIGGER;
    apic_write_around(APIC_LVT1, value);
}

static void apic_pm_activate(void)
{
    apic_pm_state.active = 1;
}

void __devinit setup_local_APIC(void)
{
    unsigned long oldvalue, value, ver, maxlvt;
    int i, j;

    /* Pound the ESR really hard over the head with a big hammer - mbligh */
    if (esr_disable) {
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
        apic_write(APIC_ESR, 0);
    }

    value = apic_read(APIC_LVR);
    ver = GET_APIC_VERSION(value);

    if ((SPURIOUS_APIC_VECTOR & 0x0f) != 0x0f)
        __error_in_apic_c();

    /*
     * Double-check whether this APIC is really registered.
     */
    if (!apic_id_registered())
        BUG();

    /*
     * Intel recommends to set DFR, LDR and TPR before enabling
     * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
     * document number 292116).  So here it goes...
     */
    init_apic_ldr();

    /*
     * Set Task Priority to 'accept all'. We never change this
     * later on.
     */
    value = apic_read(APIC_TASKPRI);
    value &= ~APIC_TPRI_MASK;
    apic_write_around(APIC_TASKPRI, value);

    /*
     * After a crash, we no longer service the interrupts and a pending
     * interrupt from previous kernel might still have ISR bit set.
     *
     * Most probably by now CPU has serviced that pending interrupt and
     * it might not have done the ack_APIC_irq() because it thought,
     * interrupt came from i8259 as ExtInt. LAPIC did not get EOI so it
     * does not clear the ISR bit and cpu thinks it has already serivced
     * the interrupt. Hence a vector might get locked. It was noticed
     * for timer irq (vector 0x31). Issue an extra EOI to clear ISR.
     */
    for (i = APIC_ISR_NR - 1; i >= 0; i--) {
        value = apic_read(APIC_ISR + i*0x10);
        for (j = 31; j >= 0; j--) {
            if (value & (1<<j))
                ack_APIC_irq();
        }
    }

    /*
     * Now that we are all set up, enable the APIC
     */
    value = apic_read(APIC_SPIV);
    value &= ~APIC_VECTOR_MASK;
    /*
     * Enable APIC
     */
    value |= APIC_SPIV_APIC_ENABLED;

    /*
     * Some unknown Intel IO/APIC (or APIC) errata is biting us with
     * certain networking cards. If high frequency interrupts are
     * happening on a particular IOAPIC pin, plus the IOAPIC routing
     * entry is masked/unmasked at a high rate as well then sooner or
     * later IOAPIC line gets 'stuck', no more interrupts are received
     * from the device. If focus CPU is disabled then the hang goes
     * away, oh well :-(
     *
     * [ This bug can be reproduced easily with a level-triggered
     *   PCI Ne2000 networking cards and PII/PIII processors, dual
     *   BX chipset. ]
     */
    /*
     * Actually disabling the focus CPU check just makes the hang less
     * frequent as it makes the interrupt distributon model be more
     * like LRU than MRU (the short-term load is more even across CPUs).
     * See also the comment in end_level_ioapic_irq().  --macro
     */
#if 1
    /* Enable focus processor (bit==0) */
    value &= ~APIC_SPIV_FOCUS_DISABLED;
#else
    /* Disable focus processor (bit==1) */
    value |= APIC_SPIV_FOCUS_DISABLED;
#endif
    /*
     * Set spurious IRQ vector
     */
    value |= SPURIOUS_APIC_VECTOR;

    /*
     * Enable directed EOI
     */
    if ( directed_eoi_enabled )
    {
        value |= APIC_SPIV_DIRECTED_EOI;
        apic_printk(APIC_VERBOSE, "Suppress EOI broadcast on CPU#%d\n",
                    smp_processor_id());
    }

    apic_write_around(APIC_SPIV, value);

    /*
     * Set up LVT0, LVT1:
     *
     * set up through-local-APIC on the BP's LINT0. This is not
     * strictly necessery in pure symmetric-IO mode, but sometimes
     * we delegate interrupts to the 8259A.
     */
    /*
     * TODO: set up through-local-APIC from through-I/O-APIC? --macro
     */
    value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
    if (!smp_processor_id() && (pic_mode || !value)) {
        value = APIC_DM_EXTINT;
        apic_printk(APIC_VERBOSE, "enabled ExtINT on CPU#%d\n",
                    smp_processor_id());
    } else {
        value = APIC_DM_EXTINT | APIC_LVT_MASKED;
        apic_printk(APIC_VERBOSE, "masked ExtINT on CPU#%d\n",
                    smp_processor_id());
    }
    apic_write_around(APIC_LVT0, value);

    /*
     * only the BP should see the LINT1 NMI signal, obviously.
     */
    if (!smp_processor_id())
        value = APIC_DM_NMI;
    else
        value = APIC_DM_NMI | APIC_LVT_MASKED;
    if (!APIC_INTEGRATED(ver))      /* 82489DX */
        value |= APIC_LVT_LEVEL_TRIGGER;
    apic_write_around(APIC_LVT1, value);

    if (APIC_INTEGRATED(ver) && !esr_disable) {        /* !82489DX */
        maxlvt = get_maxlvt();
        if (maxlvt > 3)     /* Due to the Pentium erratum 3AP. */
            apic_write(APIC_ESR, 0);
        oldvalue = apic_read(APIC_ESR);

        value = ERROR_APIC_VECTOR;      // enables sending errors
        apic_write_around(APIC_LVTERR, value);
        /*
         * spec says clear errors after enabling vector.
         */
        if (maxlvt > 3)
            apic_write(APIC_ESR, 0);
        value = apic_read(APIC_ESR);
        if (value != oldvalue)
            apic_printk(APIC_VERBOSE, "ESR value before enabling "
                        "vector: 0x%08lx  after: 0x%08lx\n",
                        oldvalue, value);
    } else {
        if (esr_disable)    
            /* 
             * Something untraceble is creating bad interrupts on 
             * secondary quads ... for the moment, just leave the
             * ESR disabled - we can't do anything useful with the
             * errors anyway - mbligh
             */
            printk("Leaving ESR disabled.\n");
        else
            printk("No ESR for 82489DX.\n");
    }

    if (nmi_watchdog == NMI_LOCAL_APIC)
        setup_apic_nmi_watchdog();
    apic_pm_activate();
}

int lapic_suspend(void)
{
    unsigned long flags;
    int maxlvt = get_maxlvt();
    if (!apic_pm_state.active)
        return 0;

    apic_pm_state.apic_id = apic_read(APIC_ID);
    apic_pm_state.apic_taskpri = apic_read(APIC_TASKPRI);
    apic_pm_state.apic_ldr = apic_read(APIC_LDR);
    apic_pm_state.apic_dfr = apic_read(APIC_DFR);
    apic_pm_state.apic_spiv = apic_read(APIC_SPIV);
    apic_pm_state.apic_lvtt = apic_read(APIC_LVTT);
    apic_pm_state.apic_lvtpc = apic_read(APIC_LVTPC);

    if (maxlvt >= 6) {
        apic_pm_state.apic_lvtcmci = apic_read(APIC_CMCI);
    }

    apic_pm_state.apic_lvt0 = apic_read(APIC_LVT0);
    apic_pm_state.apic_lvt1 = apic_read(APIC_LVT1);
    apic_pm_state.apic_lvterr = apic_read(APIC_LVTERR);
    apic_pm_state.apic_tmict = apic_read(APIC_TMICT);
    apic_pm_state.apic_tdcr = apic_read(APIC_TDCR);
    apic_pm_state.apic_thmr = apic_read(APIC_LVTTHMR);
    
    local_irq_save(flags);
    disable_local_APIC();
    local_irq_restore(flags);
    return 0;
}

int lapic_resume(void)
{
    unsigned int l, h;
    unsigned long flags;
    int maxlvt;

    if (!apic_pm_state.active)
        return 0;

    local_irq_save(flags);

    /*
     * Make sure the APICBASE points to the right address
     *
     * FIXME! This will be wrong if we ever support suspend on
     * SMP! We'll need to do this as part of the CPU restore!
     */
    if ( !x2apic_enabled )
    {
        rdmsr(MSR_IA32_APICBASE, l, h);
        l &= ~MSR_IA32_APICBASE_BASE;
        l |= MSR_IA32_APICBASE_ENABLE | mp_lapic_addr;
        wrmsr(MSR_IA32_APICBASE, l, h);
    }
    else
        enable_x2apic();

    apic_write(APIC_LVTERR, ERROR_APIC_VECTOR | APIC_LVT_MASKED);
    apic_write(APIC_ID, apic_pm_state.apic_id);
    apic_write(APIC_DFR, apic_pm_state.apic_dfr);
    apic_write(APIC_LDR, apic_pm_state.apic_ldr);
    apic_write(APIC_TASKPRI, apic_pm_state.apic_taskpri);
    apic_write(APIC_SPIV, apic_pm_state.apic_spiv);
    apic_write(APIC_LVT0, apic_pm_state.apic_lvt0);
    apic_write(APIC_LVT1, apic_pm_state.apic_lvt1);
    apic_write(APIC_LVTTHMR, apic_pm_state.apic_thmr);

    maxlvt = get_maxlvt();
    if (maxlvt >= 6) {
        apic_write(APIC_CMCI, apic_pm_state.apic_lvtcmci);
    }

    apic_write(APIC_LVTPC, apic_pm_state.apic_lvtpc);
    apic_write(APIC_LVTT, apic_pm_state.apic_lvtt);
    apic_write(APIC_TDCR, apic_pm_state.apic_tdcr);
    apic_write(APIC_TMICT, apic_pm_state.apic_tmict);
    apic_write(APIC_ESR, 0);
    apic_read(APIC_ESR);
    apic_write(APIC_LVTERR, apic_pm_state.apic_lvterr);
    apic_write(APIC_ESR, 0);
    apic_read(APIC_ESR);
    local_irq_restore(flags);
    return 0;
}


/*
 * If Linux enabled the LAPIC against the BIOS default
 * disable it down before re-entering the BIOS on shutdown.
 * Otherwise the BIOS may get confused and not power-off.
 * Additionally clear all LVT entries before disable_local_APIC
 * for the case where Linux didn't enable the LAPIC.
 */
void lapic_shutdown(void)
{
    unsigned long flags;

    if (!cpu_has_apic)
        return;

    local_irq_save(flags);
    clear_local_APIC();

    if (enabled_via_apicbase)
        disable_local_APIC();

    local_irq_restore(flags);
}

/*
 * Detect and enable local APICs on non-SMP boards.
 * Original code written by Keir Fraser.
 */

static void __init lapic_disable(char *str)
{
    enable_local_apic = -1;
    setup_clear_cpu_cap(X86_FEATURE_APIC);
}
custom_param("nolapic", lapic_disable);

static void __init lapic_enable(char *str)
{
    enable_local_apic = 1;
}
custom_param("lapic", lapic_enable);

static void __init apic_set_verbosity(char *str)
{
    if (strcmp("debug", str) == 0)
        apic_verbosity = APIC_DEBUG;
    else if (strcmp("verbose", str) == 0)
        apic_verbosity = APIC_VERBOSE;
    else
        printk(KERN_WARNING "APIC Verbosity level %s not recognised"
               " use apic_verbosity=verbose or apic_verbosity=debug", str);
}
custom_param("apic_verbosity", apic_set_verbosity);

static int __init detect_init_APIC (void)
{
    u32 h, l, features;

    /* Disabled by kernel option? */
    if (enable_local_apic < 0)
        return -1;

    switch (boot_cpu_data.x86_vendor) {
    case X86_VENDOR_AMD:
        if ((boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model > 1) ||
            (boot_cpu_data.x86 >= 0xf && boot_cpu_data.x86 <= 0x17))
            break;
        goto no_apic;
    case X86_VENDOR_INTEL:
        if (boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15 ||
            (boot_cpu_data.x86 == 5 && cpu_has_apic))
            break;
        goto no_apic;
    default:
        goto no_apic;
    }

    if (!cpu_has_apic) {
        /*
         * Over-ride BIOS and try to enable the local
         * APIC only if "lapic" specified.
         */
        if (enable_local_apic <= 0) {
            printk("Local APIC disabled by BIOS -- "
                   "you can enable it with \"lapic\"\n");
            return -1;
        }
        /*
         * Some BIOSes disable the local APIC in the
         * APIC_BASE MSR. This can only be done in
         * software for Intel P6 or later and AMD K7
         * (Model > 1) or later.
         */
        rdmsr(MSR_IA32_APICBASE, l, h);
        if (!(l & MSR_IA32_APICBASE_ENABLE)) {
            printk("Local APIC disabled by BIOS -- reenabling.\n");
            l &= ~MSR_IA32_APICBASE_BASE;
            l |= MSR_IA32_APICBASE_ENABLE | APIC_DEFAULT_PHYS_BASE;
            wrmsr(MSR_IA32_APICBASE, l, h);
            enabled_via_apicbase = 1;
        }
    }
    /*
     * The APIC feature bit should now be enabled
     * in `cpuid'
     */
    features = cpuid_edx(1);
    if (!(features & (1 << X86_FEATURE_APIC))) {
        printk("Could not enable APIC!\n");
        return -1;
    }

    set_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
    mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

    /* The BIOS may have set up the APIC at some other address */
    rdmsr(MSR_IA32_APICBASE, l, h);
    if (l & MSR_IA32_APICBASE_ENABLE)
        mp_lapic_addr = l & MSR_IA32_APICBASE_BASE;

    if (nmi_watchdog != NMI_NONE)
        nmi_watchdog = NMI_LOCAL_APIC;

    printk("Found and enabled local APIC!\n");

    apic_pm_activate();

    return 0;

no_apic:
    printk("No local APIC present or hardware disabled\n");
    return -1;
}

void enable_x2apic(void)
{
    u32 lo, hi;

    if ( smp_processor_id() == 0 )
    {
        if ( !iommu_supports_eim() )
        {
            printk("x2APIC would not be enabled without EIM.\n");
            return;
        }

        if ( apic_x2apic_phys.probe() )
            genapic = &apic_x2apic_phys;
        else if ( apic_x2apic_cluster.probe() )
            genapic = &apic_x2apic_cluster;
        else
        {
            printk("x2APIC would not be enabled due to x2apic=off.\n");
            return;
        }

        x2apic_enabled = 1;
        printk("Switched to APIC driver %s.\n", genapic->name);
    }
    else
    {
        BUG_ON(!x2apic_enabled); /* APs only enable x2apic when BSP did so. */
    }

    rdmsr(MSR_IA32_APICBASE, lo, hi);
    if ( !(lo & MSR_IA32_APICBASE_EXTD) )
    {
        lo |= MSR_IA32_APICBASE_ENABLE | MSR_IA32_APICBASE_EXTD;
        wrmsr(MSR_IA32_APICBASE, lo, 0);
        printk("x2APIC mode enabled.\n");
    }
    else
        printk("x2APIC mode enabled by BIOS.\n");
}

void __init init_apic_mappings(void)
{
    unsigned long apic_phys;

    if ( x2apic_enabled )
        goto __next;
    /*
     * If no local APIC can be found then set up a fake all
     * zeroes page to simulate the local APIC and another
     * one for the IO-APIC.
     */
    if (!smp_found_config && detect_init_APIC()) {
        apic_phys = __pa(alloc_xenheap_page());
        clear_page(__va(apic_phys));
    } else
        apic_phys = mp_lapic_addr;

    set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
    apic_printk(APIC_VERBOSE, "mapped APIC to %08lx (%08lx)\n", APIC_BASE,
                apic_phys);

__next:
    /*
     * Fetch the APIC ID of the BSP in case we have a
     * default configuration (or the MP table is broken).
     */
    if (boot_cpu_physical_apicid == -1U)
        boot_cpu_physical_apicid = get_apic_id();
    x86_cpu_to_apicid[0] = get_apic_id();
    cpu_2_logical_apicid[0] = get_logical_apic_id();

    init_ioapic_mappings();
}

/*****************************************************************************
 * APIC calibration
 * 
 * The APIC is programmed in bus cycles.
 * Timeout values should specified in real time units.
 * The "cheapest" time source is the cyclecounter.
 * 
 * Thus, we need a mappings from: bus cycles <- cycle counter <- system time
 * 
 * The calibration is currently a bit shoddy since it requires the external
 * timer chip to generate periodic timer interupts. 
 *****************************************************************************/

/* used for system time scaling */
static unsigned long bus_freq;    /* KAF: pointer-size avoids compile warns. */
static u32           bus_cycle;   /* length of one bus cycle in pico-seconds */
static u32           bus_scale;   /* scaling factor convert ns to bus cycles */

/*
 * The timer chip is already set up at HZ interrupts per second here,
 * but we do not accept timer interrupts yet. We only allow the BP
 * to calibrate.
 */
static unsigned int __init get_8254_timer_count(void)
{
    /*extern spinlock_t i8253_lock;*/
    /*unsigned long flags;*/

    unsigned int count;

    /*spin_lock_irqsave(&i8253_lock, flags);*/

    outb_p(0x00, PIT_MODE);
    count = inb_p(PIT_CH0);
    count |= inb_p(PIT_CH0) << 8;

    /*spin_unlock_irqrestore(&i8253_lock, flags);*/

    return count;
}

/* next tick in 8254 can be caught by catching timer wraparound */
static void __init wait_8254_wraparound(void)
{
    unsigned int curr_count, prev_count;
    
    curr_count = get_8254_timer_count();
    do {
        prev_count = curr_count;
        curr_count = get_8254_timer_count();

        /* workaround for broken Mercury/Neptune */
        if (prev_count >= curr_count + 0x100)
            curr_count = get_8254_timer_count();
        
    } while (prev_count >= curr_count);
}

/*
 * Default initialization for 8254 timers. If we use other timers like HPET,
 * we override this later
 */
void (*wait_timer_tick)(void) __initdata = wait_8254_wraparound;

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice on the boot CPU, once with a bogus timeout
 * value, second time for real. The other (noncalibrating) CPUs
 * call this function only once, with the real, calibrated value.
 *
 * We do reads before writes even if unnecessary, to get around the
 * P5 APIC double write bug.
 */

#define APIC_DIVISOR 1

static void __setup_APIC_LVTT(unsigned int clocks)
{
    unsigned int lvtt_value, tmp_value, ver;

    ver = GET_APIC_VERSION(apic_read(APIC_LVR));
    /* NB. Xen uses local APIC timer in one-shot mode. */
    lvtt_value = /*APIC_LVT_TIMER_PERIODIC |*/ LOCAL_TIMER_VECTOR;
    if (!APIC_INTEGRATED(ver))
        lvtt_value |= SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV);
    apic_write_around(APIC_LVTT, lvtt_value);

    tmp_value = apic_read(APIC_TDCR);
    apic_write_around(APIC_TDCR, (tmp_value | APIC_TDR_DIV_1));

    apic_write_around(APIC_TMICT, clocks/APIC_DIVISOR);
}

static void __devinit setup_APIC_timer(unsigned int clocks)
{
    unsigned long flags;
    local_irq_save(flags);
    __setup_APIC_LVTT(clocks);
    local_irq_restore(flags);
}

/*
 * In this function we calibrate APIC bus clocks to the external
 * timer. Unfortunately we cannot use jiffies and the timer irq
 * to calibrate, since some later bootup code depends on getting
 * the first irq? Ugh.
 *
 * We want to do the calibration only once since we
 * want to have local timer irqs syncron. CPUs connected
 * by the same APIC bus have the very same bus frequency.
 * And we want to have irqs off anyways, no accidental
 * APIC irq that way.
 */

static int __init calibrate_APIC_clock(void)
{
    unsigned long long t1 = 0, t2 = 0;
    long tt1, tt2;
    long result;
    int i;
    const int LOOPS = HZ/10;

    apic_printk(APIC_VERBOSE, "calibrating APIC timer ...\n");

    /*
     * Put whatever arbitrary (but long enough) timeout
     * value into the APIC clock, we just want to get the
     * counter running for calibration.
     */
    __setup_APIC_LVTT(1000000000);

    /*
     * The timer chip counts down to zero. Let's wait
     * for a wraparound to start exact measurement:
     * (the current tick might have been already half done)
     */
    wait_timer_tick();

    /*
     * We wrapped around just now. Let's start:
     */
    if (cpu_has_tsc)
        rdtscll(t1);
    tt1 = apic_read(APIC_TMCCT);

    /*
     * Let's wait LOOPS wraprounds:
     */
    for (i = 0; i < LOOPS; i++)
        wait_timer_tick();

    tt2 = apic_read(APIC_TMCCT);
    if (cpu_has_tsc)
        rdtscll(t2);

    /*
     * The APIC bus clock counter is 32 bits only, it
     * might have overflown, but note that we use signed
     * longs, thus no extra care needed.
     *
     * underflown to be exact, as the timer counts down ;)
     */

    result = (tt1-tt2)*APIC_DIVISOR/LOOPS;

    if (cpu_has_tsc)
        apic_printk(APIC_VERBOSE, "..... CPU clock speed is "
                    "%ld.%04ld MHz.\n",
                    ((long)(t2-t1)/LOOPS)/(1000000/HZ),
                    ((long)(t2-t1)/LOOPS)%(1000000/HZ));

    apic_printk(APIC_VERBOSE, "..... host bus clock speed is "
                "%ld.%04ld MHz.\n",
                result/(1000000/HZ),
                result%(1000000/HZ));

    /* set up multipliers for accurate timer code */
    bus_freq   = result*HZ;
    bus_cycle  = (u32) (1000000000000LL/bus_freq); /* in pico seconds */
    bus_scale  = (1000*262144)/bus_cycle;

    apic_printk(APIC_VERBOSE, "..... bus_scale = 0x%08X\n", bus_scale);
    /* reset APIC to zero timeout value */
    __setup_APIC_LVTT(0);

    return result;
}

static unsigned int calibration_result;

void __init setup_boot_APIC_clock(void)
{
    unsigned long flags;
    apic_printk(APIC_VERBOSE, "Using local APIC timer interrupts.\n");
    using_apic_timer = 1;

    local_irq_save(flags);

    calibration_result = calibrate_APIC_clock();
    /*
     * Now set up the timer for real.
     */
    setup_APIC_timer(calibration_result);
    
    local_irq_restore(flags);
}

void __devinit setup_secondary_APIC_clock(void)
{
    setup_APIC_timer(calibration_result);
}

void disable_APIC_timer(void)
{
    if (using_apic_timer) {
        unsigned long v;
        
        v = apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT, v | APIC_LVT_MASKED);
    }
}

void enable_APIC_timer(void)
{
    if (using_apic_timer) {
        unsigned long v;
        
        v = apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT, v & ~APIC_LVT_MASKED);
    }
}

#undef APIC_DIVISOR

/*
 * reprogram_timer: Reprogram the APIC timer.
 * Timeout is a Xen system time (nanoseconds since boot); 0 disables the timer.
 * Returns 1 on success; 0 if the timeout is too soon or is in the past.
 */
int reprogram_timer(s_time_t timeout)
{
    s_time_t expire;
    u32 apic_tmict = 0;

    /* No local APIC: timer list is polled via the PIT interrupt. */
    if ( !cpu_has_apic )
        return 1;

    if ( timeout && ((expire = timeout - NOW()) > 0) )
        apic_tmict = min_t(u64, (bus_scale * expire) >> 18, UINT_MAX);

    apic_write(APIC_TMICT, (unsigned long)apic_tmict);

    return apic_tmict || !timeout;
}

fastcall void smp_apic_timer_interrupt(struct cpu_user_regs * regs)
{
    struct cpu_user_regs *old_regs = set_irq_regs(regs);
    ack_APIC_irq();
    perfc_incr(apic_timer);
    raise_softirq(TIMER_SOFTIRQ);
    set_irq_regs(old_regs);
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
fastcall void smp_spurious_interrupt(struct cpu_user_regs *regs)
{
    unsigned long v;
    struct cpu_user_regs *old_regs = set_irq_regs(regs);

    irq_enter();
    /*
     * Check if this really is a spurious interrupt and ACK it
     * if it is a vectored one.  Just in case...
     * Spurious interrupts should not be ACKed.
     */
    v = apic_read(APIC_ISR + ((SPURIOUS_APIC_VECTOR & ~0x1f) >> 1));
    if (v & (1 << (SPURIOUS_APIC_VECTOR & 0x1f)))
        ack_APIC_irq();

    /* see sw-dev-man vol 3, chapter 7.4.13.5 */
    printk(KERN_INFO "spurious APIC interrupt on CPU#%d, should never happen.\n",
           smp_processor_id());
    irq_exit();
    set_irq_regs(old_regs);
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */

fastcall void smp_error_interrupt(struct cpu_user_regs *regs)
{
    unsigned long v, v1;
    struct cpu_user_regs *old_regs = set_irq_regs(regs);

    irq_enter();
    /* First tickle the hardware, only then report what went on. -- REW */
    v = apic_read(APIC_ESR);
    apic_write(APIC_ESR, 0);
    v1 = apic_read(APIC_ESR);
    ack_APIC_irq();
    atomic_inc(&irq_err_count);

    /* Here is what the APIC error bits mean:
       0: Send CS error
       1: Receive CS error
       2: Send accept error
       3: Receive accept error
       4: Reserved
       5: Send illegal vector
       6: Received illegal vector
       7: Illegal register address
    */
    printk (KERN_DEBUG "APIC error on CPU%d: %02lx(%02lx)\n",
            smp_processor_id(), v , v1);
    irq_exit();
    set_irq_regs(old_regs);
}

/*
 * This interrupt handles performance counters interrupt
 */

fastcall void smp_pmu_apic_interrupt(struct cpu_user_regs *regs)
{
    struct cpu_user_regs *old_regs = set_irq_regs(regs);
    ack_APIC_irq();
    hvm_do_pmu_interrupt(regs);
    set_irq_regs(old_regs);
}

/*
 * This initializes the IO-APIC and APIC hardware if this is
 * a UP kernel.
 */
int __init APIC_init_uniprocessor (void)
{
    if (enable_local_apic < 0)
        clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);

    if (!smp_found_config && !cpu_has_apic) {
        skip_ioapic_setup = 1;
        return -1;
    }

    /*
     * Complain if the BIOS pretends there is one.
     */
    if (!cpu_has_apic && APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
        printk(KERN_ERR "BIOS bug, local APIC #%d not detected!...\n",
               boot_cpu_physical_apicid);
        clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
        skip_ioapic_setup = 1;
        return -1;
    }

    verify_local_APIC();

    connect_bsp_APIC();

    /*
     * Hack: In case of kdump, after a crash, kernel might be booting
     * on a cpu with non-zero lapic id. But boot_cpu_physical_apicid
     * might be zero if read from MP tables. Get it from LAPIC.
     */
#ifdef CONFIG_CRASH_DUMP
    boot_cpu_physical_apicid = get_apic_id();
#endif
    phys_cpu_present_map = physid_mask_of_physid(boot_cpu_physical_apicid);

    setup_local_APIC();

    if (nmi_watchdog == NMI_LOCAL_APIC)
        check_nmi_watchdog();
#ifdef CONFIG_X86_IO_APIC
    if (smp_found_config)
        if (!skip_ioapic_setup && nr_ioapics)
            setup_IO_APIC();
#endif
    setup_boot_APIC_clock();

    return 0;
}
