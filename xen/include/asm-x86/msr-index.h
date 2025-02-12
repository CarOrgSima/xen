#ifndef __ASM_MSR_INDEX_H
#define __ASM_MSR_INDEX_H

/* CPU model specific register (MSR) numbers */

/* x86-64 specific MSRs */
#define MSR_EFER		0xc0000080 /* extended feature register */
#define MSR_STAR		0xc0000081 /* legacy mode SYSCALL target */
#define MSR_LSTAR		0xc0000082 /* long mode SYSCALL target */
#define MSR_CSTAR		0xc0000083 /* compat mode SYSCALL target */
#define MSR_SYSCALL_MASK	0xc0000084 /* EFLAGS mask for syscall */
#define MSR_FS_BASE		0xc0000100 /* 64bit FS base */
#define MSR_GS_BASE		0xc0000101 /* 64bit GS base */
#define MSR_SHADOW_GS_BASE	0xc0000102 /* SwapGS GS shadow */
#define MSR_TSC_AUX		0xc0000103 /* Auxiliary TSC */

/* EFER bits: */
#define _EFER_SCE		0  /* SYSCALL/SYSRET */
#define _EFER_LME		8  /* Long mode enable */
#define _EFER_LMA		10 /* Long mode active (read-only) */
#define _EFER_NX		11 /* No execute enable */
#define _EFER_SVME		12 /* AMD: SVM enable */
#define _EFER_LMSLE		13 /* AMD: Long-mode segment limit enable */
#define _EFER_FFXSE		14 /* AMD: Fast FXSAVE/FXRSTOR enable */

#define EFER_SCE		(1<<_EFER_SCE)
#define EFER_LME		(1<<_EFER_LME)
#define EFER_LMA		(1<<_EFER_LMA)
#define EFER_NX			(1<<_EFER_NX)
#define EFER_SVME		(1<<_EFER_SVME)
#define EFER_LMSLE		(1<<_EFER_LMSLE)
#define EFER_FFXSE		(1<<_EFER_FFXSE)

/* Intel MSRs. Some also available on other CPUs */
#define MSR_IA32_PERFCTR0		0x000000c1
#define MSR_IA32_PERFCTR1		0x000000c2
#define MSR_FSB_FREQ			0x000000cd

#define MSR_MTRRcap			0x000000fe
#define MSR_IA32_BBL_CR_CTL		0x00000119

#define MSR_IA32_SYSENTER_CS		0x00000174
#define MSR_IA32_SYSENTER_ESP		0x00000175
#define MSR_IA32_SYSENTER_EIP		0x00000176

#define MSR_IA32_MCG_CAP		0x00000179
#define MSR_IA32_MCG_STATUS		0x0000017a
#define MSR_IA32_MCG_CTL		0x0000017b

#define MSR_IA32_PEBS_ENABLE		0x000003f1
#define MSR_IA32_DS_AREA		0x00000600
#define MSR_IA32_PERF_CAPABILITIES	0x00000345

#define MSR_MTRRfix64K_00000		0x00000250
#define MSR_MTRRfix16K_80000		0x00000258
#define MSR_MTRRfix16K_A0000		0x00000259
#define MSR_MTRRfix4K_C0000		0x00000268
#define MSR_MTRRfix4K_C8000		0x00000269
#define MSR_MTRRfix4K_D0000		0x0000026a
#define MSR_MTRRfix4K_D8000		0x0000026b
#define MSR_MTRRfix4K_E0000		0x0000026c
#define MSR_MTRRfix4K_E8000		0x0000026d
#define MSR_MTRRfix4K_F0000		0x0000026e
#define MSR_MTRRfix4K_F8000		0x0000026f
#define MSR_MTRRdefType			0x000002ff

#define MSR_IA32_DEBUGCTLMSR		0x000001d9
#define MSR_IA32_LASTBRANCHFROMIP	0x000001db
#define MSR_IA32_LASTBRANCHTOIP		0x000001dc
#define MSR_IA32_LASTINTFROMIP		0x000001dd
#define MSR_IA32_LASTINTTOIP		0x000001de
 
#define MSR_IA32_MTRR_PHYSBASE0     0x00000200
#define MSR_IA32_MTRR_PHYSMASK0     0x00000201
#define MSR_IA32_MTRR_PHYSBASE1     0x00000202
#define MSR_IA32_MTRR_PHYSMASK1     0x00000203
#define MSR_IA32_MTRR_PHYSBASE2     0x00000204
#define MSR_IA32_MTRR_PHYSMASK2     0x00000205
#define MSR_IA32_MTRR_PHYSBASE3     0x00000206
#define MSR_IA32_MTRR_PHYSMASK3     0x00000207
#define MSR_IA32_MTRR_PHYSBASE4     0x00000208
#define MSR_IA32_MTRR_PHYSMASK4     0x00000209
#define MSR_IA32_MTRR_PHYSBASE5     0x0000020a
#define MSR_IA32_MTRR_PHYSMASK5     0x0000020b
#define MSR_IA32_MTRR_PHYSBASE6     0x0000020c
#define MSR_IA32_MTRR_PHYSMASK6     0x0000020d
#define MSR_IA32_MTRR_PHYSBASE7     0x0000020e
#define MSR_IA32_MTRR_PHYSMASK7     0x0000020f

#define MSR_IA32_CR_PAT             0x00000277
#define MSR_IA32_CR_PAT_RESET       0x0007040600070406ULL

#define MSR_IA32_MC0_CTL		0x00000400
#define MSR_IA32_MC0_STATUS		0x00000401
#define MSR_IA32_MC0_ADDR		0x00000402
#define MSR_IA32_MC0_MISC		0x00000403
#define MSR_IA32_MC0_CTL2		0x00000280
#define CMCI_EN 			(1UL<<30)
#define CMCI_THRESHOLD_MASK		0x7FFF

#define MSR_IA32_MC1_CTL		0x00000404
#define MSR_IA32_MC1_CTL2		0x00000281
#define MSR_IA32_MC1_STATUS		0x00000405
#define MSR_IA32_MC1_ADDR		0x00000406
#define MSR_IA32_MC1_MISC		0x00000407

#define MSR_IA32_MC2_CTL		0x00000408
#define MSR_IA32_MC2_CTL2		0x00000282
#define MSR_IA32_MC2_STATUS		0x00000409
#define MSR_IA32_MC2_ADDR		0x0000040A
#define MSR_IA32_MC2_MISC		0x0000040B

#define MSR_IA32_MC3_CTL2		0x00000283
#define MSR_IA32_MC3_CTL		0x0000040C
#define MSR_IA32_MC3_STATUS		0x0000040D
#define MSR_IA32_MC3_ADDR		0x0000040E
#define MSR_IA32_MC3_MISC		0x0000040F

#define MSR_IA32_MC4_CTL2		0x00000284
#define MSR_IA32_MC4_CTL		0x00000410
#define MSR_IA32_MC4_STATUS		0x00000411
#define MSR_IA32_MC4_ADDR		0x00000412
#define MSR_IA32_MC4_MISC		0x00000413

#define MSR_IA32_MC5_CTL2		0x00000285
#define MSR_IA32_MC5_CTL		0x00000414
#define MSR_IA32_MC5_STATUS		0x00000415
#define MSR_IA32_MC5_ADDR		0x00000416
#define MSR_IA32_MC5_MISC		0x00000417

#define MSR_IA32_MC6_CTL2		0x00000286
#define MSR_IA32_MC6_CTL		0x00000418
#define MSR_IA32_MC6_STATUS		0x00000419
#define MSR_IA32_MC6_ADDR		0x0000041A
#define MSR_IA32_MC6_MISC		0x0000041B

#define MSR_IA32_MC7_CTL2		0x00000287
#define MSR_IA32_MC7_CTL		0x0000041C
#define MSR_IA32_MC7_STATUS		0x0000041D
#define MSR_IA32_MC7_ADDR		0x0000041E
#define MSR_IA32_MC7_MISC		0x0000041F

#define MSR_IA32_MC8_CTL2		0x00000288
#define MSR_IA32_MC8_CTL		0x00000420
#define MSR_IA32_MC8_STATUS		0x00000421
#define MSR_IA32_MC8_ADDR		0x00000422
#define MSR_IA32_MC8_MISC		0x00000423

#define MSR_IA32_MCx_CTL(x)		(MSR_IA32_MC0_CTL + 4*(x))
#define MSR_IA32_MCx_STATUS(x)		(MSR_IA32_MC0_STATUS + 4*(x))
#define MSR_IA32_MCx_ADDR(x)		(MSR_IA32_MC0_ADDR + 4*(x))
#define MSR_IA32_MCx_MISC(x)		(MSR_IA32_MC0_MISC + 4*(x)) 

#define MSR_P6_PERFCTR0			0x000000c1
#define MSR_P6_PERFCTR1			0x000000c2
#define MSR_P6_EVNTSEL0			0x00000186
#define MSR_P6_EVNTSEL1			0x00000187

/* MSR for cpuid feature mask */
#define MSR_IA32_CPUID_FEATURE_MASK1	0x00000478

/* MSRs & bits used for VMX enabling */
#define MSR_IA32_VMX_BASIC                      0x480
#define MSR_IA32_VMX_PINBASED_CTLS              0x481
#define MSR_IA32_VMX_PROCBASED_CTLS             0x482
#define MSR_IA32_VMX_EXIT_CTLS                  0x483
#define MSR_IA32_VMX_ENTRY_CTLS                 0x484
#define MSR_IA32_VMX_MISC                       0x485
#define MSR_IA32_VMX_CR0_FIXED0                 0x486
#define MSR_IA32_VMX_CR0_FIXED1                 0x487
#define MSR_IA32_VMX_CR4_FIXED0                 0x488
#define MSR_IA32_VMX_CR4_FIXED1                 0x489
#define MSR_IA32_VMX_PROCBASED_CTLS2            0x48b
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS         0x48d
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS        0x48e
#define MSR_IA32_VMX_TRUE_EXIT_CTLS             0x48f
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS            0x490
#define IA32_FEATURE_CONTROL_MSR                0x3a
#define IA32_FEATURE_CONTROL_MSR_LOCK                     0x0001
#define IA32_FEATURE_CONTROL_MSR_ENABLE_VMXON_INSIDE_SMX  0x0002
#define IA32_FEATURE_CONTROL_MSR_ENABLE_VMXON_OUTSIDE_SMX 0x0004
#define IA32_FEATURE_CONTROL_MSR_SENTER_PARAM_CTL         0x7f00
#define IA32_FEATURE_CONTROL_MSR_ENABLE_SENTER            0x8000

/* K7/K8 MSRs. Not complete. See the architecture manual for a more
   complete list. */
#define MSR_K7_EVNTSEL0			0xc0010000
#define MSR_K7_PERFCTR0			0xc0010004
#define MSR_K7_EVNTSEL1			0xc0010001
#define MSR_K7_PERFCTR1			0xc0010005
#define MSR_K7_EVNTSEL2			0xc0010002
#define MSR_K7_PERFCTR2			0xc0010006
#define MSR_K7_EVNTSEL3			0xc0010003
#define MSR_K7_PERFCTR3			0xc0010007
#define MSR_K8_TOP_MEM1			0xc001001a
#define MSR_K7_CLK_CTL			0xc001001b
#define MSR_K8_TOP_MEM2			0xc001001d
#define MSR_K8_SYSCFG			0xc0010010

#define K8_MTRRFIXRANGE_DRAM_ENABLE	0x00040000 /* MtrrFixDramEn bit    */
#define K8_MTRRFIXRANGE_DRAM_MODIFY	0x00080000 /* MtrrFixDramModEn bit */
#define K8_MTRR_RDMEM_WRMEM_MASK	0x18181818 /* Mask: RdMem|WrMem    */

#define MSR_K7_HWCR			0xc0010015
#define MSR_K8_HWCR			0xc0010015
#define MSR_K7_FID_VID_CTL		0xc0010041
#define MSR_K7_FID_VID_STATUS		0xc0010042
#define MSR_K8_PSTATE_LIMIT		0xc0010061
#define MSR_K8_PSTATE_CTRL		0xc0010062
#define MSR_K8_PSTATE_STATUS		0xc0010063
#define MSR_K8_PSTATE0			0xc0010064
#define MSR_K8_PSTATE1			0xc0010065
#define MSR_K8_PSTATE2			0xc0010066
#define MSR_K8_PSTATE3			0xc0010067
#define MSR_K8_PSTATE4			0xc0010068
#define MSR_K8_PSTATE5			0xc0010069
#define MSR_K8_PSTATE6			0xc001006A
#define MSR_K8_PSTATE7			0xc001006B
#define MSR_K8_ENABLE_C1E		0xc0010055
#define MSR_K8_VM_CR			0xc0010114
#define MSR_K8_VM_HSAVE_PA		0xc0010117

#define MSR_K8_FEATURE_MASK		0xc0011004
#define MSR_K8_EXT_FEATURE_MASK		0xc0011005

/* MSR_K8_VM_CR bits: */
#define _K8_VMCR_SVME_DISABLE		4
#define K8_VMCR_SVME_DISABLE		(1 << _K8_VMCR_SVME_DISABLE)

/* AMD64 MSRs */
#define MSR_AMD64_NB_CFG		0xc001001f
#define MSR_AMD64_DC_CFG		0xc0011022
#define AMD64_NB_CFG_CF8_EXT_ENABLE_BIT	46

/* AMD Family10h machine check MSRs */
#define MSR_F10_MC4_MISC1		0xc0000408
#define MSR_F10_MC4_MISC2		0xc0000409
#define MSR_F10_MC4_MISC3		0xc000040A

/* Other AMD Fam10h MSRs */
#define MSR_FAM10H_MMIO_CONF_BASE	0xc0010058
#define FAM10H_MMIO_CONF_ENABLE         (1<<0)
#define FAM10H_MMIO_CONF_BUSRANGE_MASK	0xf
#define FAM10H_MMIO_CONF_BUSRANGE_SHIFT 2
#define FAM10H_MMIO_CONF_BASE_MASK	0xfffffff
#define FAM10H_MMIO_CONF_BASE_SHIFT	20

/* AMD Microcode MSRs */
#define MSR_AMD_PATCHLEVEL		0x0000008b
#define MSR_AMD_PATCHLOADER		0xc0010020

/* K6 MSRs */
#define MSR_K6_EFER			0xc0000080
#define MSR_K6_STAR			0xc0000081
#define MSR_K6_WHCR			0xc0000082
#define MSR_K6_UWCCR			0xc0000085
#define MSR_K6_EPMR			0xc0000086
#define MSR_K6_PSOR			0xc0000087
#define MSR_K6_PFIR			0xc0000088

/* Centaur-Hauls/IDT defined MSRs. */
#define MSR_IDT_FCR1			0x00000107
#define MSR_IDT_FCR2			0x00000108
#define MSR_IDT_FCR3			0x00000109
#define MSR_IDT_FCR4			0x0000010a

#define MSR_IDT_MCR0			0x00000110
#define MSR_IDT_MCR1			0x00000111
#define MSR_IDT_MCR2			0x00000112
#define MSR_IDT_MCR3			0x00000113
#define MSR_IDT_MCR4			0x00000114
#define MSR_IDT_MCR5			0x00000115
#define MSR_IDT_MCR6			0x00000116
#define MSR_IDT_MCR7			0x00000117
#define MSR_IDT_MCR_CTRL		0x00000120

/* VIA Cyrix defined MSRs*/
#define MSR_VIA_FCR			0x00001107
#define MSR_VIA_LONGHAUL		0x0000110a
#define MSR_VIA_RNG			0x0000110b
#define MSR_VIA_BCR2			0x00001147

/* Transmeta defined MSRs */
#define MSR_TMTA_LONGRUN_CTRL		0x80868010
#define MSR_TMTA_LONGRUN_FLAGS		0x80868011
#define MSR_TMTA_LRTI_READOUT		0x80868018
#define MSR_TMTA_LRTI_VOLT_MHZ		0x8086801a

/* Intel defined MSRs. */
#define MSR_IA32_P5_MC_ADDR		0x00000000
#define MSR_IA32_P5_MC_TYPE		0x00000001
#define MSR_IA32_TSC			0x00000010
#define MSR_IA32_PLATFORM_ID		0x00000017
#define MSR_IA32_EBL_CR_POWERON		0x0000002a
#define MSR_IA32_EBC_FREQUENCY_ID	0x0000002c

#define MSR_IA32_APICBASE		0x0000001b
#define MSR_IA32_APICBASE_BSP		(1<<8)
#define MSR_IA32_APICBASE_EXTD		(1<<10)
#define MSR_IA32_APICBASE_ENABLE	(1<<11)
#define MSR_IA32_APICBASE_BASE		(0xfffff<<12)

#define MSR_IA32_UCODE_WRITE		0x00000079
#define MSR_IA32_UCODE_REV		0x0000008b

#define MSR_IA32_PERF_STATUS		0x00000198
#define MSR_IA32_PERF_CTL		0x00000199

#define MSR_IA32_MPERF			0x000000e7
#define MSR_IA32_APERF			0x000000e8

#define MSR_IA32_THERM_CONTROL		0x0000019a
#define MSR_IA32_THERM_INTERRUPT	0x0000019b
#define MSR_IA32_THERM_STATUS		0x0000019c
#define MSR_IA32_MISC_ENABLE		0x000001a0
#define MSR_IA32_MISC_ENABLE_PERF_AVAIL   (1<<7)
#define MSR_IA32_MISC_ENABLE_BTS_UNAVAIL  (1<<11)
#define MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL (1<<12)
#define MSR_IA32_MISC_ENABLE_MONITOR_ENABLE (1<<18)
#define MSR_IA32_MISC_ENABLE_XTPR_DISABLE (1<<23)

/* Intel Model 6 */
#define MSR_P6_EVNTSEL0			0x00000186
#define MSR_P6_EVNTSEL1			0x00000187

/* P4/Xeon+ specific */
#define MSR_IA32_MCG_EAX		0x00000180
#define MSR_IA32_MCG_EBX		0x00000181
#define MSR_IA32_MCG_ECX		0x00000182
#define MSR_IA32_MCG_EDX		0x00000183
#define MSR_IA32_MCG_ESI		0x00000184
#define MSR_IA32_MCG_EDI		0x00000185
#define MSR_IA32_MCG_EBP		0x00000186
#define MSR_IA32_MCG_ESP		0x00000187
#define MSR_IA32_MCG_EFLAGS		0x00000188
#define MSR_IA32_MCG_EIP		0x00000189
#define MSR_IA32_MCG_MISC		0x0000018a
#define MSR_IA32_MCG_R8			0x00000190
#define MSR_IA32_MCG_R9			0x00000191
#define MSR_IA32_MCG_R10		0x00000192
#define MSR_IA32_MCG_R11		0x00000193
#define MSR_IA32_MCG_R12		0x00000194
#define MSR_IA32_MCG_R13		0x00000195
#define MSR_IA32_MCG_R14		0x00000196
#define MSR_IA32_MCG_R15		0x00000197

/* Pentium IV performance counter MSRs */
#define MSR_P4_BPU_PERFCTR0		0x00000300
#define MSR_P4_BPU_PERFCTR1		0x00000301
#define MSR_P4_BPU_PERFCTR2		0x00000302
#define MSR_P4_BPU_PERFCTR3		0x00000303
#define MSR_P4_MS_PERFCTR0		0x00000304
#define MSR_P4_MS_PERFCTR1		0x00000305
#define MSR_P4_MS_PERFCTR2		0x00000306
#define MSR_P4_MS_PERFCTR3		0x00000307
#define MSR_P4_FLAME_PERFCTR0		0x00000308
#define MSR_P4_FLAME_PERFCTR1		0x00000309
#define MSR_P4_FLAME_PERFCTR2		0x0000030a
#define MSR_P4_FLAME_PERFCTR3		0x0000030b
#define MSR_P4_IQ_PERFCTR0		0x0000030c
#define MSR_P4_IQ_PERFCTR1		0x0000030d
#define MSR_P4_IQ_PERFCTR2		0x0000030e
#define MSR_P4_IQ_PERFCTR3		0x0000030f
#define MSR_P4_IQ_PERFCTR4		0x00000310
#define MSR_P4_IQ_PERFCTR5		0x00000311
#define MSR_P4_BPU_CCCR0		0x00000360
#define MSR_P4_BPU_CCCR1		0x00000361
#define MSR_P4_BPU_CCCR2		0x00000362
#define MSR_P4_BPU_CCCR3		0x00000363
#define MSR_P4_MS_CCCR0			0x00000364
#define MSR_P4_MS_CCCR1			0x00000365
#define MSR_P4_MS_CCCR2			0x00000366
#define MSR_P4_MS_CCCR3			0x00000367
#define MSR_P4_FLAME_CCCR0		0x00000368
#define MSR_P4_FLAME_CCCR1		0x00000369
#define MSR_P4_FLAME_CCCR2		0x0000036a
#define MSR_P4_FLAME_CCCR3		0x0000036b
#define MSR_P4_IQ_CCCR0			0x0000036c
#define MSR_P4_IQ_CCCR1			0x0000036d
#define MSR_P4_IQ_CCCR2			0x0000036e
#define MSR_P4_IQ_CCCR3			0x0000036f
#define MSR_P4_IQ_CCCR4			0x00000370
#define MSR_P4_IQ_CCCR5			0x00000371
#define MSR_P4_ALF_ESCR0		0x000003ca
#define MSR_P4_ALF_ESCR1		0x000003cb
#define MSR_P4_BPU_ESCR0		0x000003b2
#define MSR_P4_BPU_ESCR1		0x000003b3
#define MSR_P4_BSU_ESCR0		0x000003a0
#define MSR_P4_BSU_ESCR1		0x000003a1
#define MSR_P4_CRU_ESCR0		0x000003b8
#define MSR_P4_CRU_ESCR1		0x000003b9
#define MSR_P4_CRU_ESCR2		0x000003cc
#define MSR_P4_CRU_ESCR3		0x000003cd
#define MSR_P4_CRU_ESCR4		0x000003e0
#define MSR_P4_CRU_ESCR5		0x000003e1
#define MSR_P4_DAC_ESCR0		0x000003a8
#define MSR_P4_DAC_ESCR1		0x000003a9
#define MSR_P4_FIRM_ESCR0		0x000003a4
#define MSR_P4_FIRM_ESCR1		0x000003a5
#define MSR_P4_FLAME_ESCR0		0x000003a6
#define MSR_P4_FLAME_ESCR1		0x000003a7
#define MSR_P4_FSB_ESCR0		0x000003a2
#define MSR_P4_FSB_ESCR1		0x000003a3
#define MSR_P4_IQ_ESCR0			0x000003ba
#define MSR_P4_IQ_ESCR1			0x000003bb
#define MSR_P4_IS_ESCR0			0x000003b4
#define MSR_P4_IS_ESCR1			0x000003b5
#define MSR_P4_ITLB_ESCR0		0x000003b6
#define MSR_P4_ITLB_ESCR1		0x000003b7
#define MSR_P4_IX_ESCR0			0x000003c8
#define MSR_P4_IX_ESCR1			0x000003c9
#define MSR_P4_MOB_ESCR0		0x000003aa
#define MSR_P4_MOB_ESCR1		0x000003ab
#define MSR_P4_MS_ESCR0			0x000003c0
#define MSR_P4_MS_ESCR1			0x000003c1
#define MSR_P4_PMH_ESCR0		0x000003ac
#define MSR_P4_PMH_ESCR1		0x000003ad
#define MSR_P4_RAT_ESCR0		0x000003bc
#define MSR_P4_RAT_ESCR1		0x000003bd
#define MSR_P4_SAAT_ESCR0		0x000003ae
#define MSR_P4_SAAT_ESCR1		0x000003af
#define MSR_P4_SSU_ESCR0		0x000003be
#define MSR_P4_SSU_ESCR1		0x000003bf /* guess: not in manual */

#define MSR_P4_TBPU_ESCR0		0x000003c2
#define MSR_P4_TBPU_ESCR1		0x000003c3
#define MSR_P4_TC_ESCR0			0x000003c4
#define MSR_P4_TC_ESCR1			0x000003c5
#define MSR_P4_U2L_ESCR0		0x000003b0
#define MSR_P4_U2L_ESCR1		0x000003b1

/* Netburst (P4) last-branch recording */
#define MSR_P4_LER_FROM_LIP 		0x000001d7
#define MSR_P4_LER_TO_LIP 		0x000001d8
#define MSR_P4_LASTBRANCH_TOS		0x000001da
#define MSR_P4_LASTBRANCH_0		0x000001db
#define NUM_MSR_P4_LASTBRANCH		4
#define MSR_P4_LASTBRANCH_0_FROM_LIP	0x00000680
#define MSR_P4_LASTBRANCH_0_TO_LIP	0x000006c0
#define NUM_MSR_P4_LASTBRANCH_FROM_TO	16

/* Pentium M (and Core) last-branch recording */
#define MSR_PM_LASTBRANCH_TOS		0x000001c9
#define MSR_PM_LASTBRANCH_0		0x00000040
#define NUM_MSR_PM_LASTBRANCH		8

/* Core 2 last-branch recording */
#define MSR_C2_LASTBRANCH_TOS		0x000001c9
#define MSR_C2_LASTBRANCH_0_FROM_IP	0x00000040
#define MSR_C2_LASTBRANCH_0_TO_IP	0x00000060
#define NUM_MSR_C2_LASTBRANCH_FROM_TO	4

/* Intel Core-based CPU performance counters */
#define MSR_CORE_PERF_FIXED_CTR0	0x00000309
#define MSR_CORE_PERF_FIXED_CTR1	0x0000030a
#define MSR_CORE_PERF_FIXED_CTR2	0x0000030b
#define MSR_CORE_PERF_FIXED_CTR_CTRL	0x0000038d
#define MSR_CORE_PERF_GLOBAL_STATUS	0x0000038e
#define MSR_CORE_PERF_GLOBAL_CTRL	0x0000038f
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390

/* Geode defined MSRs */
#define MSR_GEODE_BUSCONT_CONF0		0x00001900

#endif /* __ASM_MSR_INDEX_H */
