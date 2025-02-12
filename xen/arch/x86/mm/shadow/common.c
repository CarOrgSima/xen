/******************************************************************************
 * arch/x86/mm/shadow/common.c
 *
 * Shadow code that does not need to be multiply compiled.
 * Parts of this code are Copyright (c) 2006 by XenSource Inc.
 * Parts of this code are Copyright (c) 2006 by Michael A Fetterman
 * Parts based on earlier work by Michael A Fetterman, Ian Pratt et al.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <xen/config.h>
#include <xen/types.h>
#include <xen/mm.h>
#include <xen/trace.h>
#include <xen/sched.h>
#include <xen/perfc.h>
#include <xen/irq.h>
#include <xen/domain_page.h>
#include <xen/guest_access.h>
#include <xen/keyhandler.h>
#include <asm/event.h>
#include <asm/page.h>
#include <asm/current.h>
#include <asm/flushtlb.h>
#include <asm/shadow.h>
#include <xen/numa.h>
#include "private.h"

DEFINE_PER_CPU(uint32_t,trace_shadow_path_flags);

/* Set up the shadow-specific parts of a domain struct at start of day.
 * Called for every domain from arch_domain_create() */
void shadow_domain_init(struct domain *d, unsigned int domcr_flags)
{
    int i;
    shadow_lock_init(d);
    for ( i = 0; i <= SHADOW_MAX_ORDER; i++ )
        INIT_PAGE_LIST_HEAD(&d->arch.paging.shadow.freelists[i]);
    INIT_PAGE_LIST_HEAD(&d->arch.paging.shadow.p2m_freelist);
    INIT_PAGE_LIST_HEAD(&d->arch.paging.shadow.pinned_shadows);

    /* Use shadow pagetables for log-dirty support */
    paging_log_dirty_init(d, shadow_enable_log_dirty, 
                          shadow_disable_log_dirty, shadow_clean_dirty_bitmap);

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
    d->arch.paging.shadow.oos_active = 0;
    d->arch.paging.shadow.oos_off = (domcr_flags & DOMCRF_oos_off) ?  1 : 0;
#endif
}

/* Setup the shadow-specfic parts of a vcpu struct. Note: The most important
 * job is to initialize the update_paging_modes() function pointer, which is
 * used to initialized the rest of resources. Therefore, it really does not
 * matter to have v->arch.paging.mode pointing to any mode, as long as it can
 * be compiled.
 */
void shadow_vcpu_init(struct vcpu *v)
{
#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
    int i, j;

    for ( i = 0; i < SHADOW_OOS_PAGES; i++ )
    {
        v->arch.paging.shadow.oos[i] = _mfn(INVALID_MFN);
        v->arch.paging.shadow.oos_snapshot[i] = _mfn(INVALID_MFN);
        for ( j = 0; j < SHADOW_OOS_FIXUPS; j++ )
            v->arch.paging.shadow.oos_fixup[i].smfn[j] = _mfn(INVALID_MFN);
    }
#endif

    v->arch.paging.mode = &SHADOW_INTERNAL_NAME(sh_paging_mode, 3);
}

#if SHADOW_AUDIT
int shadow_audit_enable = 0;

static void shadow_audit_key(unsigned char key)
{
    shadow_audit_enable = !shadow_audit_enable;
    printk("%s shadow_audit_enable=%d\n",
           __func__, shadow_audit_enable);
}

static struct keyhandler shadow_audit_keyhandler = {
    .u.fn = shadow_audit_key,
    .desc = "toggle shadow audits"
};

static int __init shadow_audit_key_init(void)
{
    register_keyhandler('O', &shadow_audit_keyhandler);
    return 0;
}
__initcall(shadow_audit_key_init);
#endif /* SHADOW_AUDIT */

int _shadow_mode_refcounts(struct domain *d)
{
    return shadow_mode_refcounts(d);
}


/**************************************************************************/
/* x86 emulator support for the shadow code
 */

struct segment_register *hvm_get_seg_reg(
    enum x86_segment seg, struct sh_emulate_ctxt *sh_ctxt)
{
    struct segment_register *seg_reg = &sh_ctxt->seg_reg[seg];
    if ( !__test_and_set_bit(seg, &sh_ctxt->valid_seg_regs) )
        hvm_get_segment_register(current, seg, seg_reg);
    return seg_reg;
}

static int hvm_translate_linear_addr(
    enum x86_segment seg,
    unsigned long offset,
    unsigned int bytes,
    enum hvm_access_type access_type,
    struct sh_emulate_ctxt *sh_ctxt,
    unsigned long *paddr)
{
    struct segment_register *reg = hvm_get_seg_reg(seg, sh_ctxt);
    int okay;

    okay = hvm_virtual_to_linear_addr(
        seg, reg, offset, bytes, access_type, sh_ctxt->ctxt.addr_size, paddr);

    if ( !okay )
    {
        hvm_inject_exception(TRAP_gp_fault, 0, 0);
        return X86EMUL_EXCEPTION;
    }

    return 0;
}

static int
hvm_read(enum x86_segment seg,
         unsigned long offset,
         void *p_data,
         unsigned int bytes,
         enum hvm_access_type access_type,
         struct sh_emulate_ctxt *sh_ctxt)
{
    unsigned long addr;
    int rc;

    rc = hvm_translate_linear_addr(
        seg, offset, bytes, access_type, sh_ctxt, &addr);
    if ( rc )
        return rc;

    if ( access_type == hvm_access_insn_fetch )
        rc = hvm_fetch_from_guest_virt(p_data, addr, bytes, 0);
    else
        rc = hvm_copy_from_guest_virt(p_data, addr, bytes, 0);

    switch ( rc )
    {
    case HVMCOPY_okay:
        return X86EMUL_OKAY;
    case HVMCOPY_bad_gva_to_gfn:
        return X86EMUL_EXCEPTION;
    case HVMCOPY_bad_gfn_to_mfn:
    case HVMCOPY_unhandleable:
        return X86EMUL_UNHANDLEABLE;
    case HVMCOPY_gfn_paged_out:
    case HVMCOPY_gfn_shared:
        return X86EMUL_RETRY;
    }

    BUG();
    return X86EMUL_UNHANDLEABLE;
}

static int
hvm_emulate_read(enum x86_segment seg,
                 unsigned long offset,
                 void *p_data,
                 unsigned int bytes,
                 struct x86_emulate_ctxt *ctxt)
{
    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;
    return hvm_read(seg, offset, p_data, bytes, hvm_access_read,
                    container_of(ctxt, struct sh_emulate_ctxt, ctxt));
}

static int
hvm_emulate_insn_fetch(enum x86_segment seg,
                       unsigned long offset,
                       void *p_data,
                       unsigned int bytes,
                       struct x86_emulate_ctxt *ctxt)
{
    struct sh_emulate_ctxt *sh_ctxt =
        container_of(ctxt, struct sh_emulate_ctxt, ctxt);
    unsigned int insn_off = offset - sh_ctxt->insn_buf_eip;

    ASSERT(seg == x86_seg_cs);

    /* Fall back if requested bytes are not in the prefetch cache. */
    if ( unlikely((insn_off + bytes) > sh_ctxt->insn_buf_bytes) )
        return hvm_read(seg, offset, p_data, bytes,
                        hvm_access_insn_fetch, sh_ctxt);

    /* Hit the cache. Simple memcpy. */
    memcpy(p_data, &sh_ctxt->insn_buf[insn_off], bytes);
    return X86EMUL_OKAY;
}

static int
hvm_emulate_write(enum x86_segment seg,
                  unsigned long offset,
                  void *p_data,
                  unsigned int bytes,
                  struct x86_emulate_ctxt *ctxt)
{
    struct sh_emulate_ctxt *sh_ctxt =
        container_of(ctxt, struct sh_emulate_ctxt, ctxt);
    struct vcpu *v = current;
    unsigned long addr;
    int rc;

    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;

    /* How many emulations could we save if we unshadowed on stack writes? */
    if ( seg == x86_seg_ss )
        perfc_incr(shadow_fault_emulate_stack);

    rc = hvm_translate_linear_addr(
        seg, offset, bytes, hvm_access_write, sh_ctxt, &addr);
    if ( rc )
        return rc;

    return v->arch.paging.mode->shadow.x86_emulate_write(
        v, addr, p_data, bytes, sh_ctxt);
}

static int 
hvm_emulate_cmpxchg(enum x86_segment seg,
                    unsigned long offset,
                    void *p_old,
                    void *p_new,
                    unsigned int bytes,
                    struct x86_emulate_ctxt *ctxt)
{
    struct sh_emulate_ctxt *sh_ctxt =
        container_of(ctxt, struct sh_emulate_ctxt, ctxt);
    struct vcpu *v = current;
    unsigned long addr, old[2], new[2];
    int rc;

    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;

    rc = hvm_translate_linear_addr(
        seg, offset, bytes, hvm_access_write, sh_ctxt, &addr);
    if ( rc )
        return rc;

    old[0] = new[0] = 0;
    memcpy(old, p_old, bytes);
    memcpy(new, p_new, bytes);

    if ( bytes <= sizeof(long) )
        return v->arch.paging.mode->shadow.x86_emulate_cmpxchg(
            v, addr, old[0], new[0], bytes, sh_ctxt);

#ifdef __i386__
    if ( bytes == 8 )
        return v->arch.paging.mode->shadow.x86_emulate_cmpxchg8b(
            v, addr, old[0], old[1], new[0], new[1], sh_ctxt);
#endif

    return X86EMUL_UNHANDLEABLE;
}

static const struct x86_emulate_ops hvm_shadow_emulator_ops = {
    .read       = hvm_emulate_read,
    .insn_fetch = hvm_emulate_insn_fetch,
    .write      = hvm_emulate_write,
    .cmpxchg    = hvm_emulate_cmpxchg,
};

static int
pv_emulate_read(enum x86_segment seg,
                unsigned long offset,
                void *p_data,
                unsigned int bytes,
                struct x86_emulate_ctxt *ctxt)
{
    unsigned int rc;

    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;

    if ( (rc = copy_from_user(p_data, (void *)offset, bytes)) != 0 )
    {
        propagate_page_fault(offset + bytes - rc, 0); /* read fault */
        return X86EMUL_EXCEPTION;
    }

    return X86EMUL_OKAY;
}

static int
pv_emulate_write(enum x86_segment seg,
                 unsigned long offset,
                 void *p_data,
                 unsigned int bytes,
                 struct x86_emulate_ctxt *ctxt)
{
    struct sh_emulate_ctxt *sh_ctxt =
        container_of(ctxt, struct sh_emulate_ctxt, ctxt);
    struct vcpu *v = current;
    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;
    return v->arch.paging.mode->shadow.x86_emulate_write(
        v, offset, p_data, bytes, sh_ctxt);
}

static int 
pv_emulate_cmpxchg(enum x86_segment seg,
                   unsigned long offset,
                   void *p_old,
                   void *p_new,
                   unsigned int bytes,
                   struct x86_emulate_ctxt *ctxt)
{
    struct sh_emulate_ctxt *sh_ctxt =
        container_of(ctxt, struct sh_emulate_ctxt, ctxt);
    unsigned long old[2], new[2];
    struct vcpu *v = current;

    if ( !is_x86_user_segment(seg) )
        return X86EMUL_UNHANDLEABLE;

    old[0] = new[0] = 0;
    memcpy(old, p_old, bytes);
    memcpy(new, p_new, bytes);

    if ( bytes <= sizeof(long) )
        return v->arch.paging.mode->shadow.x86_emulate_cmpxchg(
            v, offset, old[0], new[0], bytes, sh_ctxt);

#ifdef __i386__
    if ( bytes == 8 )
        return v->arch.paging.mode->shadow.x86_emulate_cmpxchg8b(
            v, offset, old[0], old[1], new[0], new[1], sh_ctxt);
#endif

    return X86EMUL_UNHANDLEABLE;
}

static const struct x86_emulate_ops pv_shadow_emulator_ops = {
    .read       = pv_emulate_read,
    .insn_fetch = pv_emulate_read,
    .write      = pv_emulate_write,
    .cmpxchg    = pv_emulate_cmpxchg,
};

const struct x86_emulate_ops *shadow_init_emulation(
    struct sh_emulate_ctxt *sh_ctxt, struct cpu_user_regs *regs)
{
    struct segment_register *creg, *sreg;
    struct vcpu *v = current;
    unsigned long addr;

    sh_ctxt->ctxt.regs = regs;
    sh_ctxt->ctxt.force_writeback = 0;

    if ( !is_hvm_vcpu(v) )
    {
        sh_ctxt->ctxt.addr_size = sh_ctxt->ctxt.sp_size = BITS_PER_LONG;
        return &pv_shadow_emulator_ops;
    }

    /* Segment cache initialisation. Primed with CS. */
    sh_ctxt->valid_seg_regs = 0;
    creg = hvm_get_seg_reg(x86_seg_cs, sh_ctxt);

    /* Work out the emulation mode. */
    if ( hvm_long_mode_enabled(v) && creg->attr.fields.l )
    {
        sh_ctxt->ctxt.addr_size = sh_ctxt->ctxt.sp_size = 64;
    }
    else
    {
        sreg = hvm_get_seg_reg(x86_seg_ss, sh_ctxt);
        sh_ctxt->ctxt.addr_size = creg->attr.fields.db ? 32 : 16;
        sh_ctxt->ctxt.sp_size   = sreg->attr.fields.db ? 32 : 16;
    }

    /* Attempt to prefetch whole instruction. */
    sh_ctxt->insn_buf_eip = regs->eip;
    sh_ctxt->insn_buf_bytes =
        (!hvm_translate_linear_addr(
            x86_seg_cs, regs->eip, sizeof(sh_ctxt->insn_buf),
            hvm_access_insn_fetch, sh_ctxt, &addr) &&
         !hvm_fetch_from_guest_virt_nofault(
             sh_ctxt->insn_buf, addr, sizeof(sh_ctxt->insn_buf), 0))
        ? sizeof(sh_ctxt->insn_buf) : 0;

    return &hvm_shadow_emulator_ops;
}

/* Update an initialized emulation context to prepare for the next 
 * instruction */
void shadow_continue_emulation(struct sh_emulate_ctxt *sh_ctxt, 
                               struct cpu_user_regs *regs)
{
    struct vcpu *v = current;
    unsigned long addr, diff;

    /* We don't refetch the segment bases, because we don't emulate
     * writes to segment registers */

    if ( is_hvm_vcpu(v) )
    {
        diff = regs->eip - sh_ctxt->insn_buf_eip;
        if ( diff > sh_ctxt->insn_buf_bytes )
        {
            /* Prefetch more bytes. */
            sh_ctxt->insn_buf_bytes =
                (!hvm_translate_linear_addr(
                    x86_seg_cs, regs->eip, sizeof(sh_ctxt->insn_buf),
                    hvm_access_insn_fetch, sh_ctxt, &addr) &&
                 !hvm_fetch_from_guest_virt_nofault(
                     sh_ctxt->insn_buf, addr, sizeof(sh_ctxt->insn_buf), 0))
                ? sizeof(sh_ctxt->insn_buf) : 0;
            sh_ctxt->insn_buf_eip = regs->eip;
        }
    }
}
 

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
/**************************************************************************/
/* Out-of-sync shadows. */ 

/* From time to time, we let a shadowed pagetable page go out of sync 
 * with its shadow: the guest is allowed to write directly to the page, 
 * and those writes are not synchronously reflected in the shadow.
 * This lets us avoid many emulations if the guest is writing a lot to a 
 * pagetable, but it relaxes a pretty important invariant in the shadow 
 * pagetable design.  Therefore, some rules:
 *
 * 1. Only L1 pagetables may go out of sync: any page that is shadowed
 *    at at higher level must be synchronously updated.  This makes
 *    using linear shadow pagetables much less dangerous.
 *    That means that: (a) unsyncing code needs to check for higher-level
 *    shadows, and (b) promotion code needs to resync.
 * 
 * 2. All shadow operations on a guest page require the page to be brought
 *    back into sync before proceeding.  This must be done under the
 *    shadow lock so that the page is guaranteed to remain synced until
 *    the operation completes.
 *
 *    Exceptions to this rule: the pagefault and invlpg handlers may 
 *    update only one entry on an out-of-sync page without resyncing it. 
 *
 * 3. Operations on shadows that do not start from a guest page need to
 *    be aware that they may be handling an out-of-sync shadow.
 *
 * 4. Operations that do not normally take the shadow lock (fast-path 
 *    #PF handler, INVLPG) must fall back to a locking, syncing version 
 *    if they see an out-of-sync table. 
 *
 * 5. Operations corresponding to guest TLB flushes (MOV CR3, INVLPG)
 *    must explicitly resync all relevant pages or update their
 *    shadows.
 *
 * Currently out-of-sync pages are listed in a simple open-addressed
 * hash table with a second chance (must resist temptation to radically
 * over-engineer hash tables...)  The virtual address of the access
 * which caused us to unsync the page is also kept in the hash table, as
 * a hint for finding the writable mappings later.
 *
 * We keep a hash per vcpu, because we want as much as possible to do
 * the re-sync on the save vcpu we did the unsync on, so the VA hint
 * will be valid.
 */


#if SHADOW_AUDIT & SHADOW_AUDIT_ENTRIES_FULL
static void sh_oos_audit(struct domain *d) 
{
    int idx, expected_idx, expected_idx_alt;
    struct page_info *pg;
    struct vcpu *v;
    
    for_each_vcpu(d, v) 
    {
        for ( idx = 0; idx < SHADOW_OOS_PAGES; idx++ )
        {
            mfn_t *oos = v->arch.paging.shadow.oos;
            if ( !mfn_valid(oos[idx]) )
                continue;
            
            expected_idx = mfn_x(oos[idx]) % SHADOW_OOS_PAGES;
            expected_idx_alt = ((expected_idx + 1) % SHADOW_OOS_PAGES);
            if ( idx != expected_idx && idx != expected_idx_alt )
            {
                printk("%s: idx %d contains gmfn %lx, expected at %d or %d.\n",
                       __func__, idx, mfn_x(oos[idx]), 
                       expected_idx, expected_idx_alt);
                BUG();
            }
            pg = mfn_to_page(oos[idx]);
            if ( !(pg->count_info & PGC_page_table) )
            {
                printk("%s: idx %x gmfn %lx not a pt (count %"PRIx32")\n",
                       __func__, idx, mfn_x(oos[idx]), pg->count_info);
                BUG();
            }
            if ( !(pg->shadow_flags & SHF_out_of_sync) )
            {
                printk("%s: idx %x gmfn %lx not marked oos (flags %lx)\n",
                       __func__, idx, mfn_x(oos[idx]), pg->shadow_flags);
                BUG();
            }
            if ( (pg->shadow_flags & SHF_page_type_mask & ~SHF_L1_ANY) )
            {
                printk("%s: idx %x gmfn %lx shadowed as non-l1 (flags %lx)\n",
                       __func__, idx, mfn_x(oos[idx]), pg->shadow_flags);
                BUG();
            }
        }
    }
}
#endif

#if SHADOW_AUDIT & SHADOW_AUDIT_ENTRIES
void oos_audit_hash_is_present(struct domain *d, mfn_t gmfn) 
{
    int idx;
    struct vcpu *v;
    mfn_t *oos;

    ASSERT(mfn_is_out_of_sync(gmfn));
    
    for_each_vcpu(d, v) 
    {
        oos = v->arch.paging.shadow.oos;
        idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) != mfn_x(gmfn) )
            idx = (idx + 1) % SHADOW_OOS_PAGES;
        
        if ( mfn_x(oos[idx]) == mfn_x(gmfn) )
            return;
    }

    SHADOW_ERROR("gmfn %lx marked OOS but not in hash table\n", mfn_x(gmfn));
    BUG();
}
#endif

/* Update the shadow, but keep the page out of sync. */
static inline void _sh_resync_l1(struct vcpu *v, mfn_t gmfn, mfn_t snpmfn)
{
    struct page_info *pg = mfn_to_page(gmfn);

    ASSERT(mfn_valid(gmfn));
    ASSERT(page_is_out_of_sync(pg));

    /* Call out to the appropriate per-mode resyncing function */
    if ( pg->shadow_flags & SHF_L1_32 )
        SHADOW_INTERNAL_NAME(sh_resync_l1, 2)(v, gmfn, snpmfn);
    else if ( pg->shadow_flags & SHF_L1_PAE )
        SHADOW_INTERNAL_NAME(sh_resync_l1, 3)(v, gmfn, snpmfn);
#if CONFIG_PAGING_LEVELS >= 4
    else if ( pg->shadow_flags & SHF_L1_64 )
        SHADOW_INTERNAL_NAME(sh_resync_l1, 4)(v, gmfn, snpmfn);
#endif
}


/*
 * Fixup arrays: We limit the maximum number of writable mappings to
 * SHADOW_OOS_FIXUPS and store enough information to remove them
 * quickly on resync.
 */

static inline int oos_fixup_flush_gmfn(struct vcpu *v, mfn_t gmfn,
                                       struct oos_fixup *fixup)
{
    int i;
    for ( i = 0; i < SHADOW_OOS_FIXUPS; i++ )
    {
        if ( mfn_x(fixup->smfn[i]) != INVALID_MFN )
        {
            sh_remove_write_access_from_sl1p(v, gmfn,
                                             fixup->smfn[i], 
                                             fixup->off[i]);
            fixup->smfn[i] = _mfn(INVALID_MFN);
        }
    }

    /* Always flush the TLBs. See comment on oos_fixup_add(). */
    return 1;
}

void oos_fixup_add(struct vcpu *v, mfn_t gmfn,
                   mfn_t smfn,  unsigned long off)
{
    int idx, next;
    mfn_t *oos;
    struct oos_fixup *oos_fixup;
    struct domain *d = v->domain;

    perfc_incr(shadow_oos_fixup_add);
    
    for_each_vcpu(d, v) 
    {
        oos = v->arch.paging.shadow.oos;
        oos_fixup = v->arch.paging.shadow.oos_fixup;
        idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) != mfn_x(gmfn) )
            idx = (idx + 1) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) == mfn_x(gmfn) )
        {
            int i;
            for ( i = 0; i < SHADOW_OOS_FIXUPS; i++ )
            {
                if ( mfn_valid(oos_fixup[idx].smfn[i])
                     && (mfn_x(oos_fixup[idx].smfn[i]) == mfn_x(smfn))
                     && (oos_fixup[idx].off[i] == off) )
                    return;
            }

            next = oos_fixup[idx].next;

            if ( mfn_x(oos_fixup[idx].smfn[next]) != INVALID_MFN )
            {
                TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_OOS_FIXUP_EVICT);

                /* Reuse this slot and remove current writable mapping. */
                sh_remove_write_access_from_sl1p(v, gmfn, 
                                                 oos_fixup[idx].smfn[next],
                                                 oos_fixup[idx].off[next]);
                perfc_incr(shadow_oos_fixup_evict);
                /* We should flush the TLBs now, because we removed a
                   writable mapping, but since the shadow is already
                   OOS we have no problem if another vcpu write to
                   this page table. We just have to be very careful to
                   *always* flush the tlbs on resync. */
            }

            oos_fixup[idx].smfn[next] = smfn;
            oos_fixup[idx].off[next] = off;
            oos_fixup[idx].next = (next + 1) % SHADOW_OOS_FIXUPS;

            TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_OOS_FIXUP_ADD);
            return;
        }
    }

    SHADOW_ERROR("gmfn %lx was OOS but not in hash table\n", mfn_x(gmfn));
    BUG();
}

static int oos_remove_write_access(struct vcpu *v, mfn_t gmfn,
                                   struct oos_fixup *fixup)
{
    int ftlb = 0;

    ftlb |= oos_fixup_flush_gmfn(v, gmfn, fixup);

    switch ( sh_remove_write_access(v, gmfn, 0, 0) )
    {
    default:
    case 0:
        break;

    case 1:
        ftlb |= 1;
        break;

    case -1:
        /* An unfindable writeable typecount has appeared, probably via a
         * grant table entry: can't shoot the mapping, so try to unshadow 
         * the page.  If that doesn't work either, the guest is granting
         * his pagetables and must be killed after all.
         * This will flush the tlb, so we can return with no worries. */
        sh_remove_shadows(v, gmfn, 0 /* Be thorough */, 1 /* Must succeed */);
        return 1;
    }

    if ( ftlb )
        flush_tlb_mask(&v->domain->domain_dirty_cpumask);

    return 0;
}


static inline void trace_resync(int event, mfn_t gmfn)
{
    if ( tb_init_done )
    {
        /* Convert gmfn to gfn */
        unsigned long gfn = mfn_to_gfn(current->domain, gmfn);
        __trace_var(event, 0/*!tsc*/, sizeof(gfn), (unsigned char*)&gfn);
    }
}

/* Pull all the entries on an out-of-sync page back into sync. */
static void _sh_resync(struct vcpu *v, mfn_t gmfn,
                       struct oos_fixup *fixup, mfn_t snp)
{
    struct page_info *pg = mfn_to_page(gmfn);

    ASSERT(shadow_locked_by_me(v->domain));
    ASSERT(mfn_is_out_of_sync(gmfn));
    /* Guest page must be shadowed *only* as L1 when out of sync. */
    ASSERT(!(mfn_to_page(gmfn)->shadow_flags & SHF_page_type_mask 
             & ~SHF_L1_ANY));
    ASSERT(!sh_page_has_multiple_shadows(mfn_to_page(gmfn)));

    SHADOW_PRINTK("d=%d, v=%d, gmfn=%05lx\n",
                  v->domain->domain_id, v->vcpu_id, mfn_x(gmfn));

    /* Need to pull write access so the page *stays* in sync. */
    if ( oos_remove_write_access(v, gmfn, fixup) )
    {
        /* Page has been unshadowed. */
        return;
    }

    /* No more writable mappings of this page, please */
    pg->shadow_flags &= ~SHF_oos_may_write;

    /* Update the shadows with current guest entries. */
    _sh_resync_l1(v, gmfn, snp);

    /* Now we know all the entries are synced, and will stay that way */
    pg->shadow_flags &= ~SHF_out_of_sync;
    perfc_incr(shadow_resync);
    trace_resync(TRC_SHADOW_RESYNC_FULL, gmfn);
}


/* Add an MFN to the list of out-of-sync guest pagetables */
static void oos_hash_add(struct vcpu *v, mfn_t gmfn)
{
    int i, idx, oidx, swap = 0;
    void *gptr, *gsnpptr;
    mfn_t *oos = v->arch.paging.shadow.oos;
    mfn_t *oos_snapshot = v->arch.paging.shadow.oos_snapshot;
    struct oos_fixup *oos_fixup = v->arch.paging.shadow.oos_fixup;
    struct oos_fixup fixup = { .next = 0 };
    
    for (i = 0; i < SHADOW_OOS_FIXUPS; i++ )
        fixup.smfn[i] = _mfn(INVALID_MFN);

    idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
    oidx = idx;

    if ( mfn_valid(oos[idx]) 
         && (mfn_x(oos[idx]) % SHADOW_OOS_PAGES) == idx )
    {
        /* Punt the current occupant into the next slot */
        SWAP(oos[idx], gmfn);
        SWAP(oos_fixup[idx], fixup);
        swap = 1;
        idx = (idx + 1) % SHADOW_OOS_PAGES;
    }
    if ( mfn_valid(oos[idx]) )
   {
        /* Crush the current occupant. */
        _sh_resync(v, oos[idx], &oos_fixup[idx], oos_snapshot[idx]);
        perfc_incr(shadow_unsync_evict);
    }
    oos[idx] = gmfn;
    oos_fixup[idx] = fixup;

    if ( swap )
        SWAP(oos_snapshot[idx], oos_snapshot[oidx]);

    gptr = sh_map_domain_page(oos[oidx]);
    gsnpptr = sh_map_domain_page(oos_snapshot[oidx]);
    memcpy(gsnpptr, gptr, PAGE_SIZE);
    sh_unmap_domain_page(gptr);
    sh_unmap_domain_page(gsnpptr);
}

/* Remove an MFN from the list of out-of-sync guest pagetables */
static void oos_hash_remove(struct vcpu *v, mfn_t gmfn)
{
    int idx;
    mfn_t *oos;
    struct domain *d = v->domain;

    SHADOW_PRINTK("D%dV%d gmfn %lx\n",
                  v->domain->domain_id, v->vcpu_id, mfn_x(gmfn)); 

    for_each_vcpu(d, v) 
    {
        oos = v->arch.paging.shadow.oos;
        idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) != mfn_x(gmfn) )
            idx = (idx + 1) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) == mfn_x(gmfn) )
        {
            oos[idx] = _mfn(INVALID_MFN);
            return;
        }
    }

    SHADOW_ERROR("gmfn %lx was OOS but not in hash table\n", mfn_x(gmfn));
    BUG();
}

mfn_t oos_snapshot_lookup(struct vcpu *v, mfn_t gmfn)
{
    int idx;
    mfn_t *oos;
    mfn_t *oos_snapshot;
    struct domain *d = v->domain;
    
    for_each_vcpu(d, v) 
    {
        oos = v->arch.paging.shadow.oos;
        oos_snapshot = v->arch.paging.shadow.oos_snapshot;
        idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) != mfn_x(gmfn) )
            idx = (idx + 1) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) == mfn_x(gmfn) )
        {
            return oos_snapshot[idx];
        }
    }

    SHADOW_ERROR("gmfn %lx was OOS but not in hash table\n", mfn_x(gmfn));
    BUG();
    return _mfn(INVALID_MFN);
}

/* Pull a single guest page back into sync */
void sh_resync(struct vcpu *v, mfn_t gmfn)
{
    int idx;
    mfn_t *oos;
    mfn_t *oos_snapshot;
    struct oos_fixup *oos_fixup;
    struct domain *d = v->domain;

    for_each_vcpu(d, v) 
    {
        oos = v->arch.paging.shadow.oos;
        oos_fixup = v->arch.paging.shadow.oos_fixup;
        oos_snapshot = v->arch.paging.shadow.oos_snapshot;
        idx = mfn_x(gmfn) % SHADOW_OOS_PAGES;
        if ( mfn_x(oos[idx]) != mfn_x(gmfn) )
            idx = (idx + 1) % SHADOW_OOS_PAGES;
        
        if ( mfn_x(oos[idx]) == mfn_x(gmfn) )
        {
            _sh_resync(v, gmfn, &oos_fixup[idx], oos_snapshot[idx]);
            oos[idx] = _mfn(INVALID_MFN);
            return;
        }
    }

    SHADOW_ERROR("gmfn %lx was OOS but not in hash table\n", mfn_x(gmfn));
    BUG();
}

/* Figure out whether it's definitely safe not to sync this l1 table,
 * by making a call out to the mode in which that shadow was made. */
static int sh_skip_sync(struct vcpu *v, mfn_t gl1mfn)
{
    struct page_info *pg = mfn_to_page(gl1mfn);
    if ( pg->shadow_flags & SHF_L1_32 )
        return SHADOW_INTERNAL_NAME(sh_safe_not_to_sync, 2)(v, gl1mfn);
    else if ( pg->shadow_flags & SHF_L1_PAE )
        return SHADOW_INTERNAL_NAME(sh_safe_not_to_sync, 3)(v, gl1mfn);
#if CONFIG_PAGING_LEVELS >= 4
    else if ( pg->shadow_flags & SHF_L1_64 )
        return SHADOW_INTERNAL_NAME(sh_safe_not_to_sync, 4)(v, gl1mfn);
#endif
    SHADOW_ERROR("gmfn 0x%lx was OOS but not shadowed as an l1.\n", 
                 mfn_x(gl1mfn));
    BUG();
    return 0; /* BUG() is no longer __attribute__((noreturn)). */
}


/* Pull all out-of-sync pages back into sync.  Pages brought out of sync
 * on other vcpus are allowed to remain out of sync, but their contents
 * will be made safe (TLB flush semantics); pages unsynced by this vcpu
 * are brought back into sync and write-protected.  If skip != 0, we try
 * to avoid resyncing at all if we think we can get away with it. */
void sh_resync_all(struct vcpu *v, int skip, int this, int others, int do_locking)
{
    int idx;
    struct vcpu *other;
    mfn_t *oos = v->arch.paging.shadow.oos;
    mfn_t *oos_snapshot = v->arch.paging.shadow.oos_snapshot;
    struct oos_fixup *oos_fixup = v->arch.paging.shadow.oos_fixup;

    SHADOW_PRINTK("d=%d, v=%d\n", v->domain->domain_id, v->vcpu_id);

    ASSERT(do_locking || shadow_locked_by_me(v->domain));

    if ( !this )
        goto resync_others;

    if ( do_locking )
        shadow_lock(v->domain);

    /* First: resync all of this vcpu's oos pages */
    for ( idx = 0; idx < SHADOW_OOS_PAGES; idx++ ) 
        if ( mfn_valid(oos[idx]) )
        {
            /* Write-protect and sync contents */
            _sh_resync(v, oos[idx], &oos_fixup[idx], oos_snapshot[idx]);
            oos[idx] = _mfn(INVALID_MFN);
        }

    if ( do_locking )
        shadow_unlock(v->domain);

 resync_others:
    if ( !others )
        return;

    /* Second: make all *other* vcpus' oos pages safe. */
    for_each_vcpu(v->domain, other)
    {
        if ( v == other ) 
            continue;

        if ( do_locking )
            shadow_lock(v->domain);

        oos = other->arch.paging.shadow.oos;
        oos_fixup = other->arch.paging.shadow.oos_fixup;
        oos_snapshot = other->arch.paging.shadow.oos_snapshot;

        for ( idx = 0; idx < SHADOW_OOS_PAGES; idx++ ) 
        {
            if ( !mfn_valid(oos[idx]) )
                continue;

            if ( skip )
            {
                /* Update the shadows and leave the page OOS. */
                if ( sh_skip_sync(v, oos[idx]) )
                    continue;
                trace_resync(TRC_SHADOW_RESYNC_ONLY, oos[idx]);
                _sh_resync_l1(other, oos[idx], oos_snapshot[idx]);
            }
            else
            {
                /* Write-protect and sync contents */
                _sh_resync(other, oos[idx], &oos_fixup[idx], oos_snapshot[idx]);
                oos[idx] = _mfn(INVALID_MFN);
            }
        }
        
        if ( do_locking )
            shadow_unlock(v->domain);
    }
}

/* Allow a shadowed page to go out of sync. Unsyncs are traced in
 * multi.c:sh_page_fault() */
int sh_unsync(struct vcpu *v, mfn_t gmfn)
{
    struct page_info *pg;
    
    ASSERT(shadow_locked_by_me(v->domain));

    SHADOW_PRINTK("d=%d, v=%d, gmfn=%05lx\n",
                  v->domain->domain_id, v->vcpu_id, mfn_x(gmfn));

    pg = mfn_to_page(gmfn);
 
    /* Guest page must be shadowed *only* as L1 and *only* once when out
     * of sync.  Also, get out now if it's already out of sync. 
     * Also, can't safely unsync if some vcpus have paging disabled.*/
    if ( pg->shadow_flags & 
         ((SHF_page_type_mask & ~SHF_L1_ANY) | SHF_out_of_sync) 
         || sh_page_has_multiple_shadows(pg)
         || !is_hvm_domain(v->domain)
         || !v->domain->arch.paging.shadow.oos_active )
        return 0;

    pg->shadow_flags |= SHF_out_of_sync|SHF_oos_may_write;
    oos_hash_add(v, gmfn);
    perfc_incr(shadow_unsync);
    TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_UNSYNC);
    return 1;
}

#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) */


/**************************************************************************/
/* Code for "promoting" a guest page to the point where the shadow code is
 * willing to let it be treated as a guest page table.  This generally
 * involves making sure there are no writable mappings available to the guest
 * for this page.
 */
void shadow_promote(struct vcpu *v, mfn_t gmfn, unsigned int type)
{
    struct page_info *page = mfn_to_page(gmfn);

    ASSERT(mfn_valid(gmfn));

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
    /* Is the page already shadowed and out of sync? */
    if ( page_is_out_of_sync(page) ) 
        sh_resync(v, gmfn);
#endif

    /* We should never try to promote a gmfn that has writeable mappings */
    ASSERT((page->u.inuse.type_info & PGT_type_mask) != PGT_writable_page
           || (page->u.inuse.type_info & PGT_count_mask) == 0
           || v->domain->is_shutting_down);

    /* Is the page already shadowed? */
    if ( !test_and_set_bit(_PGC_page_table, &page->count_info) )
        page->shadow_flags = 0;

    ASSERT(!test_bit(type, &page->shadow_flags));
    set_bit(type, &page->shadow_flags);
    TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_PROMOTE);
}

void shadow_demote(struct vcpu *v, mfn_t gmfn, u32 type)
{
    struct page_info *page = mfn_to_page(gmfn);

    ASSERT(test_bit(_PGC_page_table, &page->count_info));
    ASSERT(test_bit(type, &page->shadow_flags));

    clear_bit(type, &page->shadow_flags);

    if ( (page->shadow_flags & SHF_page_type_mask) == 0 )
    {
#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
        /* Was the page out of sync? */
        if ( page_is_out_of_sync(page) ) 
        {
            oos_hash_remove(v, gmfn);
        }
#endif 
        clear_bit(_PGC_page_table, &page->count_info);
    }

    TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_DEMOTE);
}

/**************************************************************************/
/* Validate a pagetable change from the guest and update the shadows.
 * Returns a bitmask of SHADOW_SET_* flags. */

int
sh_validate_guest_entry(struct vcpu *v, mfn_t gmfn, void *entry, u32 size)
{
    int result = 0;
    struct page_info *page = mfn_to_page(gmfn);

    paging_mark_dirty(v->domain, mfn_x(gmfn));
    
    // Determine which types of shadows are affected, and update each.
    //
    // Always validate L1s before L2s to prevent another cpu with a linear
    // mapping of this gmfn from seeing a walk that results from 
    // using the new L2 value and the old L1 value.  (It is OK for such a
    // guest to see a walk that uses the old L2 value with the new L1 value,
    // as hardware could behave this way if one level of the pagewalk occurs
    // before the store, and the next level of the pagewalk occurs after the
    // store.
    //
    // Ditto for L2s before L3s, etc.
    //

    if ( !(page->count_info & PGC_page_table) )
        return 0;  /* Not shadowed at all */

    if ( page->shadow_flags & SHF_L1_32 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl1e, 2)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L2_32 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl2e, 2)
            (v, gmfn, entry, size);

    if ( page->shadow_flags & SHF_L1_PAE ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl1e, 3)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L2_PAE ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl2e, 3)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L2H_PAE ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl2he, 3)
            (v, gmfn, entry, size);

#if CONFIG_PAGING_LEVELS >= 4 
    if ( page->shadow_flags & SHF_L1_64 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl1e, 4)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L2_64 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl2e, 4)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L2H_64 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl2he, 4)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L3_64 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl3e, 4)
            (v, gmfn, entry, size);
    if ( page->shadow_flags & SHF_L4_64 ) 
        result |= SHADOW_INTERNAL_NAME(sh_map_and_validate_gl4e, 4)
            (v, gmfn, entry, size);
#else /* 32-bit hypervisor does not support 64-bit guests */
    ASSERT((page->shadow_flags 
            & (SHF_L4_64|SHF_L3_64|SHF_L2H_64|SHF_L2_64|SHF_L1_64)) == 0);
#endif
    this_cpu(trace_shadow_path_flags) |= (result<<(TRCE_SFLAG_SET_CHANGED)); 

    return result;
}


void
sh_validate_guest_pt_write(struct vcpu *v, mfn_t gmfn,
                           void *entry, u32 size)
/* This is the entry point for emulated writes to pagetables in HVM guests and
 * PV translated guests.
 */
{
    struct domain *d = v->domain;
    int rc;

    ASSERT(shadow_locked_by_me(v->domain));
    rc = sh_validate_guest_entry(v, gmfn, entry, size);
    if ( rc & SHADOW_SET_FLUSH )
        /* Need to flush TLBs to pick up shadow PT changes */
        flush_tlb_mask(&d->domain_dirty_cpumask);
    if ( rc & SHADOW_SET_ERROR ) 
    {
        /* This page is probably not a pagetable any more: tear it out of the 
         * shadows, along with any tables that reference it.  
         * Since the validate call above will have made a "safe" (i.e. zero) 
         * shadow entry, we can let the domain live even if we can't fully 
         * unshadow the page. */
        sh_remove_shadows(v, gmfn, 0, 0);
    }
}

int shadow_write_guest_entry(struct vcpu *v, intpte_t *p,
                             intpte_t new, mfn_t gmfn)
/* Write a new value into the guest pagetable, and update the shadows 
 * appropriately.  Returns 0 if we page-faulted, 1 for success. */
{
    int failed;
    shadow_lock(v->domain);
    failed = __copy_to_user(p, &new, sizeof(new));
    if ( failed != sizeof(new) )
        sh_validate_guest_entry(v, gmfn, p, sizeof(new));
    shadow_unlock(v->domain);
    return (failed == 0);
}

int shadow_cmpxchg_guest_entry(struct vcpu *v, intpte_t *p,
                               intpte_t *old, intpte_t new, mfn_t gmfn)
/* Cmpxchg a new value into the guest pagetable, and update the shadows 
 * appropriately. Returns 0 if we page-faulted, 1 if not.
 * N.B. caller should check the value of "old" to see if the
 * cmpxchg itself was successful. */
{
    int failed;
    intpte_t t = *old;
    shadow_lock(v->domain);
    failed = cmpxchg_user(p, t, new);
    if ( t == *old )
        sh_validate_guest_entry(v, gmfn, p, sizeof(new));
    *old = t;
    shadow_unlock(v->domain);
    return (failed == 0);
}


/**************************************************************************/
/* Memory management for shadow pages. */ 

/* Allocating shadow pages
 * -----------------------
 *
 * Most shadow pages are allocated singly, but there is one case where
 * we need to allocate multiple pages together: shadowing 32-bit guest
 * tables on PAE or 64-bit shadows.  A 32-bit guest l1 table covers 4MB
 * of virtual address space, and needs to be shadowed by two PAE/64-bit
 * l1 tables (covering 2MB of virtual address space each).  Similarly, a
 * 32-bit guest l2 table (4GB va) needs to be shadowed by four
 * PAE/64-bit l2 tables (1GB va each).  These multi-page shadows are
 * contiguous and aligned; functions for handling offsets into them are
 * defined in shadow.c (shadow_l1_index() etc.)
 *    
 * This table shows the allocation behaviour of the different modes:
 *
 * Xen paging      pae  pae  64b  64b  64b
 * Guest paging    32b  pae  32b  pae  64b
 * PV or HVM       HVM   *   HVM  HVM   * 
 * Shadow paging   pae  pae  pae  pae  64b
 *
 * sl1 size         8k   4k   8k   4k   4k
 * sl2 size        16k   4k  16k   4k   4k
 * sl3 size         -    -    -    -    4k
 * sl4 size         -    -    -    -    4k
 *
 * We allocate memory from xen in four-page units and break them down
 * with a simple buddy allocator.  Can't use the xen allocator to handle
 * this as it only works for contiguous zones, and a domain's shadow
 * pool is made of fragments.
 *
 * In HVM guests, the p2m table is built out of shadow pages, and we provide 
 * a function for the p2m management to steal pages, in max-order chunks, from 
 * the free pool.  We don't provide for giving them back, yet.
 */

/* Figure out the least acceptable quantity of shadow memory.
 * The minimum memory requirement for always being able to free up a
 * chunk of memory is very small -- only three max-order chunks per
 * vcpu to hold the top level shadows and pages with Xen mappings in them.  
 *
 * But for a guest to be guaranteed to successfully execute a single
 * instruction, we must be able to map a large number (about thirty) VAs
 * at the same time, which means that to guarantee progress, we must
 * allow for more than ninety allocated pages per vcpu.  We round that
 * up to 128 pages, or half a megabyte per vcpu, and add 1 more vcpu's 
 * worth to make sure we never return zero. */
static unsigned int shadow_min_acceptable_pages(struct domain *d) 
{
    u32 vcpu_count = 1;
    struct vcpu *v;

    for_each_vcpu(d, v)
        vcpu_count++;

    return (vcpu_count * 128);
} 

/* Figure out the order of allocation needed for a given shadow type */
static inline u32
shadow_order(unsigned int shadow_type) 
{
    static const u32 type_to_order[SH_type_unused] = {
        0, /* SH_type_none           */
        1, /* SH_type_l1_32_shadow   */
        1, /* SH_type_fl1_32_shadow  */
        2, /* SH_type_l2_32_shadow   */
        0, /* SH_type_l1_pae_shadow  */
        0, /* SH_type_fl1_pae_shadow */
        0, /* SH_type_l2_pae_shadow  */
        0, /* SH_type_l2h_pae_shadow */
        0, /* SH_type_l1_64_shadow   */
        0, /* SH_type_fl1_64_shadow  */
        0, /* SH_type_l2_64_shadow   */
        0, /* SH_type_l2h_64_shadow  */
        0, /* SH_type_l3_64_shadow   */
        0, /* SH_type_l4_64_shadow   */
        2, /* SH_type_p2m_table      */
        0, /* SH_type_monitor_table  */
        0  /* SH_type_oos_snapshot   */
        };
    ASSERT(shadow_type < SH_type_unused);
    return type_to_order[shadow_type];
}

static inline unsigned int
shadow_max_order(struct domain *d)
{
    return is_hvm_domain(d) ? SHADOW_MAX_ORDER : 0;
}

/* Do we have at total of count pages of the requested order free? */
static inline int space_is_available(
    struct domain *d,
    unsigned int order,
    unsigned int count)
{
    for ( ; order <= shadow_max_order(d); ++order )
    {
        unsigned int n = count;
        const struct page_info *sp;

        page_list_for_each ( sp, &d->arch.paging.shadow.freelists[order] )
            if ( --n == 0 )
                return 1;
        count = (count + 1) >> 1;
    }

    return 0;
}

/* Dispatcher function: call the per-mode function that will unhook the
 * non-Xen mappings in this top-level shadow mfn */
static void shadow_unhook_mappings(struct vcpu *v, mfn_t smfn)
{
    struct page_info *sp = mfn_to_page(smfn);
    switch ( sp->u.sh.type )
    {
    case SH_type_l2_32_shadow:
        SHADOW_INTERNAL_NAME(sh_unhook_32b_mappings, 2)(v,smfn);
        break;
    case SH_type_l2_pae_shadow:
    case SH_type_l2h_pae_shadow:
        SHADOW_INTERNAL_NAME(sh_unhook_pae_mappings, 3)(v,smfn);
        break;
#if CONFIG_PAGING_LEVELS >= 4
    case SH_type_l4_64_shadow:
        SHADOW_INTERNAL_NAME(sh_unhook_64b_mappings, 4)(v,smfn);
        break;
#endif
    default:
        SHADOW_ERROR("top-level shadow has bad type %08x\n", sp->u.sh.type);
        BUG();
    }
}

static inline void trace_shadow_prealloc_unpin(struct domain *d, mfn_t smfn)
{
    if ( tb_init_done )
    {
        /* Convert smfn to gfn */
        unsigned long gfn;
        ASSERT(mfn_valid(smfn));
        gfn = mfn_to_gfn(d, backpointer(mfn_to_page(smfn)));
        __trace_var(TRC_SHADOW_PREALLOC_UNPIN, 0/*!tsc*/,
                    sizeof(gfn), (unsigned char*)&gfn);
    }
}

/* Make sure there are at least count order-sized pages
 * available in the shadow page pool. */
static void _shadow_prealloc(
    struct domain *d,
    unsigned int order,
    unsigned int count)
{
    /* Need a vpcu for calling unpins; for now, since we don't have
     * per-vcpu shadows, any will do */
    struct vcpu *v, *v2;
    struct page_info *sp, *t;
    mfn_t smfn;
    int i;

    ASSERT(order <= shadow_max_order(d));
    if ( space_is_available(d, order, count) ) return;
    
    v = current;
    if ( v->domain != d )
        v = d->vcpu[0];
    ASSERT(v != NULL); /* Shouldn't have enabled shadows if we've no vcpus  */

    /* Stage one: walk the list of pinned pages, unpinning them */
    perfc_incr(shadow_prealloc_1);
    page_list_for_each_safe_reverse(sp, t, &d->arch.paging.shadow.pinned_shadows)
    {
        smfn = page_to_mfn(sp);

        /* Unpin this top-level shadow */
        trace_shadow_prealloc_unpin(d, smfn);
        sh_unpin(v, smfn);

        /* See if that freed up enough space */
        if ( space_is_available(d, order, count) ) return;
    }

    /* Stage two: all shadow pages are in use in hierarchies that are
     * loaded in cr3 on some vcpu.  Walk them, unhooking the non-Xen
     * mappings. */
    perfc_incr(shadow_prealloc_2);

    for_each_vcpu(d, v2) 
        for ( i = 0 ; i < 4 ; i++ )
        {
            if ( !pagetable_is_null(v2->arch.shadow_table[i]) )
            {
                TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_PREALLOC_UNHOOK);
                shadow_unhook_mappings(v, 
                               pagetable_get_mfn(v2->arch.shadow_table[i]));

                /* See if that freed up enough space */
                if ( space_is_available(d, order, count) )
                {
                    flush_tlb_mask(&d->domain_dirty_cpumask);
                    return;
                }
            }
        }
    
    /* Nothing more we can do: all remaining shadows are of pages that
     * hold Xen mappings for some vcpu.  This can never happen. */
    SHADOW_ERROR("Can't pre-allocate %u order-%u shadow pages!\n"
                 "  shadow pages total = %u, free = %u, p2m=%u\n",
                 count, order,
                 d->arch.paging.shadow.total_pages,
                 d->arch.paging.shadow.free_pages,
                 d->arch.paging.shadow.p2m_pages);
    BUG();
}

/* Make sure there are at least count pages of the order according to
 * type available in the shadow page pool.
 * This must be called before any calls to shadow_alloc().  Since this
 * will free existing shadows to make room, it must be called early enough
 * to avoid freeing shadows that the caller is currently working on. */
void shadow_prealloc(struct domain *d, u32 type, unsigned int count)
{
    return _shadow_prealloc(d, shadow_order(type), count);
}

/* Deliberately free all the memory we can: this will tear down all of
 * this domain's shadows */
static void shadow_blow_tables(struct domain *d) 
{
    struct page_info *sp, *t;
    struct vcpu *v = d->vcpu[0];
    mfn_t smfn;
    int i;

    ASSERT(v != NULL);

    /* Pass one: unpin all pinned pages */
    page_list_for_each_safe_reverse(sp, t, &d->arch.paging.shadow.pinned_shadows)
    {
        smfn = page_to_mfn(sp);
        sh_unpin(v, smfn);
    }
        
    /* Second pass: unhook entries of in-use shadows */
    for_each_vcpu(d, v) 
        for ( i = 0 ; i < 4 ; i++ )
            if ( !pagetable_is_null(v->arch.shadow_table[i]) )
                shadow_unhook_mappings(v, 
                               pagetable_get_mfn(v->arch.shadow_table[i]));

    /* Make sure everyone sees the unshadowings */
    flush_tlb_mask(&d->domain_dirty_cpumask);
}

void shadow_blow_tables_per_domain(struct domain *d)
{
    if ( shadow_mode_enabled(d) && d->vcpu != NULL && d->vcpu[0] != NULL ) {
        shadow_lock(d);
        shadow_blow_tables(d);
        shadow_unlock(d);
    }
}

#ifndef NDEBUG
/* Blow all shadows of all shadowed domains: this can be used to cause the
 * guest's pagetables to be re-shadowed if we suspect that the shadows
 * have somehow got out of sync */
static void shadow_blow_all_tables(unsigned char c)
{
    struct domain *d;
    printk("'%c' pressed -> blowing all shadow tables\n", c);
    rcu_read_lock(&domlist_read_lock);
    for_each_domain(d)
    {
        if ( shadow_mode_enabled(d) && d->vcpu != NULL && d->vcpu[0] != NULL )
        {
            shadow_lock(d);
            shadow_blow_tables(d);
            shadow_unlock(d);
        }
    }
    rcu_read_unlock(&domlist_read_lock);
}

static struct keyhandler shadow_blow_all_tables_keyhandler = {
    .u.fn = shadow_blow_all_tables,
    .desc = "reset shadow pagetables"
};

/* Register this function in the Xen console keypress table */
static __init int shadow_blow_tables_keyhandler_init(void)
{
    register_keyhandler('S', &shadow_blow_all_tables_keyhandler);
    return 0;
}
__initcall(shadow_blow_tables_keyhandler_init);
#endif /* !NDEBUG */

static inline struct page_info *
next_shadow(const struct page_info *sp)
{
    return sp->next_shadow ? pdx_to_page(sp->next_shadow) : NULL;
}

static inline void
set_next_shadow(struct page_info *sp, struct page_info *next)
{
    sp->next_shadow = next ? page_to_pdx(next) : 0;
}

/* Allocate another shadow's worth of (contiguous, aligned) pages,
 * and fill in the type and backpointer fields of their page_infos. 
 * Never fails to allocate. */
mfn_t shadow_alloc(struct domain *d,  
                    u32 shadow_type,
                    unsigned long backpointer)
{
    struct page_info *sp = NULL;
    unsigned int order = shadow_order(shadow_type);
    cpumask_t mask;
    void *p;
    int i;

    ASSERT(shadow_locked_by_me(d));
    if (shadow_type == SH_type_p2m_table && order > shadow_max_order(d))
        order = shadow_max_order(d);
    ASSERT(order <= shadow_max_order(d));
    ASSERT(shadow_type != SH_type_none);
    perfc_incr(shadow_alloc);

    /* Find smallest order which can satisfy the request. */
    for ( i = order; i <= SHADOW_MAX_ORDER; i++ )
        if ( (sp = page_list_remove_head(&d->arch.paging.shadow.freelists[i])) )
            goto found;
    
    /* If we get here, we failed to allocate. This should never happen.
     * It means that we didn't call shadow_prealloc() correctly before
     * we allocated.  We can't recover by calling prealloc here, because
     * we might free up higher-level pages that the caller is working on. */
    SHADOW_ERROR("Can't allocate %i shadow pages!\n", 1 << order);
    BUG();

 found:
    /* We may have to halve the chunk a number of times. */
    while ( i != order )
    {
        i--;
        sp->v.free.order = i;
        page_list_add_tail(sp, &d->arch.paging.shadow.freelists[i]);
        sp += 1 << i;
    }
    d->arch.paging.shadow.free_pages -= 1 << order;

    switch (shadow_type)
    {
    case SH_type_fl1_32_shadow:
    case SH_type_fl1_pae_shadow:
    case SH_type_fl1_64_shadow:
        break;
    default:
        backpointer = pfn_to_pdx(backpointer);
        break;
    }

    /* Init page info fields and clear the pages */
    for ( i = 0; i < 1<<order ; i++ ) 
    {
        /* Before we overwrite the old contents of this page, 
         * we need to be sure that no TLB holds a pointer to it. */
        mask = d->domain_dirty_cpumask;
        tlbflush_filter(mask, sp[i].tlbflush_timestamp);
        if ( unlikely(!cpus_empty(mask)) )
        {
            perfc_incr(shadow_alloc_tlbflush);
            flush_tlb_mask(&mask);
        }
        /* Now safe to clear the page for reuse */
        p = __map_domain_page(sp+i);
        ASSERT(p != NULL);
        clear_page(p);
        sh_unmap_domain_page(p);
        INIT_PAGE_LIST_ENTRY(&sp[i].list);
        sp[i].u.sh.type = shadow_type;
        sp[i].u.sh.pinned = 0;
        sp[i].u.sh.count = 0;
        sp[i].v.sh.back = backpointer;
        set_next_shadow(&sp[i], NULL);
        perfc_incr(shadow_alloc_count);
    }
    return page_to_mfn(sp);
}


/* Return some shadow pages to the pool. */
void shadow_free(struct domain *d, mfn_t smfn)
{
    struct page_info *sp = mfn_to_page(smfn); 
    u32 shadow_type;
    unsigned long order;
    unsigned long mask;
    int i;

    ASSERT(shadow_locked_by_me(d));
    perfc_incr(shadow_free);

    shadow_type = sp->u.sh.type;
    ASSERT(shadow_type != SH_type_none);
    ASSERT(shadow_type != SH_type_p2m_table);
    order = shadow_order(shadow_type);

    d->arch.paging.shadow.free_pages += 1 << order;

    for ( i = 0; i < 1<<order; i++ ) 
    {
#if SHADOW_OPTIMIZATIONS & (SHOPT_WRITABLE_HEURISTIC | SHOPT_FAST_EMULATION)
        struct vcpu *v;
        for_each_vcpu(d, v) 
        {
#if SHADOW_OPTIMIZATIONS & SHOPT_WRITABLE_HEURISTIC
            /* No longer safe to look for a writeable mapping in this shadow */
            if ( v->arch.paging.shadow.last_writeable_pte_smfn == mfn_x(smfn) + i ) 
                v->arch.paging.shadow.last_writeable_pte_smfn = 0;
#endif
#if SHADOW_OPTIMIZATIONS & SHOPT_FAST_EMULATION
            v->arch.paging.last_write_emul_ok = 0;
#endif
        }
#endif
        /* Strip out the type: this is now a free shadow page */
        sp[i].u.sh.type = 0;
        /* Remember the TLB timestamp so we will know whether to flush 
         * TLBs when we reuse the page.  Because the destructors leave the
         * contents of the pages in place, we can delay TLB flushes until
         * just before the allocator hands the page out again. */
        sp[i].tlbflush_timestamp = tlbflush_current_time();
        perfc_decr(shadow_alloc_count);
    }

    /* Merge chunks as far as possible. */
    for ( ; order < shadow_max_order(d); ++order )
    {
        mask = 1 << order;
        if ( (mfn_x(page_to_mfn(sp)) & mask) ) {
            /* Merge with predecessor block? */
            if ( ((sp-mask)->u.sh.type != PGT_none) ||
                 ((sp-mask)->v.free.order != order) )
                break;
            sp -= mask;
            page_list_del(sp, &d->arch.paging.shadow.freelists[order]);
        } else {
            /* Merge with successor block? */
            if ( ((sp+mask)->u.sh.type != PGT_none) ||
                 ((sp+mask)->v.free.order != order) )
                break;
            page_list_del(sp + mask, &d->arch.paging.shadow.freelists[order]);
        }
    }

    sp->v.free.order = order;
    page_list_add_tail(sp, &d->arch.paging.shadow.freelists[order]);
}

/* Divert some memory from the pool to be used by the p2m mapping.
 * This action is irreversible: the p2m mapping only ever grows.
 * That's OK because the p2m table only exists for translated domains,
 * and those domains can't ever turn off shadow mode.
 * Also, we only ever allocate a max-order chunk, so as to preserve
 * the invariant that shadow_prealloc() always works.
 * Returns 0 iff it can't get a chunk (the caller should then
 * free up some pages in domheap and call sh_set_allocation);
 * returns non-zero on success.
 */
static int
sh_alloc_p2m_pages(struct domain *d)
{
    struct page_info *pg;
    u32 i;
    unsigned int order = shadow_max_order(d);

    ASSERT(shadow_locked_by_me(d));
    
    if ( d->arch.paging.shadow.total_pages 
         < (shadow_min_acceptable_pages(d) + (1 << order)) )
        return 0; /* Not enough shadow memory: need to increase it first */
    
    shadow_prealloc(d, SH_type_p2m_table, 1);
    pg = mfn_to_page(shadow_alloc(d, SH_type_p2m_table, 0));
    d->arch.paging.shadow.p2m_pages += (1 << order);
    d->arch.paging.shadow.total_pages -= (1 << order);
    for (i = 0; i < (1U << order); i++)
    {
        /* Unlike shadow pages, mark p2m pages as owned by the domain.
         * Marking the domain as the owner would normally allow the guest to
         * create mappings of these pages, but these p2m pages will never be
         * in the domain's guest-physical address space, and so that is not
         * believed to be a concern.
         */
        page_set_owner(&pg[i], d);
        pg[i].count_info |= 1;
        page_list_add_tail(&pg[i], &d->arch.paging.shadow.p2m_freelist);
    }
    return 1;
}

// Returns 0 if no memory is available...
static struct page_info *
shadow_alloc_p2m_page(struct domain *d)
{
    struct page_info *pg;
    mfn_t mfn;
    void *p;
    
    shadow_lock(d);

    if ( page_list_empty(&d->arch.paging.shadow.p2m_freelist) &&
         !sh_alloc_p2m_pages(d) )
    {
        shadow_unlock(d);
        return NULL;
    }
    pg = page_list_remove_head(&d->arch.paging.shadow.p2m_freelist);

    shadow_unlock(d);

    mfn = page_to_mfn(pg);
    p = sh_map_domain_page(mfn);
    clear_page(p);
    sh_unmap_domain_page(p);

    return pg;
}

static void
shadow_free_p2m_page(struct domain *d, struct page_info *pg)
{
    ASSERT(page_get_owner(pg) == d);
    /* Should have just the one ref we gave it in alloc_p2m_page() */
    if ( (pg->count_info & PGC_count_mask) != 1 )
    {
        SHADOW_ERROR("Odd p2m page count c=%#lx t=%"PRtype_info"\n",
                     pg->count_info, pg->u.inuse.type_info);
    }
    pg->count_info &= ~PGC_count_mask;
    /* Free should not decrement domain's total allocation, since 
     * these pages were allocated without an owner. */
    page_set_owner(pg, NULL); 
    free_domheap_pages(pg, 0);
    d->arch.paging.shadow.p2m_pages--;
    perfc_decr(shadow_alloc_count);
}

#if CONFIG_PAGING_LEVELS == 3
static void p2m_install_entry_in_monitors(struct domain *d, 
                                          l3_pgentry_t *l3e) 
/* Special case, only used for external-mode domains on PAE hosts:
 * update the mapping of the p2m table.  Once again, this is trivial in
 * other paging modes (one top-level entry points to the top-level p2m,
 * no maintenance needed), but PAE makes life difficult by needing a
 * copy the eight l3es of the p2m table in eight l2h slots in the
 * monitor table.  This function makes fresh copies when a p2m l3e
 * changes. */
{
    l2_pgentry_t *ml2e;
    struct vcpu *v;
    unsigned int index;

    index = ((unsigned long)l3e & ~PAGE_MASK) / sizeof(l3_pgentry_t);
    ASSERT(index < MACHPHYS_MBYTES>>1);

    for_each_vcpu(d, v) 
    {
        if ( pagetable_get_pfn(v->arch.monitor_table) == 0 ) 
            continue;
        ASSERT(shadow_mode_external(v->domain));

        SHADOW_DEBUG(P2M, "d=%u v=%u index=%u mfn=%#lx\n",
                      d->domain_id, v->vcpu_id, index, l3e_get_pfn(*l3e));

        if ( v == current ) /* OK to use linear map of monitor_table */
            ml2e = __linear_l2_table + l2_linear_offset(RO_MPT_VIRT_START);
        else 
        {
            l3_pgentry_t *ml3e;
            ml3e = sh_map_domain_page(pagetable_get_mfn(v->arch.monitor_table));
            ASSERT(l3e_get_flags(ml3e[3]) & _PAGE_PRESENT);
            ml2e = sh_map_domain_page(_mfn(l3e_get_pfn(ml3e[3])));
            ml2e += l2_table_offset(RO_MPT_VIRT_START);
            sh_unmap_domain_page(ml3e);
        }
        ml2e[index] = l2e_from_pfn(l3e_get_pfn(*l3e), __PAGE_HYPERVISOR);
        if ( v != current )
            sh_unmap_domain_page(ml2e);
    }
}
#endif

/* Set the pool of shadow pages to the required number of pages.
 * Input will be rounded up to at least shadow_min_acceptable_pages(),
 * plus space for the p2m table.
 * Returns 0 for success, non-zero for failure. */
static unsigned int sh_set_allocation(struct domain *d, 
                                      unsigned int pages,
                                      int *preempted)
{
    struct page_info *sp;
    unsigned int lower_bound;
    unsigned int j, order = shadow_max_order(d);

    ASSERT(shadow_locked_by_me(d));
    
    /* Don't allocate less than the minimum acceptable, plus one page per
     * megabyte of RAM (for the p2m table) */
    lower_bound = shadow_min_acceptable_pages(d) + (d->tot_pages / 256);
    if ( pages > 0 && pages < lower_bound )
        pages = lower_bound;
    /* Round up to largest block size */
    pages = (pages + ((1<<SHADOW_MAX_ORDER)-1)) & ~((1<<SHADOW_MAX_ORDER)-1);

    SHADOW_PRINTK("current %i target %i\n", 
                   d->arch.paging.shadow.total_pages, pages);

    while ( d->arch.paging.shadow.total_pages != pages ) 
    {
        if ( d->arch.paging.shadow.total_pages < pages ) 
        {
            /* Need to allocate more memory from domheap */
            sp = (struct page_info *)
                alloc_domheap_pages(NULL, order, MEMF_node(domain_to_node(d)));
            if ( sp == NULL ) 
            { 
                SHADOW_PRINTK("failed to allocate shadow pages.\n");
                return -ENOMEM;
            }
            d->arch.paging.shadow.free_pages += 1 << order;
            d->arch.paging.shadow.total_pages += 1 << order;
            for ( j = 0; j < 1U << order; j++ )
            {
                sp[j].u.sh.type = 0;
                sp[j].u.sh.pinned = 0;
                sp[j].u.sh.count = 0;
                sp[j].tlbflush_timestamp = 0; /* Not in any TLB */
            }
            sp->v.free.order = order;
            page_list_add_tail(sp, &d->arch.paging.shadow.freelists[order]);
        } 
        else if ( d->arch.paging.shadow.total_pages > pages ) 
        {
            /* Need to return memory to domheap */
            _shadow_prealloc(d, order, 1);
            sp = page_list_remove_head(&d->arch.paging.shadow.freelists[order]);
            ASSERT(sp);
            /*
             * The pages were allocated anonymously, but the owner field
             * gets overwritten normally, so need to clear it here.
             */
            for ( j = 0; j < 1U << order; j++ )
                page_set_owner(&((struct page_info *)sp)[j], NULL);
            d->arch.paging.shadow.free_pages -= 1 << order;
            d->arch.paging.shadow.total_pages -= 1 << order;
            free_domheap_pages((struct page_info *)sp, order);
        }

        /* Check to see if we need to yield and try again */
        if ( preempted && hypercall_preempt_check() )
        {
            *preempted = 1;
            return 0;
        }
    }

    return 0;
}

/* Return the size of the shadow pool, rounded up to the nearest MB */
static unsigned int shadow_get_allocation(struct domain *d)
{
    unsigned int pg = d->arch.paging.shadow.total_pages;
    return ((pg >> (20 - PAGE_SHIFT))
            + ((pg & ((1 << (20 - PAGE_SHIFT)) - 1)) ? 1 : 0));
}

/**************************************************************************/
/* Hash table for storing the guest->shadow mappings.
 * The table itself is an array of pointers to shadows; the shadows are then 
 * threaded on a singly-linked list of shadows with the same hash value */

#define SHADOW_HASH_BUCKETS 251
/* Other possibly useful primes are 509, 1021, 2039, 4093, 8191, 16381 */

/* Hash function that takes a gfn or mfn, plus another byte of type info */
typedef u32 key_t;
static inline key_t sh_hash(unsigned long n, unsigned int t) 
{
    unsigned char *p = (unsigned char *)&n;
    key_t k = t;
    int i;
    for ( i = 0; i < sizeof(n) ; i++ ) k = (u32)p[i] + (k<<6) + (k<<16) - k;
    return k % SHADOW_HASH_BUCKETS;
}

#if SHADOW_AUDIT & (SHADOW_AUDIT_HASH|SHADOW_AUDIT_HASH_FULL)

/* Before we get to the mechanism, define a pair of audit functions
 * that sanity-check the contents of the hash table. */
static void sh_hash_audit_bucket(struct domain *d, int bucket)
/* Audit one bucket of the hash table */
{
    struct page_info *sp, *x;

    if ( !(SHADOW_AUDIT_ENABLE) )
        return;

    sp = d->arch.paging.shadow.hash_table[bucket];
    while ( sp )
    {
        /* Not a shadow? */
        BUG_ON( (sp->count_info & PGC_count_mask )!= 0 ) ;
        /* Bogus type? */
        BUG_ON( sp->u.sh.type == 0 );
        BUG_ON( sp->u.sh.type > SH_type_max_shadow );
        /* Wrong bucket? */
        BUG_ON( sh_hash(__backpointer(sp), sp->u.sh.type) != bucket );
        /* Duplicate entry? */
        for ( x = next_shadow(sp); x; x = next_shadow(x) )
            BUG_ON( x->v.sh.back == sp->v.sh.back &&
                    x->u.sh.type == sp->u.sh.type );
        /* Follow the backpointer to the guest pagetable */
        if ( sp->u.sh.type != SH_type_fl1_32_shadow
             && sp->u.sh.type != SH_type_fl1_pae_shadow
             && sp->u.sh.type != SH_type_fl1_64_shadow )
        {
            struct page_info *gpg = mfn_to_page(backpointer(sp));
            /* Bad shadow flags on guest page? */
            BUG_ON( !(gpg->shadow_flags & (1<<sp->u.sh.type)) );
            /* Bad type count on guest page? */
#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
            if ( sp->u.sh.type == SH_type_l1_32_shadow
                 || sp->u.sh.type == SH_type_l1_pae_shadow
                 || sp->u.sh.type == SH_type_l1_64_shadow )
            {
                if ( (gpg->u.inuse.type_info & PGT_type_mask) == PGT_writable_page
                     && (gpg->u.inuse.type_info & PGT_count_mask) != 0 )
                {
                    if ( !page_is_out_of_sync(gpg) )
                    {
                        SHADOW_ERROR("MFN %#"PRI_mfn" shadowed (by %#"PRI_mfn")"
                                     " and not OOS but has typecount %#lx\n",
                                     __backpointer(sp),
                                     mfn_x(page_to_mfn(sp)), 
                                     gpg->u.inuse.type_info);
                        BUG();
                    }
                }
            }
            else /* Not an l1 */
#endif
            if ( (gpg->u.inuse.type_info & PGT_type_mask) == PGT_writable_page 
                 && (gpg->u.inuse.type_info & PGT_count_mask) != 0 )
            {
                SHADOW_ERROR("MFN %#"PRI_mfn" shadowed (by %#"PRI_mfn")"
                             " but has typecount %#lx\n",
                             __backpointer(sp), mfn_x(page_to_mfn(sp)),
                             gpg->u.inuse.type_info);
                BUG();
            }
        }
        /* That entry was OK; on we go */
        sp = next_shadow(sp);
    }
}

#else
#define sh_hash_audit_bucket(_d, _b) do {} while(0)
#endif /* Hashtable bucket audit */


#if SHADOW_AUDIT & SHADOW_AUDIT_HASH_FULL

static void sh_hash_audit(struct domain *d)
/* Full audit: audit every bucket in the table */
{
    int i;

    if ( !(SHADOW_AUDIT_ENABLE) )
        return;

    for ( i = 0; i < SHADOW_HASH_BUCKETS; i++ ) 
    {
        sh_hash_audit_bucket(d, i);
    }
}

#else
#define sh_hash_audit(_d) do {} while(0)
#endif /* Hashtable bucket audit */

/* Allocate and initialise the table itself.  
 * Returns 0 for success, 1 for error. */
static int shadow_hash_alloc(struct domain *d)
{
    struct page_info **table;

    ASSERT(shadow_locked_by_me(d));
    ASSERT(!d->arch.paging.shadow.hash_table);

    table = xmalloc_array(struct page_info *, SHADOW_HASH_BUCKETS);
    if ( !table ) return 1;
    memset(table, 0, 
           SHADOW_HASH_BUCKETS * sizeof (struct page_info *));
    d->arch.paging.shadow.hash_table = table;
    return 0;
}

/* Tear down the hash table and return all memory to Xen.
 * This function does not care whether the table is populated. */
static void shadow_hash_teardown(struct domain *d)
{
    ASSERT(shadow_locked_by_me(d));
    ASSERT(d->arch.paging.shadow.hash_table);

    xfree(d->arch.paging.shadow.hash_table);
    d->arch.paging.shadow.hash_table = NULL;
}


mfn_t shadow_hash_lookup(struct vcpu *v, unsigned long n, unsigned int t)
/* Find an entry in the hash table.  Returns the MFN of the shadow,
 * or INVALID_MFN if it doesn't exist */
{
    struct domain *d = v->domain;
    struct page_info *sp, *prev;
    key_t key;

    ASSERT(shadow_locked_by_me(d));
    ASSERT(d->arch.paging.shadow.hash_table);
    ASSERT(t);

    sh_hash_audit(d);

    perfc_incr(shadow_hash_lookups);
    key = sh_hash(n, t);
    sh_hash_audit_bucket(d, key);

    sp = d->arch.paging.shadow.hash_table[key];
    prev = NULL;
    while(sp)
    {
        if ( __backpointer(sp) == n && sp->u.sh.type == t )
        {
            /* Pull-to-front if 'sp' isn't already the head item */
            if ( unlikely(sp != d->arch.paging.shadow.hash_table[key]) )
            {
                if ( unlikely(d->arch.paging.shadow.hash_walking != 0) )
                    /* Can't reorder: someone is walking the hash chains */
                    return page_to_mfn(sp);
                else 
                {
                    ASSERT(prev);
                    /* Delete sp from the list */
                    prev->next_shadow = sp->next_shadow;                    
                    /* Re-insert it at the head of the list */
                    set_next_shadow(sp, d->arch.paging.shadow.hash_table[key]);
                    d->arch.paging.shadow.hash_table[key] = sp;
                }
            }
            else
            {
                perfc_incr(shadow_hash_lookup_head);
            }
            return page_to_mfn(sp);
        }
        prev = sp;
        sp = next_shadow(sp);
    }

    perfc_incr(shadow_hash_lookup_miss);
    return _mfn(INVALID_MFN);
}

void shadow_hash_insert(struct vcpu *v, unsigned long n, unsigned int t, 
                        mfn_t smfn)
/* Put a mapping (n,t)->smfn into the hash table */
{
    struct domain *d = v->domain;
    struct page_info *sp;
    key_t key;
    
    ASSERT(shadow_locked_by_me(d));
    ASSERT(d->arch.paging.shadow.hash_table);
    ASSERT(t);

    sh_hash_audit(d);

    perfc_incr(shadow_hash_inserts);
    key = sh_hash(n, t);
    sh_hash_audit_bucket(d, key);
    
    /* Insert this shadow at the top of the bucket */
    sp = mfn_to_page(smfn);
    set_next_shadow(sp, d->arch.paging.shadow.hash_table[key]);
    d->arch.paging.shadow.hash_table[key] = sp;
    
    sh_hash_audit_bucket(d, key);
}

void shadow_hash_delete(struct vcpu *v, unsigned long n, unsigned int t, 
                        mfn_t smfn)
/* Excise the mapping (n,t)->smfn from the hash table */
{
    struct domain *d = v->domain;
    struct page_info *sp, *x;
    key_t key;

    ASSERT(shadow_locked_by_me(d));
    ASSERT(d->arch.paging.shadow.hash_table);
    ASSERT(t);

    sh_hash_audit(d);

    perfc_incr(shadow_hash_deletes);
    key = sh_hash(n, t);
    sh_hash_audit_bucket(d, key);
    
    sp = mfn_to_page(smfn);
    if ( d->arch.paging.shadow.hash_table[key] == sp ) 
        /* Easy case: we're deleting the head item. */
        d->arch.paging.shadow.hash_table[key] = next_shadow(sp);
    else 
    {
        /* Need to search for the one we want */
        x = d->arch.paging.shadow.hash_table[key];
        while ( 1 )
        {
            ASSERT(x); /* We can't have hit the end, since our target is
                        * still in the chain somehwere... */
            if ( next_shadow(x) == sp )
            {
                x->next_shadow = sp->next_shadow;
                break;
            }
            x = next_shadow(x);
        }
    }
    set_next_shadow(sp, NULL);

    sh_hash_audit_bucket(d, key);
}

typedef int (*hash_callback_t)(struct vcpu *v, mfn_t smfn, mfn_t other_mfn);

static void hash_foreach(struct vcpu *v, 
                         unsigned int callback_mask, 
                         const hash_callback_t callbacks[],
                         mfn_t callback_mfn)
/* Walk the hash table looking at the types of the entries and 
 * calling the appropriate callback function for each entry. 
 * The mask determines which shadow types we call back for, and the array
 * of callbacks tells us which function to call.
 * Any callback may return non-zero to let us skip the rest of the scan. 
 *
 * WARNING: Callbacks MUST NOT add or remove hash entries unless they 
 * then return non-zero to terminate the scan. */
{
    int i, done = 0;
    struct domain *d = v->domain;
    struct page_info *x;

    /* Say we're here, to stop hash-lookups reordering the chains */
    ASSERT(shadow_locked_by_me(d));
    ASSERT(d->arch.paging.shadow.hash_walking == 0);
    d->arch.paging.shadow.hash_walking = 1;

    for ( i = 0; i < SHADOW_HASH_BUCKETS; i++ ) 
    {
        /* WARNING: This is not safe against changes to the hash table.
         * The callback *must* return non-zero if it has inserted or
         * deleted anything from the hash (lookups are OK, though). */
        for ( x = d->arch.paging.shadow.hash_table[i]; x; x = next_shadow(x) )
        {
            if ( callback_mask & (1 << x->u.sh.type) )
            {
                ASSERT(x->u.sh.type <= 15);
                ASSERT(callbacks[x->u.sh.type] != NULL);
                done = callbacks[x->u.sh.type](v, page_to_mfn(x),
                                               callback_mfn);
                if ( done ) break;
            }
        }
        if ( done ) break; 
    }
    d->arch.paging.shadow.hash_walking = 0; 
}


/**************************************************************************/
/* Destroy a shadow page: simple dispatcher to call the per-type destructor
 * which will decrement refcounts appropriately and return memory to the 
 * free pool. */

void sh_destroy_shadow(struct vcpu *v, mfn_t smfn)
{
    struct page_info *sp = mfn_to_page(smfn);
    unsigned int t = sp->u.sh.type;


    SHADOW_PRINTK("smfn=%#lx\n", mfn_x(smfn));

    /* Double-check, if we can, that the shadowed page belongs to this
     * domain, (by following the back-pointer). */
    ASSERT(t == SH_type_fl1_32_shadow  ||  
           t == SH_type_fl1_pae_shadow ||  
           t == SH_type_fl1_64_shadow  || 
           t == SH_type_monitor_table  || 
           (is_pv_32on64_vcpu(v) && t == SH_type_l4_64_shadow) ||
           (page_get_owner(mfn_to_page(backpointer(sp)))
            == v->domain)); 

    /* The down-shifts here are so that the switch statement is on nice
     * small numbers that the compiler will enjoy */
    switch ( t )
    {
    case SH_type_l1_32_shadow:
    case SH_type_fl1_32_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l1_shadow, 2)(v, smfn);
        break;
    case SH_type_l2_32_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l2_shadow, 2)(v, smfn);
        break;

    case SH_type_l1_pae_shadow:
    case SH_type_fl1_pae_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l1_shadow, 3)(v, smfn);
        break;
    case SH_type_l2_pae_shadow:
    case SH_type_l2h_pae_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l2_shadow, 3)(v, smfn);
        break;

#if CONFIG_PAGING_LEVELS >= 4
    case SH_type_l1_64_shadow:
    case SH_type_fl1_64_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l1_shadow, 4)(v, smfn);
        break;
    case SH_type_l2h_64_shadow:
        ASSERT(is_pv_32on64_vcpu(v));
        /* Fall through... */
    case SH_type_l2_64_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l2_shadow, 4)(v, smfn);
        break;
    case SH_type_l3_64_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l3_shadow, 4)(v, smfn);
        break;
    case SH_type_l4_64_shadow:
        SHADOW_INTERNAL_NAME(sh_destroy_l4_shadow, 4)(v, smfn);
        break;
#endif
    default:
        SHADOW_ERROR("tried to destroy shadow of bad type %08lx\n",
                     (unsigned long)t);
        BUG();
    }    
}

static inline void trace_shadow_wrmap_bf(mfn_t gmfn)
{
    if ( tb_init_done )
    {
        /* Convert gmfn to gfn */
        unsigned long gfn = mfn_to_gfn(current->domain, gmfn);
        __trace_var(TRC_SHADOW_WRMAP_BF, 0/*!tsc*/, sizeof(gfn), (unsigned char*)&gfn);
    }
}

/**************************************************************************/
/* Remove all writeable mappings of a guest frame from the shadow tables 
 * Returns non-zero if we need to flush TLBs. 
 * level and fault_addr desribe how we found this to be a pagetable;
 * level==0 means we have some other reason for revoking write access.
 * If level==0 we are allowed to fail, returning -1. */

int sh_remove_write_access(struct vcpu *v, mfn_t gmfn, 
                           unsigned int level,
                           unsigned long fault_addr)
{
    /* Dispatch table for getting per-type functions */
    static const hash_callback_t callbacks[SH_type_unused] = {
        NULL, /* none    */
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 2), /* l1_32   */
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 2), /* fl1_32  */
        NULL, /* l2_32   */
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 3), /* l1_pae  */
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 3), /* fl1_pae */
        NULL, /* l2_pae  */
        NULL, /* l2h_pae */
#if CONFIG_PAGING_LEVELS >= 4
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 4), /* l1_64   */
        SHADOW_INTERNAL_NAME(sh_rm_write_access_from_l1, 4), /* fl1_64  */
#else
        NULL, /* l1_64   */
        NULL, /* fl1_64  */
#endif
        NULL, /* l2_64   */
        NULL, /* l2h_64  */
        NULL, /* l3_64   */
        NULL, /* l4_64   */
        NULL, /* p2m     */
        NULL  /* unused  */
    };

    static unsigned int callback_mask = 
          1 << SH_type_l1_32_shadow
        | 1 << SH_type_fl1_32_shadow
        | 1 << SH_type_l1_pae_shadow
        | 1 << SH_type_fl1_pae_shadow
        | 1 << SH_type_l1_64_shadow
        | 1 << SH_type_fl1_64_shadow
        ;
    struct page_info *pg = mfn_to_page(gmfn);

    ASSERT(shadow_locked_by_me(v->domain));

    /* Only remove writable mappings if we are doing shadow refcounts.
     * In guest refcounting, we trust Xen to already be restricting
     * all the writes to the guest page tables, so we do not need to
     * do more. */
    if ( !shadow_mode_refcounts(v->domain) )
        return 0;

    /* Early exit if it's already a pagetable, or otherwise not writeable */
    if ( (sh_mfn_is_a_page_table(gmfn)
#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
         /* Unless they've been allowed to go out of sync with their shadows */
           && !mfn_oos_may_write(gmfn)
#endif
         )
         || (pg->u.inuse.type_info & PGT_count_mask) == 0 )
        return 0;

    TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_WRMAP);

    perfc_incr(shadow_writeable);

    /* If this isn't a "normal" writeable page, the domain is trying to 
     * put pagetables in special memory of some kind.  We can't allow that. */
    if ( (pg->u.inuse.type_info & PGT_type_mask) != PGT_writable_page )
    {
        SHADOW_ERROR("can't remove write access to mfn %lx, type_info is %" 
                      PRtype_info "\n",
                      mfn_x(gmfn), mfn_to_page(gmfn)->u.inuse.type_info);
        domain_crash(v->domain);
    }

#if SHADOW_OPTIMIZATIONS & SHOPT_WRITABLE_HEURISTIC
    if ( v == current )
    {
        unsigned long gfn;
        /* Heuristic: there is likely to be only one writeable mapping,
         * and that mapping is likely to be in the current pagetable,
         * in the guest's linear map (on non-HIGHPTE linux and windows)*/

#define GUESS(_a, _h) do {                                              \
            if ( v->arch.paging.mode->shadow.guess_wrmap(v, (_a), gmfn) ) \
                perfc_incr(shadow_writeable_h_ ## _h);                  \
            if ( (pg->u.inuse.type_info & PGT_count_mask) == 0 )        \
            {                                                           \
                TRACE_SHADOW_PATH_FLAG(TRCE_SFLAG_WRMAP_GUESS_FOUND);   \
                return 1;                                               \
            }                                                           \
        } while (0)
        
        if ( v->arch.paging.mode->guest_levels == 2 )
        {
            if ( level == 1 )
                /* 32bit non-PAE w2k3: linear map at 0xC0000000 */
                GUESS(0xC0000000UL + (fault_addr >> 10), 1);

            /* Linux lowmem: first 896MB is mapped 1-to-1 above 0xC0000000 */
            if ((gfn = mfn_to_gfn(v->domain, gmfn)) < 0x38000 ) 
                GUESS(0xC0000000UL + (gfn << PAGE_SHIFT), 4);

            /* FreeBSD: Linear map at 0xBFC00000 */
            if ( level == 1 )
                GUESS(0xBFC00000UL 
                      + ((fault_addr & VADDR_MASK) >> 10), 6);
        }
        else if ( v->arch.paging.mode->guest_levels == 3 )
        {
            /* 32bit PAE w2k3: linear map at 0xC0000000 */
            switch ( level ) 
            {
            case 1: GUESS(0xC0000000UL + (fault_addr >> 9), 2); break;
            case 2: GUESS(0xC0600000UL + (fault_addr >> 18), 2); break;
            }

            /* Linux lowmem: first 896MB is mapped 1-to-1 above 0xC0000000 */
            if ((gfn = mfn_to_gfn(v->domain, gmfn)) < 0x38000 ) 
                GUESS(0xC0000000UL + (gfn << PAGE_SHIFT), 4);
            
            /* FreeBSD PAE: Linear map at 0xBF800000 */
            switch ( level )
            {
            case 1: GUESS(0xBF800000UL
                          + ((fault_addr & VADDR_MASK) >> 9), 6); break;
            case 2: GUESS(0xBFDFC000UL
                          + ((fault_addr & VADDR_MASK) >> 18), 6); break;
            }
        }
#if CONFIG_PAGING_LEVELS >= 4
        else if ( v->arch.paging.mode->guest_levels == 4 )
        {
            /* 64bit w2k3: linear map at 0xfffff68000000000 */
            switch ( level ) 
            {
            case 1: GUESS(0xfffff68000000000UL 
                          + ((fault_addr & VADDR_MASK) >> 9), 3); break;
            case 2: GUESS(0xfffff6fb40000000UL
                          + ((fault_addr & VADDR_MASK) >> 18), 3); break;
            case 3: GUESS(0xfffff6fb7da00000UL 
                          + ((fault_addr & VADDR_MASK) >> 27), 3); break;
            }

            /* 64bit Linux direct map at 0xffff880000000000; older kernels
             * had it at 0xffff810000000000, and older kernels yet had it
             * at 0x0000010000000000UL */
            gfn = mfn_to_gfn(v->domain, gmfn); 
            GUESS(0xffff880000000000UL + (gfn << PAGE_SHIFT), 4);
            GUESS(0xffff810000000000UL + (gfn << PAGE_SHIFT), 4);
            GUESS(0x0000010000000000UL + (gfn << PAGE_SHIFT), 4);

            /*
             * 64bit Solaris kernel page map at
             * kpm_vbase; 0xfffffe0000000000UL
             */
            GUESS(0xfffffe0000000000UL + (gfn << PAGE_SHIFT), 4);
 
             /* FreeBSD 64bit: linear map 0xffff800000000000 */
             switch ( level )
             {
             case 1: GUESS(0xffff800000000000
                           + ((fault_addr & VADDR_MASK) >> 9), 6); break;
             case 2: GUESS(0xffff804000000000UL
                           + ((fault_addr & VADDR_MASK) >> 18), 6); break;
             case 3: GUESS(0xffff804020000000UL
                           + ((fault_addr & VADDR_MASK) >> 27), 6); break;
             }
             /* FreeBSD 64bit: direct map at 0xffffff0000000000 */
             GUESS(0xffffff0000000000 + (gfn << PAGE_SHIFT), 6);
        }
#endif /* CONFIG_PAGING_LEVELS >= 4 */

#undef GUESS
    }

    if ( (pg->u.inuse.type_info & PGT_count_mask) == 0 )
        return 1;

    /* Second heuristic: on HIGHPTE linux, there are two particular PTEs
     * (entries in the fixmap) where linux maps its pagetables.  Since
     * we expect to hit them most of the time, we start the search for
     * the writeable mapping by looking at the same MFN where the last
     * brute-force search succeeded. */

    if ( v->arch.paging.shadow.last_writeable_pte_smfn != 0 )
    {
        unsigned long old_count = (pg->u.inuse.type_info & PGT_count_mask);
        mfn_t last_smfn = _mfn(v->arch.paging.shadow.last_writeable_pte_smfn);
        int shtype = mfn_to_page(last_smfn)->u.sh.type;

        if ( callbacks[shtype] ) 
            callbacks[shtype](v, last_smfn, gmfn);

        if ( (pg->u.inuse.type_info & PGT_count_mask) != old_count )
            perfc_incr(shadow_writeable_h_5);
    }

    if ( (pg->u.inuse.type_info & PGT_count_mask) == 0 )
        return 1;

#endif /* SHADOW_OPTIMIZATIONS & SHOPT_WRITABLE_HEURISTIC */
    
    /* Brute-force search of all the shadows, by walking the hash */
    trace_shadow_wrmap_bf(gmfn);
    if ( level == 0 )
        perfc_incr(shadow_writeable_bf_1);
    else
        perfc_incr(shadow_writeable_bf);
    hash_foreach(v, callback_mask, callbacks, gmfn);

    /* If that didn't catch the mapping, then there's some non-pagetable
     * mapping -- ioreq page, grant mapping, &c. */
    if ( (mfn_to_page(gmfn)->u.inuse.type_info & PGT_count_mask) != 0 )
    {
        if ( level == 0 )
            return -1;

        SHADOW_ERROR("can't remove write access to mfn %lx: guest has "
                      "%lu special-use mappings of it\n", mfn_x(gmfn),
                      (mfn_to_page(gmfn)->u.inuse.type_info&PGT_count_mask));
        domain_crash(v->domain);
    }
    
    /* We killed at least one writeable mapping, so must flush TLBs. */
    return 1;
}

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
int sh_remove_write_access_from_sl1p(struct vcpu *v, mfn_t gmfn,
                                     mfn_t smfn, unsigned long off)
{
    struct page_info *sp = mfn_to_page(smfn);
    
    ASSERT(mfn_valid(smfn));
    ASSERT(mfn_valid(gmfn));
    
    if ( sp->u.sh.type == SH_type_l1_32_shadow
         || sp->u.sh.type == SH_type_fl1_32_shadow )
    {
        return SHADOW_INTERNAL_NAME(sh_rm_write_access_from_sl1p,2)
            (v, gmfn, smfn, off);
    }
#if CONFIG_PAGING_LEVELS >= 3
    else if ( sp->u.sh.type == SH_type_l1_pae_shadow
              || sp->u.sh.type == SH_type_fl1_pae_shadow )
        return SHADOW_INTERNAL_NAME(sh_rm_write_access_from_sl1p,3)
            (v, gmfn, smfn, off);
#if CONFIG_PAGING_LEVELS >= 4
    else if ( sp->u.sh.type == SH_type_l1_64_shadow
              || sp->u.sh.type == SH_type_fl1_64_shadow )
        return SHADOW_INTERNAL_NAME(sh_rm_write_access_from_sl1p,4)
            (v, gmfn, smfn, off);
#endif
#endif

    return 0;
}
#endif 

/**************************************************************************/
/* Remove all mappings of a guest frame from the shadow tables.
 * Returns non-zero if we need to flush TLBs. */

int sh_remove_all_mappings(struct vcpu *v, mfn_t gmfn)
{
    struct page_info *page = mfn_to_page(gmfn);
    int expected_count, do_locking;

    /* Dispatch table for getting per-type functions */
    static const hash_callback_t callbacks[SH_type_unused] = {
        NULL, /* none    */
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 2), /* l1_32   */
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 2), /* fl1_32  */
        NULL, /* l2_32   */
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 3), /* l1_pae  */
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 3), /* fl1_pae */
        NULL, /* l2_pae  */
        NULL, /* l2h_pae */
#if CONFIG_PAGING_LEVELS >= 4
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 4), /* l1_64   */
        SHADOW_INTERNAL_NAME(sh_rm_mappings_from_l1, 4), /* fl1_64  */
#else
        NULL, /* l1_64   */
        NULL, /* fl1_64  */
#endif
        NULL, /* l2_64   */
        NULL, /* l2h_64  */
        NULL, /* l3_64   */
        NULL, /* l4_64   */
        NULL, /* p2m     */
        NULL  /* unused  */
    };

    static unsigned int callback_mask = 
          1 << SH_type_l1_32_shadow
        | 1 << SH_type_fl1_32_shadow
        | 1 << SH_type_l1_pae_shadow
        | 1 << SH_type_fl1_pae_shadow
        | 1 << SH_type_l1_64_shadow
        | 1 << SH_type_fl1_64_shadow
        ;

    perfc_incr(shadow_mappings);
    if ( (page->count_info & PGC_count_mask) == 0 )
        return 0;

    /* Although this is an externally visible function, we do not know
     * whether the shadow lock will be held when it is called (since it
     * can be called via put_page_type when we clear a shadow l1e).
     * If the lock isn't held, take it for the duration of the call. */
    do_locking = !shadow_locked_by_me(v->domain);
    if ( do_locking ) shadow_lock(v->domain);

    /* XXX TODO: 
     * Heuristics for finding the (probably) single mapping of this gmfn */
    
    /* Brute-force search of all the shadows, by walking the hash */
    perfc_incr(shadow_mappings_bf);
    hash_foreach(v, callback_mask, callbacks, gmfn);

    /* If that didn't catch the mapping, something is very wrong */
    expected_count = (page->count_info & PGC_allocated) ? 1 : 0;
    if ( (page->count_info & PGC_count_mask) != expected_count )
    {
        /* Don't complain if we're in HVM and there are some extra mappings: 
         * The qemu helper process has an untyped mapping of this dom's RAM 
         * and the HVM restore program takes another. */
        if ( !(shadow_mode_external(v->domain)
               && (page->count_info & PGC_count_mask) <= 3
               && (page->u.inuse.type_info & PGT_count_mask) == 0) )
        {
            SHADOW_ERROR("can't find all mappings of mfn %lx: "
                          "c=%08lx t=%08lx\n", mfn_x(gmfn), 
                          page->count_info, page->u.inuse.type_info);
        }
    }

    if ( do_locking ) shadow_unlock(v->domain);

    /* We killed at least one mapping, so must flush TLBs. */
    return 1;
}


/**************************************************************************/
/* Remove all shadows of a guest frame from the shadow tables */

static int sh_remove_shadow_via_pointer(struct vcpu *v, mfn_t smfn)
/* Follow this shadow's up-pointer, if it has one, and remove the reference
 * found there.  Returns 1 if that was the only reference to this shadow */
{
    struct page_info *sp = mfn_to_page(smfn);
    mfn_t pmfn;
    void *vaddr;
    int rc;

    ASSERT(sp->u.sh.type > 0);
    ASSERT(sp->u.sh.type < SH_type_max_shadow);
    ASSERT(sp->u.sh.type != SH_type_l2_32_shadow);
    ASSERT(sp->u.sh.type != SH_type_l2_pae_shadow);
    ASSERT(sp->u.sh.type != SH_type_l2h_pae_shadow);
    ASSERT(sp->u.sh.type != SH_type_l4_64_shadow);
    
    if (sp->up == 0) return 0;
    pmfn = _mfn(sp->up >> PAGE_SHIFT);
    ASSERT(mfn_valid(pmfn));
    vaddr = sh_map_domain_page(pmfn);
    ASSERT(vaddr);
    vaddr += sp->up & (PAGE_SIZE-1);
    ASSERT(l1e_get_pfn(*(l1_pgentry_t *)vaddr) == mfn_x(smfn));
    
    /* Is this the only reference to this shadow? */
    rc = (sp->u.sh.count == 1) ? 1 : 0;

    /* Blank the offending entry */
    switch (sp->u.sh.type)
    {
    case SH_type_l1_32_shadow:
    case SH_type_l2_32_shadow:
        SHADOW_INTERNAL_NAME(sh_clear_shadow_entry, 2)(v, vaddr, pmfn);
        break;
    case SH_type_l1_pae_shadow:
    case SH_type_l2_pae_shadow:
    case SH_type_l2h_pae_shadow:
        SHADOW_INTERNAL_NAME(sh_clear_shadow_entry, 3)(v, vaddr, pmfn);
        break;
#if CONFIG_PAGING_LEVELS >= 4
    case SH_type_l1_64_shadow:
    case SH_type_l2_64_shadow:
    case SH_type_l2h_64_shadow:
    case SH_type_l3_64_shadow:
    case SH_type_l4_64_shadow:
        SHADOW_INTERNAL_NAME(sh_clear_shadow_entry, 4)(v, vaddr, pmfn);
        break;
#endif
    default: BUG(); /* Some wierd unknown shadow type */
    }
    
    sh_unmap_domain_page(vaddr);
    if ( rc )
        perfc_incr(shadow_up_pointer);
    else
        perfc_incr(shadow_unshadow_bf);

    return rc;
}

void sh_remove_shadows(struct vcpu *v, mfn_t gmfn, int fast, int all)
/* Remove the shadows of this guest page.  
 * If fast != 0, just try the quick heuristic, which will remove 
 * at most one reference to each shadow of the page.  Otherwise, walk
 * all the shadow tables looking for refs to shadows of this gmfn.
 * If all != 0, kill the domain if we can't find all the shadows.
 * (all != 0 implies fast == 0)
 */
{
    struct page_info *pg = mfn_to_page(gmfn);
    mfn_t smfn;
    int do_locking;
    unsigned char t;
    
    /* Dispatch table for getting per-type functions: each level must
     * be called with the function to remove a lower-level shadow. */
    static const hash_callback_t callbacks[SH_type_unused] = {
        NULL, /* none    */
        NULL, /* l1_32   */
        NULL, /* fl1_32  */
        SHADOW_INTERNAL_NAME(sh_remove_l1_shadow, 2), /* l2_32   */
        NULL, /* l1_pae  */
        NULL, /* fl1_pae */
        SHADOW_INTERNAL_NAME(sh_remove_l1_shadow, 3), /* l2_pae  */
        SHADOW_INTERNAL_NAME(sh_remove_l1_shadow, 3), /* l2h_pae */
        NULL, /* l1_64   */
        NULL, /* fl1_64  */
#if CONFIG_PAGING_LEVELS >= 4
        SHADOW_INTERNAL_NAME(sh_remove_l1_shadow, 4), /* l2_64   */
        SHADOW_INTERNAL_NAME(sh_remove_l1_shadow, 4), /* l2h_64  */
        SHADOW_INTERNAL_NAME(sh_remove_l2_shadow, 4), /* l3_64   */
        SHADOW_INTERNAL_NAME(sh_remove_l3_shadow, 4), /* l4_64   */
#else
        NULL, /* l2_64   */
        NULL, /* l2h_64  */
        NULL, /* l3_64   */
        NULL, /* l4_64   */
#endif
        NULL, /* p2m     */
        NULL  /* unused  */
    };

    /* Another lookup table, for choosing which mask to use */
    static unsigned int masks[SH_type_unused] = {
        0, /* none    */
        1 << SH_type_l2_32_shadow, /* l1_32   */
        0, /* fl1_32  */
        0, /* l2_32   */
        ((1 << SH_type_l2h_pae_shadow)
         | (1 << SH_type_l2_pae_shadow)), /* l1_pae  */
        0, /* fl1_pae */
        0, /* l2_pae  */
        0, /* l2h_pae  */
        ((1 << SH_type_l2h_64_shadow)
         | (1 << SH_type_l2_64_shadow)),  /* l1_64   */
        0, /* fl1_64  */
        1 << SH_type_l3_64_shadow, /* l2_64   */
        1 << SH_type_l3_64_shadow, /* l2h_64  */
        1 << SH_type_l4_64_shadow, /* l3_64   */
        0, /* l4_64   */
        0, /* p2m     */
        0  /* unused  */
    };

    ASSERT(!(all && fast));
    ASSERT(mfn_valid(gmfn));

    /* Although this is an externally visible function, we do not know
     * whether the shadow lock will be held when it is called (since it
     * can be called via put_page_type when we clear a shadow l1e).
     * If the lock isn't held, take it for the duration of the call. */
    do_locking = !shadow_locked_by_me(v->domain);
    if ( do_locking ) shadow_lock(v->domain);

    SHADOW_PRINTK("d=%d, v=%d, gmfn=%05lx\n",
                   v->domain->domain_id, v->vcpu_id, mfn_x(gmfn));

    /* Bail out now if the page is not shadowed */
    if ( (pg->count_info & PGC_page_table) == 0 )
    {
        if ( do_locking ) shadow_unlock(v->domain);
        return;
    }

    /* Search for this shadow in all appropriate shadows */
    perfc_incr(shadow_unshadow);

    /* Lower-level shadows need to be excised from upper-level shadows.
     * This call to hash_foreach() looks dangerous but is in fact OK: each
     * call will remove at most one shadow, and terminate immediately when
     * it does remove it, so we never walk the hash after doing a deletion.  */
#define DO_UNSHADOW(_type) do {                                         \
    t = (_type);                                                        \
    if( !(pg->count_info & PGC_page_table)                              \
        || !(pg->shadow_flags & (1 << t)) )                             \
        break;                                                          \
    smfn = shadow_hash_lookup(v, mfn_x(gmfn), t);                       \
    if ( unlikely(!mfn_valid(smfn)) )                                   \
    {                                                                   \
        SHADOW_ERROR(": gmfn %#lx has flags 0x%"PRIx32                  \
                     " but no type-0x%"PRIx32" shadow\n",               \
                     mfn_x(gmfn), (uint32_t)pg->shadow_flags, t);       \
        break;                                                          \
    }                                                                   \
    if ( sh_type_is_pinnable(v, t) )                                    \
        sh_unpin(v, smfn);                                              \
    else                                                                \
        sh_remove_shadow_via_pointer(v, smfn);                          \
    if( !fast                                                           \
        && (pg->count_info & PGC_page_table)                            \
        && (pg->shadow_flags & (1 << t)) )                              \
        hash_foreach(v, masks[t], callbacks, smfn);                     \
} while (0)

    DO_UNSHADOW(SH_type_l2_32_shadow);
    DO_UNSHADOW(SH_type_l1_32_shadow);
    DO_UNSHADOW(SH_type_l2h_pae_shadow);
    DO_UNSHADOW(SH_type_l2_pae_shadow);
    DO_UNSHADOW(SH_type_l1_pae_shadow);
#if CONFIG_PAGING_LEVELS >= 4
    DO_UNSHADOW(SH_type_l4_64_shadow);
    DO_UNSHADOW(SH_type_l3_64_shadow);
    DO_UNSHADOW(SH_type_l2h_64_shadow);
    DO_UNSHADOW(SH_type_l2_64_shadow);
    DO_UNSHADOW(SH_type_l1_64_shadow);
#endif

#undef DO_UNSHADOW

    /* If that didn't catch the shadows, something is wrong */
    if ( !fast && all && (pg->count_info & PGC_page_table) )
    {
        SHADOW_ERROR("can't find all shadows of mfn %05lx "
                     "(shadow_flags=%08x)\n",
                      mfn_x(gmfn), pg->shadow_flags);
        domain_crash(v->domain);
    }

    /* Need to flush TLBs now, so that linear maps are safe next time we 
     * take a fault. */
    flush_tlb_mask(&v->domain->domain_dirty_cpumask);

    if ( do_locking ) shadow_unlock(v->domain);
}

static void
sh_remove_all_shadows_and_parents(struct vcpu *v, mfn_t gmfn)
/* Even harsher: this is a HVM page that we thing is no longer a pagetable.
 * Unshadow it, and recursively unshadow pages that reference it. */
{
    sh_remove_shadows(v, gmfn, 0, 1);
    /* XXX TODO:
     * Rework this hashtable walker to return a linked-list of all 
     * the shadows it modified, then do breadth-first recursion 
     * to find the way up to higher-level tables and unshadow them too. 
     *
     * The current code (just tearing down each page's shadows as we
     * detect that it is not a pagetable) is correct, but very slow. 
     * It means extra emulated writes and slows down removal of mappings. */
}

/**************************************************************************/

static void sh_update_paging_modes(struct vcpu *v)
{
    struct domain *d = v->domain;
    const struct paging_mode *old_mode = v->arch.paging.mode;

    ASSERT(shadow_locked_by_me(d));

#if (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB) 
    /* Make sure this vcpu has a virtual TLB array allocated */
    if ( unlikely(!v->arch.paging.vtlb) )
    {
        v->arch.paging.vtlb = xmalloc_array(struct shadow_vtlb, VTLB_ENTRIES);
        if ( unlikely(!v->arch.paging.vtlb) )
        {
            SHADOW_ERROR("Could not allocate vTLB space for dom %u vcpu %u\n",
                         d->domain_id, v->vcpu_id);
            domain_crash(v->domain);
            return;
        }
        memset(v->arch.paging.vtlb, 0, 
               VTLB_ENTRIES * sizeof (struct shadow_vtlb));
        spin_lock_init(&v->arch.paging.vtlb_lock);
    }
#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB) */

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
    if ( mfn_x(v->arch.paging.shadow.oos_snapshot[0]) == INVALID_MFN )
    {
        int i;
        for(i = 0; i < SHADOW_OOS_PAGES; i++)
        {
            shadow_prealloc(d, SH_type_oos_snapshot, 1);
            v->arch.paging.shadow.oos_snapshot[i] =
                shadow_alloc(d, SH_type_oos_snapshot, 0);
        }
    }
#endif /* OOS */

    // Valid transitions handled by this function:
    // - For PV guests:
    //     - after a shadow mode has been changed
    // - For HVM guests:
    //     - after a shadow mode has been changed
    //     - changes in CR0.PG, CR4.PAE, CR4.PSE, or CR4.PGE
    //

    // First, tear down any old shadow tables held by this vcpu.
    //
    if ( v->arch.paging.mode )
        v->arch.paging.mode->shadow.detach_old_tables(v);

    if ( !is_hvm_domain(d) )
    {
        ///
        /// PV guest
        ///
#if CONFIG_PAGING_LEVELS == 4
        v->arch.paging.mode = &SHADOW_INTERNAL_NAME(sh_paging_mode, 4);
#else /* CONFIG_PAGING_LEVELS == 3 */
        v->arch.paging.mode = &SHADOW_INTERNAL_NAME(sh_paging_mode, 3);
#endif
    }
    else
    {
        ///
        /// HVM guest
        ///
        ASSERT(shadow_mode_translate(d));
        ASSERT(shadow_mode_external(d));

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
        /* Need to resync all our pages now, because if a page goes out
         * of sync with paging enabled and is resynced with paging
         * disabled, the resync will go wrong. */
        shadow_resync_all(v, 0);
#endif /* OOS */

        if ( !hvm_paging_enabled(v) )
        {
            /* When the guest has CR0.PG clear, we provide a 32-bit, non-PAE
             * pagetable for it, mapping 4 GB one-to-one using a single l2
             * page of 1024 superpage mappings */
            v->arch.guest_table = d->arch.paging.shadow.unpaged_pagetable;
            v->arch.paging.mode = &SHADOW_INTERNAL_NAME(sh_paging_mode, 2);
        }
        else
        {
#ifdef __x86_64__
            if ( hvm_long_mode_enabled(v) )
            {
                // long mode guest...
                v->arch.paging.mode =
                    &SHADOW_INTERNAL_NAME(sh_paging_mode, 4);
            }
            else
#endif
                if ( hvm_pae_enabled(v) )
                {
                    // 32-bit PAE mode guest...
                    v->arch.paging.mode =
                        &SHADOW_INTERNAL_NAME(sh_paging_mode, 3);
                }
                else
                {
                    // 32-bit 2 level guest...
                    v->arch.paging.mode =
                        &SHADOW_INTERNAL_NAME(sh_paging_mode, 2);
                }
        }

        if ( pagetable_is_null(v->arch.monitor_table) )
        {
            mfn_t mmfn = v->arch.paging.mode->shadow.make_monitor_table(v);
            v->arch.monitor_table = pagetable_from_mfn(mmfn);
            make_cr3(v, mfn_x(mmfn));
            hvm_update_host_cr3(v);
        }

        if ( v->arch.paging.mode != old_mode )
        {
            SHADOW_PRINTK("new paging mode: d=%u v=%u pe=%d gl=%u "
                          "(was g=%u s=%u)\n",
                          d->domain_id, v->vcpu_id,
                          is_hvm_domain(d) ? hvm_paging_enabled(v) : 1,
                          v->arch.paging.mode->guest_levels,
                          v->arch.paging.mode->shadow.shadow_levels,
                          old_mode ? old_mode->guest_levels : 0,
                          old_mode ? old_mode->shadow.shadow_levels : 0);
            if ( old_mode &&
                 (v->arch.paging.mode->shadow.shadow_levels !=
                  old_mode->shadow.shadow_levels) )
            {
                /* Need to make a new monitor table for the new mode */
                mfn_t new_mfn, old_mfn;

                if ( v != current && vcpu_runnable(v) ) 
                {
                    SHADOW_ERROR("Some third party (d=%u v=%u) is changing "
                                 "this HVM vcpu's (d=%u v=%u) paging mode "
                                 "while it is running.\n",
                                 current->domain->domain_id, current->vcpu_id,
                                 v->domain->domain_id, v->vcpu_id);
                    /* It's not safe to do that because we can't change
                     * the host CR3 for a running domain */
                    domain_crash(v->domain);
                    return;
                }

                old_mfn = pagetable_get_mfn(v->arch.monitor_table);
                v->arch.monitor_table = pagetable_null();
                new_mfn = v->arch.paging.mode->shadow.make_monitor_table(v);
                v->arch.monitor_table = pagetable_from_mfn(new_mfn);
                SHADOW_PRINTK("new monitor table %"PRI_mfn "\n",
                               mfn_x(new_mfn));

                /* Don't be running on the old monitor table when we 
                 * pull it down!  Switch CR3, and warn the HVM code that
                 * its host cr3 has changed. */
                make_cr3(v, mfn_x(new_mfn));
                if ( v == current )
                    write_ptbase(v);
                hvm_update_host_cr3(v);
                old_mode->shadow.destroy_monitor_table(v, old_mfn);
            }
        }

        // XXX -- Need to deal with changes in CR4.PSE and CR4.PGE.
        //        These are HARD: think about the case where two CPU's have
        //        different values for CR4.PSE and CR4.PGE at the same time.
        //        This *does* happen, at least for CR4.PGE...
    }

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
    /* We need to check that all the vcpus have paging enabled to
     * unsync PTs. */
    if ( is_hvm_domain(d) && !d->arch.paging.shadow.oos_off )
    {
        int pe = 1;
        struct vcpu *vptr;

        for_each_vcpu(d, vptr)
        {
            if ( !hvm_paging_enabled(vptr) )
            {
                pe = 0;
                break;
            }
        }

        d->arch.paging.shadow.oos_active = pe;
    }
#endif /* OOS */

    v->arch.paging.mode->update_cr3(v, 0);
}

void shadow_update_paging_modes(struct vcpu *v)
{
    shadow_lock(v->domain);
    sh_update_paging_modes(v);
    shadow_unlock(v->domain);
}

/**************************************************************************/
/* Turning on and off shadow features */

static void sh_new_mode(struct domain *d, u32 new_mode)
/* Inform all the vcpus that the shadow mode has been changed */
{
    struct vcpu *v;

    ASSERT(shadow_locked_by_me(d));
    ASSERT(d != current->domain);
    d->arch.paging.mode = new_mode;
    for_each_vcpu(d, v)
        sh_update_paging_modes(v);
}

int shadow_enable(struct domain *d, u32 mode)
/* Turn on "permanent" shadow features: external, translate, refcount.
 * Can only be called once on a domain, and these features cannot be
 * disabled. 
 * Returns 0 for success, -errno for failure. */
{    
    unsigned int old_pages;
    struct page_info *pg = NULL;
    uint32_t *e;
    int i, rv = 0;

    mode |= PG_SH_enable;

    domain_pause(d);

    /* Sanity check the arguments */
    if ( (d == current->domain) ||
         shadow_mode_enabled(d) ||
         ((mode & PG_translate) && !(mode & PG_refcounts)) ||
         ((mode & PG_external) && !(mode & PG_translate)) )
    {
        rv = -EINVAL;
        goto out_unlocked;
    }

    /* Init the shadow memory allocation if the user hasn't done so */
    old_pages = d->arch.paging.shadow.total_pages;
    if ( old_pages == 0 )
    {
        unsigned int r;
        shadow_lock(d);                
        r = sh_set_allocation(d, 1024, NULL); /* Use at least 4MB */
        if ( r != 0 )
        {
            sh_set_allocation(d, 0, NULL);
            rv = -ENOMEM;
            goto out_locked;
        }        
        shadow_unlock(d);
    }

    /* Init the P2M table.  Must be done before we take the shadow lock 
     * to avoid possible deadlock. */
    if ( mode & PG_translate )
    {
        rv = p2m_alloc_table(d, shadow_alloc_p2m_page, shadow_free_p2m_page);
        if (rv != 0)
            goto out_unlocked;
    }

    /* HVM domains need an extra pagetable for vcpus that think they
     * have paging disabled */
    if ( is_hvm_domain(d) )
    {
        /* Get a single page from the shadow pool.  Take it via the 
         * P2M interface to make freeing it simpler afterwards. */
        pg = shadow_alloc_p2m_page(d);
        if ( pg == NULL )
        {
            rv = -ENOMEM;
            goto out_unlocked;
        }
        /* Fill it with 32-bit, non-PAE superpage entries, each mapping 4MB
         * of virtual address space onto the same physical address range */ 
        e = __map_domain_page(pg);
        for ( i = 0; i < PAGE_SIZE / sizeof(*e); i++ )
            e[i] = ((0x400000U * i)
                    | _PAGE_PRESENT | _PAGE_RW | _PAGE_USER 
                    | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_PSE);
        sh_unmap_domain_page(e);
        pg->u.inuse.type_info = PGT_l2_page_table | 1 | PGT_validated;
    }

    shadow_lock(d);

    /* Sanity check again with the lock held */
    if ( shadow_mode_enabled(d) )
    {
        rv = -EINVAL;
        goto out_locked;
    }

    /* Init the hash table */
    if ( shadow_hash_alloc(d) != 0 )
    {
        rv = -ENOMEM;
        goto out_locked;
    }

#if (SHADOW_OPTIMIZATIONS & SHOPT_LINUX_L3_TOPLEVEL) 
    /* We assume we're dealing with an older 64bit linux guest until we 
     * see the guest use more than one l4 per vcpu. */
    d->arch.paging.shadow.opt_flags = SHOPT_LINUX_L3_TOPLEVEL;
#endif

    /* Record the 1-to-1 pagetable we just made */
    if ( is_hvm_domain(d) )
        d->arch.paging.shadow.unpaged_pagetable = pagetable_from_page(pg);

    /* Update the bits */
    sh_new_mode(d, mode);

 out_locked:
    shadow_unlock(d);
 out_unlocked:
    if ( rv != 0 && !pagetable_is_null(d->arch.phys_table) )
        p2m_teardown(d);
    if ( rv != 0 && pg != NULL )
        shadow_free_p2m_page(d, pg);
    domain_unpause(d);
    return rv;
}

void shadow_teardown(struct domain *d)
/* Destroy the shadow pagetables of this domain and free its shadow memory.
 * Should only be called for dying domains. */
{
    struct vcpu *v;
    mfn_t mfn;
    struct page_info *pg;

    ASSERT(d->is_dying);
    ASSERT(d != current->domain);

    if ( !shadow_locked_by_me(d) )
        shadow_lock(d); /* Keep various asserts happy */

    if ( shadow_mode_enabled(d) )
    {
        /* Release the shadow and monitor tables held by each vcpu */
        for_each_vcpu(d, v)
        {
            if ( v->arch.paging.mode )
            {
                v->arch.paging.mode->shadow.detach_old_tables(v);
                if ( shadow_mode_external(d) )
                {
                    mfn = pagetable_get_mfn(v->arch.monitor_table);
                    if ( mfn_valid(mfn) && (mfn_x(mfn) != 0) )
                        v->arch.paging.mode->shadow.destroy_monitor_table(v, mfn);
                    v->arch.monitor_table = pagetable_null();
                }
            }
        }
    }

#if (SHADOW_OPTIMIZATIONS & (SHOPT_VIRTUAL_TLB|SHOPT_OUT_OF_SYNC))
    /* Free the virtual-TLB array attached to each vcpu */
    for_each_vcpu(d, v)
    {
#if (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB)
        if ( v->arch.paging.vtlb )
        {
            xfree(v->arch.paging.vtlb);
            v->arch.paging.vtlb = NULL;
        }
#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB) */

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
        {
            int i;
            mfn_t *oos_snapshot = v->arch.paging.shadow.oos_snapshot;
            for(i = 0; i < SHADOW_OOS_PAGES; i++)
                if ( mfn_valid(oos_snapshot[i]) )
                    shadow_free(d, oos_snapshot[i]);
        }
#endif /* OOS */
    }
#endif /* (SHADOW_OPTIMIZATIONS & (SHOPT_VIRTUAL_TLB|SHOPT_OUT_OF_SYNC)) */

    while ( (pg = page_list_remove_head(&d->arch.paging.shadow.p2m_freelist)) )
        shadow_free_p2m_page(d, pg);

    if ( d->arch.paging.shadow.total_pages != 0 )
    {
        SHADOW_PRINTK("teardown of domain %u starts."
                       "  Shadow pages total = %u, free = %u, p2m=%u\n",
                       d->domain_id,
                       d->arch.paging.shadow.total_pages, 
                       d->arch.paging.shadow.free_pages, 
                       d->arch.paging.shadow.p2m_pages);
        /* Destroy all the shadows and release memory to domheap */
        sh_set_allocation(d, 0, NULL);
        /* Release the hash table back to xenheap */
        if (d->arch.paging.shadow.hash_table) 
            shadow_hash_teardown(d);
        /* Should not have any more memory held */
        SHADOW_PRINTK("teardown done."
                       "  Shadow pages total = %u, free = %u, p2m=%u\n",
                       d->arch.paging.shadow.total_pages, 
                       d->arch.paging.shadow.free_pages, 
                       d->arch.paging.shadow.p2m_pages);
        ASSERT(d->arch.paging.shadow.total_pages == 0);
    }

    /* Free the non-paged-vcpus pagetable; must happen after we've 
     * destroyed any shadows of it or sh_destroy_shadow will get confused. */
    if ( !pagetable_is_null(d->arch.paging.shadow.unpaged_pagetable) )
    {
        for_each_vcpu(d, v)
        {
            ASSERT(is_hvm_vcpu(v));
            if ( !hvm_paging_enabled(v) )
                v->arch.guest_table = pagetable_null();
        }
        shadow_free_p2m_page(d, 
            pagetable_get_page(d->arch.paging.shadow.unpaged_pagetable));
        d->arch.paging.shadow.unpaged_pagetable = pagetable_null();
    }

    /* We leave the "permanent" shadow modes enabled, but clear the
     * log-dirty mode bit.  We don't want any more mark_dirty()
     * calls now that we've torn down the bitmap */
    d->arch.paging.mode &= ~PG_log_dirty;

    if (d->arch.hvm_domain.dirty_vram) {
        xfree(d->arch.hvm_domain.dirty_vram->sl1ma);
        xfree(d->arch.hvm_domain.dirty_vram->dirty_bitmap);
        xfree(d->arch.hvm_domain.dirty_vram);
        d->arch.hvm_domain.dirty_vram = NULL;
    }

    shadow_unlock(d);
}

void shadow_final_teardown(struct domain *d)
/* Called by arch_domain_destroy(), when it's safe to pull down the p2m map. */
{
    SHADOW_PRINTK("dom %u final teardown starts."
                   "  Shadow pages total = %u, free = %u, p2m=%u\n",
                   d->domain_id,
                   d->arch.paging.shadow.total_pages, 
                   d->arch.paging.shadow.free_pages, 
                   d->arch.paging.shadow.p2m_pages);

    /* Double-check that the domain didn't have any shadow memory.  
     * It is possible for a domain that never got domain_kill()ed
     * to get here with its shadow allocation intact. */
    if ( d->arch.paging.shadow.total_pages != 0 )
        shadow_teardown(d);

    /* It is now safe to pull down the p2m map. */
    p2m_teardown(d);

    SHADOW_PRINTK("dom %u final teardown done."
                   "  Shadow pages total = %u, free = %u, p2m=%u\n",
                   d->domain_id,
                   d->arch.paging.shadow.total_pages, 
                   d->arch.paging.shadow.free_pages, 
                   d->arch.paging.shadow.p2m_pages);
}

static int shadow_one_bit_enable(struct domain *d, u32 mode)
/* Turn on a single shadow mode feature */
{
    ASSERT(shadow_locked_by_me(d));

    /* Sanity check the call */
    if ( d == current->domain || (d->arch.paging.mode & mode) == mode )
    {
        return -EINVAL;
    }

    mode |= PG_SH_enable;

    if ( d->arch.paging.mode == 0 )
    {
        /* Init the shadow memory allocation and the hash table */
        if ( sh_set_allocation(d, 1, NULL) != 0 
             || shadow_hash_alloc(d) != 0 )
        {
            sh_set_allocation(d, 0, NULL);
            return -ENOMEM;
        }
    }

    /* Update the bits */
    sh_new_mode(d, d->arch.paging.mode | mode);

    return 0;
}

static int shadow_one_bit_disable(struct domain *d, u32 mode) 
/* Turn off a single shadow mode feature */
{
    struct vcpu *v;
    ASSERT(shadow_locked_by_me(d));

    /* Sanity check the call */
    if ( d == current->domain || !((d->arch.paging.mode & mode) == mode) )
    {
        return -EINVAL;
    }

    /* Update the bits */
    sh_new_mode(d, d->arch.paging.mode & ~mode);
    if ( d->arch.paging.mode == 0 )
    {
        /* Get this domain off shadows */
        SHADOW_PRINTK("un-shadowing of domain %u starts."
                       "  Shadow pages total = %u, free = %u, p2m=%u\n",
                       d->domain_id,
                       d->arch.paging.shadow.total_pages, 
                       d->arch.paging.shadow.free_pages, 
                       d->arch.paging.shadow.p2m_pages);
        for_each_vcpu(d, v)
        {
            if ( v->arch.paging.mode )
                v->arch.paging.mode->shadow.detach_old_tables(v);
#if CONFIG_PAGING_LEVELS == 4
            if ( !(v->arch.flags & TF_kernel_mode) )
                make_cr3(v, pagetable_get_pfn(v->arch.guest_table_user));
            else
#endif
                make_cr3(v, pagetable_get_pfn(v->arch.guest_table));

        }

        /* Pull down the memory allocation */
        if ( sh_set_allocation(d, 0, NULL) != 0 )
        {
            // XXX - How can this occur?
            //       Seems like a bug to return an error now that we've
            //       disabled the relevant shadow mode.
            //
            return -ENOMEM;
        }
        shadow_hash_teardown(d);
        SHADOW_PRINTK("un-shadowing of domain %u done."
                       "  Shadow pages total = %u, free = %u, p2m=%u\n",
                       d->domain_id,
                       d->arch.paging.shadow.total_pages, 
                       d->arch.paging.shadow.free_pages, 
                       d->arch.paging.shadow.p2m_pages);
    }

    return 0;
}

/* Enable/disable ops for the "test" and "log-dirty" modes */
static int shadow_test_enable(struct domain *d)
{
    int ret;

    domain_pause(d);
    shadow_lock(d);
    ret = shadow_one_bit_enable(d, PG_SH_enable);
    shadow_unlock(d);
    domain_unpause(d);

    return ret;
}

static int shadow_test_disable(struct domain *d)
{
    int ret;

    domain_pause(d);
    shadow_lock(d);
    ret = shadow_one_bit_disable(d, PG_SH_enable);
    shadow_unlock(d);
    domain_unpause(d);

    return ret;
}

/**************************************************************************/
/* P2M map manipulations */

/* shadow specific code which should be called when P2M table entry is updated
 * with new content. It is responsible for update the entry, as well as other 
 * shadow processing jobs.
 */

static void sh_unshadow_for_p2m_change(struct vcpu *v, unsigned long gfn, 
                                       l1_pgentry_t *p, mfn_t table_mfn, 
                                       l1_pgentry_t new, unsigned int level)
{
    struct domain *d = v->domain;

    /* If we're removing an MFN from the p2m, remove it from the shadows too */
    if ( level == 1 )
    {
        mfn_t mfn = _mfn(l1e_get_pfn(*p));
        p2m_type_t p2mt = p2m_flags_to_type(l1e_get_flags(*p));
        if ( (p2m_is_valid(p2mt) || p2m_is_grant(p2mt)) && mfn_valid(mfn) ) 
        {
            sh_remove_all_shadows_and_parents(v, mfn);
            if ( sh_remove_all_mappings(v, mfn) )
                flush_tlb_mask(&d->domain_dirty_cpumask);
        }
    }

    /* If we're removing a superpage mapping from the p2m, we need to check 
     * all the pages covered by it.  If they're still there in the new 
     * scheme, that's OK, but otherwise they must be unshadowed. */
    if ( level == 2 && (l1e_get_flags(*p) & _PAGE_PRESENT) &&
         (l1e_get_flags(*p) & _PAGE_PSE) )
    {
        unsigned int i;
        cpumask_t flushmask;
        mfn_t omfn = _mfn(l1e_get_pfn(*p));
        mfn_t nmfn = _mfn(l1e_get_pfn(new));
        l1_pgentry_t *npte = NULL;
        p2m_type_t p2mt = p2m_flags_to_type(l1e_get_flags(*p));
        if ( p2m_is_valid(p2mt) && mfn_valid(omfn) )
        {
            cpus_clear(flushmask);

            /* If we're replacing a superpage with a normal L1 page, map it */
            if ( (l1e_get_flags(new) & _PAGE_PRESENT)
                 && !(l1e_get_flags(new) & _PAGE_PSE) 
                 && mfn_valid(nmfn) )
                npte = map_domain_page(mfn_x(nmfn));
            
            for ( i = 0; i < L1_PAGETABLE_ENTRIES; i++ )
            {
                if ( !npte 
                     || !p2m_is_ram(p2m_flags_to_type(l1e_get_flags(npte[i])))
                     || l1e_get_pfn(npte[i]) != mfn_x(omfn) )
                {
                    /* This GFN->MFN mapping has gone away */
                    sh_remove_all_shadows_and_parents(v, omfn);
                    if ( sh_remove_all_mappings(v, omfn) )
                        cpus_or(flushmask, flushmask, d->domain_dirty_cpumask);
                }
                omfn = _mfn(mfn_x(omfn) + 1);
            }
            flush_tlb_mask(&flushmask);
            
            if ( npte )
                unmap_domain_page(npte);
        }
    }
}

void
shadow_write_p2m_entry(struct vcpu *v, unsigned long gfn, 
                       l1_pgentry_t *p, mfn_t table_mfn, 
                       l1_pgentry_t new, unsigned int level)
{
    struct domain *d = v->domain;
    
    shadow_lock(d);

    /* If there are any shadows, update them.  But if shadow_teardown()
     * has already been called then it's not safe to try. */ 
    if ( likely(d->arch.paging.shadow.total_pages != 0) )
         sh_unshadow_for_p2m_change(v, gfn, p, table_mfn, new, level);

    /* Update the entry with new content */
    safe_write_pte(p, new);

    /* install P2M in monitors for PAE Xen */
#if CONFIG_PAGING_LEVELS == 3
    if ( level == 3 )
        /* We have written to the p2m l3: need to sync the per-vcpu
         * copies of it in the monitor tables */
        p2m_install_entry_in_monitors(d, (l3_pgentry_t *)p);
#endif

#if (SHADOW_OPTIMIZATIONS & SHOPT_FAST_FAULT_PATH)
    /* If we're doing FAST_FAULT_PATH, then shadow mode may have
       cached the fact that this is an mmio region in the shadow
       page tables.  Blow the tables away to remove the cache.
       This is pretty heavy handed, but this is a rare operation
       (it might happen a dozen times during boot and then never
       again), so it doesn't matter too much. */
    if ( d->arch.paging.shadow.has_fast_mmio_entries )
    {
        shadow_blow_tables(d);
        d->arch.paging.shadow.has_fast_mmio_entries = 0;
    }
#endif

    shadow_unlock(d);
}

/**************************************************************************/
/* Log-dirty mode support */

/* Shadow specific code which is called in paging_log_dirty_enable().
 * Return 0 if no problem found.
 */
int shadow_enable_log_dirty(struct domain *d)
{
    int ret;

    /* shadow lock is required here */
    shadow_lock(d);
    if ( shadow_mode_enabled(d) )
    {
        /* This domain already has some shadows: need to clear them out 
         * of the way to make sure that all references to guest memory are 
         * properly write-protected */
        shadow_blow_tables(d);
    }

#if (SHADOW_OPTIMIZATIONS & SHOPT_LINUX_L3_TOPLEVEL)
    /* 32bit PV guests on 64bit xen behave like older 64bit linux: they
     * change an l4e instead of cr3 to switch tables.  Give them the
     * same optimization */
    if ( is_pv_32on64_domain(d) )
        d->arch.paging.shadow.opt_flags = SHOPT_LINUX_L3_TOPLEVEL;
#endif
    
    ret = shadow_one_bit_enable(d, PG_log_dirty);
    shadow_unlock(d);

    return ret;
}

/* shadow specfic code which is called in paging_log_dirty_disable() */
int shadow_disable_log_dirty(struct domain *d)
{
    int ret;

    /* shadow lock is required here */    
    shadow_lock(d);
    ret = shadow_one_bit_disable(d, PG_log_dirty);
    shadow_unlock(d);
    
    return ret;
}

/* This function is called when we CLEAN log dirty bitmap. See 
 * paging_log_dirty_op() for details. 
 */
void shadow_clean_dirty_bitmap(struct domain *d)
{
    shadow_lock(d);
    /* Need to revoke write access to the domain's pages again.
     * In future, we'll have a less heavy-handed approach to this,
     * but for now, we just unshadow everything except Xen. */
    shadow_blow_tables(d);
    shadow_unlock(d);
}


/**************************************************************************/
/* VRAM dirty tracking support */
int shadow_track_dirty_vram(struct domain *d,
                            unsigned long begin_pfn,
                            unsigned long nr,
                            XEN_GUEST_HANDLE_64(uint8) dirty_bitmap)
{
    int rc;
    unsigned long end_pfn = begin_pfn + nr;
    unsigned long dirty_size = (nr + 7) / 8;
    int flush_tlb = 0;
    unsigned long i;
    p2m_type_t t;
    struct sh_dirty_vram *dirty_vram = d->arch.hvm_domain.dirty_vram;

    if (end_pfn < begin_pfn
            || begin_pfn > d->arch.p2m->max_mapped_pfn
            || end_pfn >= d->arch.p2m->max_mapped_pfn)
        return -EINVAL;

    shadow_lock(d);

    if ( dirty_vram && (!nr ||
             ( begin_pfn != dirty_vram->begin_pfn
            || end_pfn   != dirty_vram->end_pfn )) )
    {
        /* Different tracking, tear the previous down. */
        gdprintk(XENLOG_INFO, "stopping tracking VRAM %lx - %lx\n", dirty_vram->begin_pfn, dirty_vram->end_pfn);
        xfree(dirty_vram->sl1ma);
        xfree(dirty_vram->dirty_bitmap);
        xfree(dirty_vram);
        dirty_vram = d->arch.hvm_domain.dirty_vram = NULL;
    }

    if ( !nr )
    {
        rc = 0;
        goto out;
    }

    /* This should happen seldomly (Video mode change),
     * no need to be careful. */
    if ( !dirty_vram )
    {
        /* Throw away all the shadows rather than walking through them 
         * up to nr times getting rid of mappings of each pfn */
        shadow_blow_tables(d);

        gdprintk(XENLOG_INFO, "tracking VRAM %lx - %lx\n", begin_pfn, end_pfn);

        rc = -ENOMEM;
        if ( (dirty_vram = xmalloc(struct sh_dirty_vram)) == NULL )
            goto out;
        dirty_vram->begin_pfn = begin_pfn;
        dirty_vram->end_pfn = end_pfn;
        d->arch.hvm_domain.dirty_vram = dirty_vram;

        if ( (dirty_vram->sl1ma = xmalloc_array(paddr_t, nr)) == NULL )
            goto out_dirty_vram;
        memset(dirty_vram->sl1ma, ~0, sizeof(paddr_t) * nr);

        if ( (dirty_vram->dirty_bitmap = xmalloc_array(uint8_t, dirty_size)) == NULL )
            goto out_sl1ma;
        memset(dirty_vram->dirty_bitmap, 0, dirty_size);

        dirty_vram->last_dirty = NOW();

        /* Tell the caller that this time we could not track dirty bits. */
        rc = -ENODATA;
    }
    else if (dirty_vram->last_dirty == -1)
    {
        /* still completely clean, just copy our empty bitmap */
        rc = -EFAULT;
        if ( copy_to_guest(dirty_bitmap, dirty_vram->dirty_bitmap, dirty_size) == 0 )
            rc = 0;
    }
    else
    {
#ifdef __i386__
        unsigned long map_mfn = INVALID_MFN;
        void *map_sl1p = NULL;
#endif

        /* Iterate over VRAM to track dirty bits. */
        for ( i = 0; i < nr; i++ ) {
            mfn_t mfn = gfn_to_mfn(d, begin_pfn + i, &t);
            struct page_info *page;
            int dirty = 0;
            paddr_t sl1ma = dirty_vram->sl1ma[i];

            if (mfn_x(mfn) == INVALID_MFN)
            {
                dirty = 1;
            }
            else
            {
                page = mfn_to_page(mfn);
                switch (page->u.inuse.type_info & PGT_count_mask)
                {
                case 0:
                    /* No guest reference, nothing to track. */
                    break;
                case 1:
                    /* One guest reference. */
                    if ( sl1ma == INVALID_PADDR )
                    {
                        /* We don't know which sl1e points to this, too bad. */
                        dirty = 1;
                        /* TODO: Heuristics for finding the single mapping of
                         * this gmfn */
                        flush_tlb |= sh_remove_all_mappings(d->vcpu[0], mfn);
                    }
                    else
                    {
                        /* Hopefully the most common case: only one mapping,
                         * whose dirty bit we can use. */
                        l1_pgentry_t *sl1e;
#ifdef __i386__
                        void *sl1p = map_sl1p;
                        unsigned long sl1mfn = paddr_to_pfn(sl1ma);

                        if ( sl1mfn != map_mfn ) {
                            if ( map_sl1p )
                                sh_unmap_domain_page(map_sl1p);
                            map_sl1p = sl1p = sh_map_domain_page(_mfn(sl1mfn));
                            map_mfn = sl1mfn;
                        }
                        sl1e = sl1p + (sl1ma & ~PAGE_MASK);
#else
                        sl1e = maddr_to_virt(sl1ma);
#endif

                        if ( l1e_get_flags(*sl1e) & _PAGE_DIRTY )
                        {
                            dirty = 1;
                            /* Note: this is atomic, so we may clear a
                             * _PAGE_ACCESSED set by another processor. */
                            l1e_remove_flags(*sl1e, _PAGE_DIRTY);
                            flush_tlb = 1;
                        }
                    }
                    break;
                default:
                    /* More than one guest reference,
                     * we don't afford tracking that. */
                    dirty = 1;
                    break;
                }
            }

            if ( dirty )
            {
                dirty_vram->dirty_bitmap[i / 8] |= 1 << (i % 8);
                dirty_vram->last_dirty = NOW();
            }
        }

#ifdef __i386__
        if ( map_sl1p )
            sh_unmap_domain_page(map_sl1p);
#endif

        rc = -EFAULT;
        if ( copy_to_guest(dirty_bitmap, dirty_vram->dirty_bitmap, dirty_size) == 0 ) {
            memset(dirty_vram->dirty_bitmap, 0, dirty_size);
            if (dirty_vram->last_dirty + SECONDS(2) < NOW())
            {
                /* was clean for more than two seconds, try to disable guest
                 * write access */
                for ( i = begin_pfn; i < end_pfn; i++ ) {
                    mfn_t mfn = gfn_to_mfn(d, i, &t);
                    if (mfn_x(mfn) != INVALID_MFN)
                        flush_tlb |= sh_remove_write_access(d->vcpu[0], mfn, 1, 0);
                }
                dirty_vram->last_dirty = -1;
            }
            rc = 0;
        }
    }
    if ( flush_tlb )
        flush_tlb_mask(&d->domain_dirty_cpumask);
    goto out;

out_sl1ma:
    xfree(dirty_vram->sl1ma);
out_dirty_vram:
    xfree(dirty_vram);
    dirty_vram = d->arch.hvm_domain.dirty_vram = NULL;

out:
    shadow_unlock(d);
    return rc;
}

/**************************************************************************/
/* Shadow-control XEN_DOMCTL dispatcher */

int shadow_domctl(struct domain *d, 
                  xen_domctl_shadow_op_t *sc,
                  XEN_GUEST_HANDLE(void) u_domctl)
{
    int rc, preempted = 0;

    switch ( sc->op )
    {
    case XEN_DOMCTL_SHADOW_OP_OFF:
        if ( d->arch.paging.mode == PG_SH_enable )
            if ( (rc = shadow_test_disable(d)) != 0 ) 
                return rc;
        return 0;

    case XEN_DOMCTL_SHADOW_OP_ENABLE_TEST:
        return shadow_test_enable(d);

    case XEN_DOMCTL_SHADOW_OP_ENABLE_TRANSLATE:
        return shadow_enable(d, PG_refcounts|PG_translate);

    case XEN_DOMCTL_SHADOW_OP_ENABLE:
        return shadow_enable(d, sc->mode << PG_mode_shift);

    case XEN_DOMCTL_SHADOW_OP_GET_ALLOCATION:
        sc->mb = shadow_get_allocation(d);
        return 0;

    case XEN_DOMCTL_SHADOW_OP_SET_ALLOCATION:
        shadow_lock(d);
        if ( sc->mb == 0 && shadow_mode_enabled(d) )
        {            
            /* Can't set the allocation to zero unless the domain stops using
             * shadow pagetables first */
            SHADOW_ERROR("Can't set shadow allocation to zero, domain %u"
                         " is still using shadows.\n", d->domain_id);
            shadow_unlock(d);
            return -EINVAL;
        }
        rc = sh_set_allocation(d, sc->mb << (20 - PAGE_SHIFT), &preempted);
        shadow_unlock(d);
        if ( preempted )
            /* Not finished.  Set up to re-run the call. */
            rc = hypercall_create_continuation(
                __HYPERVISOR_domctl, "h", u_domctl);
        else 
            /* Finished.  Return the new allocation */
            sc->mb = shadow_get_allocation(d);
        return rc;

    default:
        SHADOW_ERROR("Bad shadow op %u\n", sc->op);
        return -EINVAL;
    }
}


/**************************************************************************/
/* Auditing shadow tables */

#if SHADOW_AUDIT & SHADOW_AUDIT_ENTRIES_FULL

void shadow_audit_tables(struct vcpu *v) 
{
    /* Dispatch table for getting per-type functions */
    static const hash_callback_t callbacks[SH_type_unused] = {
        NULL, /* none    */
        SHADOW_INTERNAL_NAME(sh_audit_l1_table, 2),  /* l1_32   */
        SHADOW_INTERNAL_NAME(sh_audit_fl1_table, 2), /* fl1_32  */
        SHADOW_INTERNAL_NAME(sh_audit_l2_table, 2),  /* l2_32   */
        SHADOW_INTERNAL_NAME(sh_audit_l1_table, 3),  /* l1_pae  */
        SHADOW_INTERNAL_NAME(sh_audit_fl1_table, 3), /* fl1_pae */
        SHADOW_INTERNAL_NAME(sh_audit_l2_table, 3),  /* l2_pae  */
        SHADOW_INTERNAL_NAME(sh_audit_l2_table, 3),  /* l2h_pae */
#if CONFIG_PAGING_LEVELS >= 4
        SHADOW_INTERNAL_NAME(sh_audit_l1_table, 4),  /* l1_64   */
        SHADOW_INTERNAL_NAME(sh_audit_fl1_table, 4), /* fl1_64  */
        SHADOW_INTERNAL_NAME(sh_audit_l2_table, 4),  /* l2_64   */
        SHADOW_INTERNAL_NAME(sh_audit_l2_table, 4),  /* l2h_64   */
        SHADOW_INTERNAL_NAME(sh_audit_l3_table, 4),  /* l3_64   */
        SHADOW_INTERNAL_NAME(sh_audit_l4_table, 4),  /* l4_64   */
#endif /* CONFIG_PAGING_LEVELS >= 4 */
        NULL  /* All the rest */
    };
    unsigned int mask; 

    if ( !(SHADOW_AUDIT_ENABLE) )
        return;

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
    sh_oos_audit(v->domain);
#endif

    if ( SHADOW_AUDIT & SHADOW_AUDIT_ENTRIES_FULL )
        mask = ~1; /* Audit every table in the system */
    else 
    {
        /* Audit only the current mode's tables */
        switch ( v->arch.paging.mode->guest_levels )
        {
        case 2: mask = (SHF_L1_32|SHF_FL1_32|SHF_L2_32); break;
        case 3: mask = (SHF_L1_PAE|SHF_FL1_PAE|SHF_L2_PAE
                        |SHF_L2H_PAE); break;
        case 4: mask = (SHF_L1_64|SHF_FL1_64|SHF_L2_64  
                        |SHF_L3_64|SHF_L4_64); break;
        default: BUG();
        }
    }

    hash_foreach(v, ~1, callbacks, _mfn(INVALID_MFN));
}

#endif /* Shadow audit */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End: 
 */
