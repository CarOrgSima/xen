/******************************************************************************
 * arch/x86/mm/shadow/private.h
 *
 * Shadow code that is private, and does not need to be multiply compiled.
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

#ifndef _XEN_SHADOW_PRIVATE_H
#define _XEN_SHADOW_PRIVATE_H

// In order to override the definition of mfn_to_page, we make sure page.h has
// been included...
#include <asm/page.h>
#include <xen/domain_page.h>
#include <asm/x86_emulate.h>
#include <asm/hvm/support.h>


/******************************************************************************
 * Levels of self-test and paranoia
 */

#define SHADOW_AUDIT_HASH           0x01  /* Check current hash bucket */
#define SHADOW_AUDIT_HASH_FULL      0x02  /* Check every hash bucket */
#define SHADOW_AUDIT_ENTRIES        0x04  /* Check this walk's shadows */
#define SHADOW_AUDIT_ENTRIES_FULL   0x08  /* Check every shadow */
#define SHADOW_AUDIT_ENTRIES_MFNS   0x10  /* Check gfn-mfn map in shadows */

#ifdef NDEBUG
#define SHADOW_AUDIT                   0
#define SHADOW_AUDIT_ENABLE            0
#else
#define SHADOW_AUDIT                0x15  /* Basic audit of all */
#define SHADOW_AUDIT_ENABLE         shadow_audit_enable
extern int shadow_audit_enable;
#endif

/******************************************************************************
 * Levels of optimization
 */

#define SHOPT_WRITABLE_HEURISTIC  0x01  /* Guess at RW PTEs via linear maps */
#define SHOPT_EARLY_UNSHADOW      0x02  /* Unshadow l1s on fork or exit */
#define SHOPT_FAST_FAULT_PATH     0x04  /* Fast-path MMIO and not-present */
#define SHOPT_PREFETCH            0x08  /* Shadow multiple entries per fault */
#define SHOPT_LINUX_L3_TOPLEVEL   0x10  /* Pin l3es on early 64bit linux */
#define SHOPT_SKIP_VERIFY         0x20  /* Skip PTE v'fy when safe to do so */
#define SHOPT_VIRTUAL_TLB         0x40  /* Cache guest v->p translations */
#define SHOPT_FAST_EMULATION      0x80  /* Fast write emulation */
#define SHOPT_OUT_OF_SYNC        0x100  /* Allow guest writes to L1 PTs */

#define SHADOW_OPTIMIZATIONS     0x1ff


/******************************************************************************
 * Debug and error-message output
 */

#define SHADOW_PRINTK(_f, _a...)                                     \
    debugtrace_printk("sh: %s(): " _f, __func__, ##_a)
#define SHADOW_ERROR(_f, _a...)                                      \
    printk("sh error: %s(): " _f, __func__, ##_a)
#define SHADOW_DEBUG(flag, _f, _a...)                                \
    do {                                                              \
        if (SHADOW_DEBUG_ ## flag)                                   \
            debugtrace_printk("shdebug: %s(): " _f, __func__, ##_a); \
    } while (0)

// The flags for use with SHADOW_DEBUG:
#define SHADOW_DEBUG_PROPAGATE         1
#define SHADOW_DEBUG_MAKE_SHADOW       1
#define SHADOW_DEBUG_DESTROY_SHADOW    1
#define SHADOW_DEBUG_A_AND_D           1
#define SHADOW_DEBUG_EMULATE           1
#define SHADOW_DEBUG_P2M               1
#define SHADOW_DEBUG_LOGDIRTY          0

/******************************************************************************
 * Tracing
 */
DECLARE_PER_CPU(uint32_t,trace_shadow_path_flags);

#define TRACE_SHADOW_PATH_FLAG(_x)                      \
    do {                                                \
        this_cpu(trace_shadow_path_flags) |= (1<<(_x));      \
    } while(0)

#define TRACE_CLEAR_PATH_FLAGS                  \
    this_cpu(trace_shadow_path_flags) = 0

enum {
    TRCE_SFLAG_SET_AD,
    TRCE_SFLAG_SET_A,
    TRCE_SFLAG_SHADOW_L1_GET_REF,
    TRCE_SFLAG_SHADOW_L1_PUT_REF,
    TRCE_SFLAG_L2_PROPAGATE,
    TRCE_SFLAG_SET_CHANGED,
    TRCE_SFLAG_SET_FLUSH,
    TRCE_SFLAG_SET_ERROR,
    TRCE_SFLAG_DEMOTE,
    TRCE_SFLAG_PROMOTE,
    TRCE_SFLAG_WRMAP,
    TRCE_SFLAG_WRMAP_GUESS_FOUND,
    TRCE_SFLAG_WRMAP_BRUTE_FORCE,
    TRCE_SFLAG_EARLY_UNSHADOW,
    TRCE_SFLAG_EMULATION_2ND_PT_WRITTEN,
    TRCE_SFLAG_EMULATION_LAST_FAILED,
    TRCE_SFLAG_EMULATE_FULL_PT,
    TRCE_SFLAG_PREALLOC_UNHOOK,
    TRCE_SFLAG_UNSYNC,
    TRCE_SFLAG_OOS_FIXUP_ADD,
    TRCE_SFLAG_OOS_FIXUP_EVICT,
};

/******************************************************************************
 * The shadow lock.
 *
 * This lock is per-domain.  It is intended to allow us to make atomic
 * updates to the software TLB that the shadow tables provide.
 * 
 * Specifically, it protects:
 *   - all changes to shadow page table pages
 *   - the shadow hash table
 *   - the shadow page allocator 
 *   - all changes to guest page table pages
 *   - all changes to the page_info->tlbflush_timestamp
 *   - the page_info->count fields on shadow pages
 *   - the shadow dirty bit array and count
 */
#ifndef CONFIG_SMP
#error shadow.h currently requires CONFIG_SMP
#endif

#define shadow_lock_init(_d)                                   \
    do {                                                       \
        spin_lock_init(&(_d)->arch.paging.shadow.lock);        \
        (_d)->arch.paging.shadow.locker = -1;                  \
        (_d)->arch.paging.shadow.locker_function = "nobody";   \
    } while (0)

#define shadow_locked_by_me(_d)                     \
    (current->processor == (_d)->arch.paging.shadow.locker)

#define shadow_lock(_d)                                                       \
    do {                                                                      \
        if ( unlikely((_d)->arch.paging.shadow.locker == current->processor) )\
        {                                                                     \
            printk("Error: shadow lock held by %s\n",                         \
                   (_d)->arch.paging.shadow.locker_function);                 \
            BUG();                                                            \
        }                                                                     \
        spin_lock(&(_d)->arch.paging.shadow.lock);                            \
        ASSERT((_d)->arch.paging.shadow.locker == -1);                        \
        (_d)->arch.paging.shadow.locker = current->processor;                 \
        (_d)->arch.paging.shadow.locker_function = __func__;                  \
    } while (0)

#define shadow_unlock(_d)                                              \
    do {                                                               \
        ASSERT((_d)->arch.paging.shadow.locker == current->processor); \
        (_d)->arch.paging.shadow.locker = -1;                          \
        (_d)->arch.paging.shadow.locker_function = "nobody";           \
        spin_unlock(&(_d)->arch.paging.shadow.lock);                   \
    } while (0)


/* Size (in bytes) of a guest PTE */
#if GUEST_PAGING_LEVELS >= 3
# define GUEST_PTE_SIZE 8
#else
# define GUEST_PTE_SIZE 4
#endif

/******************************************************************************
 * Auditing routines 
 */

#if SHADOW_AUDIT & SHADOW_AUDIT_ENTRIES_FULL
extern void shadow_audit_tables(struct vcpu *v);
#else
#define shadow_audit_tables(_v) do {} while(0)
#endif

/******************************************************************************
 * Macro for dealing with the naming of the internal names of the
 * shadow code's external entry points.
 */
#define SHADOW_INTERNAL_NAME_HIDDEN(name, guest_levels) \
    name ## __guest_ ## guest_levels
#define SHADOW_INTERNAL_NAME(name, guest_levels)        \
    SHADOW_INTERNAL_NAME_HIDDEN(name, guest_levels)

#define GUEST_LEVELS  2
#include "multi.h"
#undef GUEST_LEVELS

#define GUEST_LEVELS  3
#include "multi.h"
#undef GUEST_LEVELS

#if CONFIG_PAGING_LEVELS == 4
#define GUEST_LEVELS  4
#include "multi.h"
#undef GUEST_LEVELS
#endif /* CONFIG_PAGING_LEVELS == 4 */

/* Shadow type codes */
#define SH_type_none           (0U) /* on the shadow free list */
#define SH_type_min_shadow     (1U)
#define SH_type_l1_32_shadow   (1U) /* shadowing a 32-bit L1 guest page */
#define SH_type_fl1_32_shadow  (2U) /* L1 shadow for a 32b 4M superpage */
#define SH_type_l2_32_shadow   (3U) /* shadowing a 32-bit L2 guest page */
#define SH_type_l1_pae_shadow  (4U) /* shadowing a pae L1 page */
#define SH_type_fl1_pae_shadow (5U) /* L1 shadow for pae 2M superpg */
#define SH_type_l2_pae_shadow  (6U) /* shadowing a pae L2-low page */
#define SH_type_l2h_pae_shadow (7U) /* shadowing a pae L2-high page */
#define SH_type_l1_64_shadow   (8U) /* shadowing a 64-bit L1 page */
#define SH_type_fl1_64_shadow  (9U) /* L1 shadow for 64-bit 2M superpg */
#define SH_type_l2_64_shadow  (10U) /* shadowing a 64-bit L2 page */
#define SH_type_l2h_64_shadow (11U) /* shadowing a compat PAE L2 high page */
#define SH_type_l3_64_shadow  (12U) /* shadowing a 64-bit L3 page */
#define SH_type_l4_64_shadow  (13U) /* shadowing a 64-bit L4 page */
#define SH_type_max_shadow    (13U)
#define SH_type_p2m_table     (14U) /* in use as the p2m table */
#define SH_type_monitor_table (15U) /* in use as a monitor table */
#define SH_type_oos_snapshot  (16U) /* in use as OOS snapshot */
#define SH_type_unused        (17U)

/* 
 * What counts as a pinnable shadow?
 */

static inline int sh_type_is_pinnable(struct vcpu *v, unsigned int t) 
{
    /* Top-level shadow types in each mode can be pinned, so that they 
     * persist even when not currently in use in a guest CR3 */
    if ( t == SH_type_l2_32_shadow
         || t == SH_type_l2_pae_shadow
         || t == SH_type_l2h_pae_shadow 
         || t == SH_type_l4_64_shadow )
        return 1;

#if (SHADOW_OPTIMIZATIONS & SHOPT_LINUX_L3_TOPLEVEL) 
    /* Early 64-bit linux used three levels of pagetables for the guest
     * and context switched by changing one l4 entry in a per-cpu l4
     * page.  When we're shadowing those kernels, we have to pin l3
     * shadows so they don't just evaporate on every context switch.
     * For all other guests, we'd rather use the up-pointer field in l3s. */ 
    if ( unlikely((v->domain->arch.paging.shadow.opt_flags & SHOPT_LINUX_L3_TOPLEVEL) 
                  && CONFIG_PAGING_LEVELS >= 4
                  && t == SH_type_l3_64_shadow) )
        return 1;
#endif

    /* Everything else is not pinnable, and can use the "up" pointer */
    return 0;
}

/*
 * Definitions for the shadow_flags field in page_info.
 * These flags are stored on *guest* pages...
 * Bits 1-13 are encodings for the shadow types.
 */
#define SHF_page_type_mask \
    (((1u << (SH_type_max_shadow + 1u)) - 1u) - \
     ((1u << SH_type_min_shadow) - 1u))

#define SHF_L1_32   (1u << SH_type_l1_32_shadow)
#define SHF_FL1_32  (1u << SH_type_fl1_32_shadow)
#define SHF_L2_32   (1u << SH_type_l2_32_shadow)
#define SHF_L1_PAE  (1u << SH_type_l1_pae_shadow)
#define SHF_FL1_PAE (1u << SH_type_fl1_pae_shadow)
#define SHF_L2_PAE  (1u << SH_type_l2_pae_shadow)
#define SHF_L2H_PAE (1u << SH_type_l2h_pae_shadow)
#define SHF_L1_64   (1u << SH_type_l1_64_shadow)
#define SHF_FL1_64  (1u << SH_type_fl1_64_shadow)
#define SHF_L2_64   (1u << SH_type_l2_64_shadow)
#define SHF_L2H_64  (1u << SH_type_l2h_64_shadow)
#define SHF_L3_64   (1u << SH_type_l3_64_shadow)
#define SHF_L4_64   (1u << SH_type_l4_64_shadow)

#define SHF_32  (SHF_L1_32|SHF_FL1_32|SHF_L2_32)
#define SHF_PAE (SHF_L1_PAE|SHF_FL1_PAE|SHF_L2_PAE|SHF_L2H_PAE)
#define SHF_64  (SHF_L1_64|SHF_FL1_64|SHF_L2_64|SHF_L2H_64|SHF_L3_64|SHF_L4_64)

#define SHF_L1_ANY  (SHF_L1_32|SHF_L1_PAE|SHF_L1_64)

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
/* Marks a guest L1 page table which is shadowed but not write-protected.
 * If set, then *only* L1 shadows (SHF_L1_*) are allowed. 
 *
 * out_of_sync indicates that the shadow tables may not reflect the
 * guest tables.  If it is clear, then the shadow tables *must* reflect
 * the guest tables.
 *
 * oos_may_write indicates that a page may have writable mappings.
 *
 * Most of the time the flags are synonymous.  There is a short period of time 
 * during resync that oos_may_write is clear but out_of_sync is not.  If a 
 * codepath is called during that time and is sensitive to oos issues, it may 
 * need to use the second flag.
 */
#define SHF_out_of_sync (1u<<30)
#define SHF_oos_may_write (1u<<29)

#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) */

static inline int sh_page_has_multiple_shadows(struct page_info *pg)
{
    u32 shadows;
    if ( !(pg->count_info & PGC_page_table) )
        return 0;
    shadows = pg->shadow_flags & SHF_page_type_mask;
    /* More than one type bit set in shadow-flags? */
    return ( (shadows & ~(1UL << find_first_set_bit(shadows))) != 0 );
}

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) 
/* The caller must verify this is reasonable to call; i.e., valid mfn,
 * domain is translated, &c */
static inline int page_is_out_of_sync(struct page_info *p) 
{
    return (p->count_info & PGC_page_table)
        && (p->shadow_flags & SHF_out_of_sync);
}

static inline int mfn_is_out_of_sync(mfn_t gmfn) 
{
    return page_is_out_of_sync(mfn_to_page(mfn_x(gmfn)));
}

static inline int page_oos_may_write(struct page_info *p) 
{
    return (p->count_info & PGC_page_table)
        && (p->shadow_flags & SHF_oos_may_write);
}

static inline int mfn_oos_may_write(mfn_t gmfn) 
{
    return page_oos_may_write(mfn_to_page(mfn_x(gmfn)));
}
#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) */

/******************************************************************************
 * Various function declarations 
 */

/* Hash table functions */
mfn_t shadow_hash_lookup(struct vcpu *v, unsigned long n, unsigned int t);
void  shadow_hash_insert(struct vcpu *v, 
                         unsigned long n, unsigned int t, mfn_t smfn);
void  shadow_hash_delete(struct vcpu *v, 
                         unsigned long n, unsigned int t, mfn_t smfn);

/* shadow promotion */
void shadow_promote(struct vcpu *v, mfn_t gmfn, u32 type);
void shadow_demote(struct vcpu *v, mfn_t gmfn, u32 type);

/* Shadow page allocation functions */
void  shadow_prealloc(struct domain *d, u32 shadow_type, unsigned int count);
mfn_t shadow_alloc(struct domain *d, 
                    u32 shadow_type,
                    unsigned long backpointer);
void  shadow_free(struct domain *d, mfn_t smfn);

/* Install the xen mappings in various flavours of shadow */
void sh_install_xen_entries_in_l4(struct vcpu *v, mfn_t gl4mfn, mfn_t sl4mfn);

/* Update the shadows in response to a pagetable write from Xen */
int sh_validate_guest_entry(struct vcpu *v, mfn_t gmfn, void *entry, u32 size);

/* Update the shadows in response to a pagetable write from a HVM guest */
void sh_validate_guest_pt_write(struct vcpu *v, mfn_t gmfn, 
                                void *entry, u32 size);

/* Remove all writeable mappings of a guest frame from the shadows.
 * Returns non-zero if we need to flush TLBs. 
 * level and fault_addr desribe how we found this to be a pagetable;
 * level==0 means we have some other reason for revoking write access. */
extern int sh_remove_write_access(struct vcpu *v, mfn_t readonly_mfn,
                                  unsigned int level,
                                  unsigned long fault_addr);

/* Functions that atomically write PT/P2M entries and update state */
void shadow_write_p2m_entry(struct vcpu *v, unsigned long gfn, 
                            l1_pgentry_t *p, mfn_t table_mfn,
                            l1_pgentry_t new, unsigned int level);
int shadow_write_guest_entry(struct vcpu *v, intpte_t *p,
                             intpte_t new, mfn_t gmfn);
int shadow_cmpxchg_guest_entry(struct vcpu *v, intpte_t *p,
                               intpte_t *old, intpte_t new, mfn_t gmfn);

#if (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC)
/* Allow a shadowed page to go out of sync */
int sh_unsync(struct vcpu *v, mfn_t gmfn);

/* Pull an out-of-sync page back into sync. */
void sh_resync(struct vcpu *v, mfn_t gmfn);

void oos_fixup_add(struct vcpu *v, mfn_t gmfn, mfn_t smfn, unsigned long off);

int sh_remove_write_access_from_sl1p(struct vcpu *v, mfn_t gmfn,
                                     mfn_t smfn, unsigned long offset);

/* Pull all out-of-sync shadows back into sync.  If skip != 0, we try
 * to avoid resyncing where we think we can get away with it. */

void sh_resync_all(struct vcpu *v, int skip, int this, int others, int do_locking);

static inline void
shadow_resync_all(struct vcpu *v, int do_locking)
{
    sh_resync_all(v,
                  0 /* skip */,
                  1 /* this */,
                  1 /* others */,
                  do_locking);
}

static inline void
shadow_resync_current_vcpu(struct vcpu *v, int do_locking)
{
    sh_resync_all(v,
                  0 /* skip */,
                  1 /* this */, 
                  0 /* others */,
                  do_locking);
}

static inline void
shadow_sync_other_vcpus(struct vcpu *v, int do_locking)
{
    sh_resync_all(v,
                  1 /* skip */, 
                  0 /* this */,
                  1 /* others */,
                  do_locking);
}

void oos_audit_hash_is_present(struct domain *d, mfn_t gmfn);
mfn_t oos_snapshot_lookup(struct vcpu *v, mfn_t gmfn);

#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_OUT_OF_SYNC) */

/******************************************************************************
 * Flags used in the return value of the shadow_set_lXe() functions...
 */

/* We actually wrote something new to the shadow */
#define SHADOW_SET_CHANGED            0x1
/* Caller should flush TLBs to clear the old entry */
#define SHADOW_SET_FLUSH              0x2
/* Something went wrong: the shadow entry was invalid or refcount failed */
#define SHADOW_SET_ERROR              0x4


/******************************************************************************
 * MFN/page-info handling 
 */

/* Override macros from asm/page.h to make them work with mfn_t */
#undef mfn_to_page
#define mfn_to_page(_m) __mfn_to_page(mfn_x(_m))
#undef mfn_valid
#define mfn_valid(_mfn) __mfn_valid(mfn_x(_mfn))
#undef page_to_mfn
#define page_to_mfn(_pg) _mfn(__page_to_mfn(_pg))

/* Override pagetable_t <-> struct page_info conversions to work with mfn_t */
#undef pagetable_get_page
#define pagetable_get_page(x)   mfn_to_page(pagetable_get_mfn(x))
#undef pagetable_from_page
#define pagetable_from_page(pg) pagetable_from_mfn(page_to_mfn(pg))

#define backpointer(sp) _mfn(pdx_to_pfn((unsigned long)(sp)->v.sh.back))
static inline unsigned long __backpointer(const struct page_info *sp)
{
    switch (sp->u.sh.type)
    {
    case SH_type_fl1_32_shadow:
    case SH_type_fl1_pae_shadow:
    case SH_type_fl1_64_shadow:
        return sp->v.sh.back;
    }
    return pdx_to_pfn(sp->v.sh.back);
}

static inline int
sh_mfn_is_a_page_table(mfn_t gmfn)
{
    struct page_info *page = mfn_to_page(gmfn);
    struct domain *owner;
    unsigned long type_info;

    if ( !mfn_valid(gmfn) )
        return 0;

    owner = page_get_owner(page);
    if ( owner && shadow_mode_refcounts(owner) 
         && (page->count_info & PGC_page_table) )
        return 1; 

    type_info = page->u.inuse.type_info & PGT_type_mask;
    return type_info && (type_info <= PGT_l4_page_table);
}

// Provide mfn_t-aware versions of common xen functions
static inline void *
sh_map_domain_page(mfn_t mfn)
{
    return map_domain_page(mfn_x(mfn));
}

static inline void 
sh_unmap_domain_page(void *p) 
{
    unmap_domain_page(p);
}

static inline void *
sh_map_domain_page_global(mfn_t mfn)
{
    return map_domain_page_global(mfn_x(mfn));
}

static inline void 
sh_unmap_domain_page_global(void *p) 
{
    unmap_domain_page_global(p);
}

/******************************************************************************
 * Log-dirty mode bitmap handling
 */

extern void sh_mark_dirty(struct domain *d, mfn_t gmfn);

static inline int
sh_mfn_is_dirty(struct domain *d, mfn_t gmfn)
/* Is this guest page dirty?  Call only in log-dirty mode. */
{
    unsigned long pfn;
    mfn_t mfn, *l4, *l3, *l2;
    unsigned long *l1;
    int rv;

    ASSERT(shadow_mode_log_dirty(d));
    ASSERT(mfn_valid(d->arch.paging.log_dirty.top));

    /* We /really/ mean PFN here, even for non-translated guests. */
    pfn = get_gpfn_from_mfn(mfn_x(gmfn));
    /* Page sharing not supported for shadow domains */
    BUG_ON(SHARED_M2P(pfn));
    if ( unlikely(!VALID_M2P(pfn)) )
        return 0;
    
    if ( d->arch.paging.log_dirty.failed_allocs > 0 )
        /* If we have any failed allocations our dirty log is bogus.
         * Since we can't signal an error here, be conservative and
         * report "dirty" in this case.  (The only current caller,
         * _sh_propagate, leaves known-dirty pages writable, preventing
         * subsequent dirty-logging faults from them.)
         */
        return 1;

    l4 = map_domain_page(mfn_x(d->arch.paging.log_dirty.top));
    mfn = l4[L4_LOGDIRTY_IDX(pfn)];
    unmap_domain_page(l4);
    if ( !mfn_valid(mfn) )
        return 0;

    l3 = map_domain_page(mfn_x(mfn));
    mfn = l3[L3_LOGDIRTY_IDX(pfn)];
    unmap_domain_page(l3);
    if ( !mfn_valid(mfn) )
        return 0;

    l2 = map_domain_page(mfn_x(mfn));
    mfn = l2[L2_LOGDIRTY_IDX(pfn)];
    unmap_domain_page(l2);
    if ( !mfn_valid(mfn) )
        return 0;

    l1 = map_domain_page(mfn_x(mfn));
    rv = test_bit(L1_LOGDIRTY_IDX(pfn), l1);
    unmap_domain_page(l1);

    return rv;
}

/**************************************************************************/
/* Shadow-page refcounting. */

void sh_destroy_shadow(struct vcpu *v, mfn_t smfn);

/* Increase the refcount of a shadow page.  Arguments are the mfn to refcount, 
 * and the physical address of the shadow entry that holds the ref (or zero
 * if the ref is held by something else).  
 * Returns 0 for failure, 1 for success. */
static inline int sh_get_ref(struct vcpu *v, mfn_t smfn, paddr_t entry_pa)
{
    u32 x, nx;
    struct page_info *sp = mfn_to_page(smfn);

    ASSERT(mfn_valid(smfn));

    x = sp->u.sh.count;
    nx = x + 1;

    if ( unlikely(nx >= 1U<<26) )
    {
        SHADOW_PRINTK("shadow ref overflow, gmfn=%lx smfn=%lx\n",
                       __backpointer(sp), mfn_x(smfn));
        return 0;
    }
    
    /* Guarded by the shadow lock, so no need for atomic update */
    sp->u.sh.count = nx;

    /* We remember the first shadow entry that points to each shadow. */
    if ( entry_pa != 0 
         && !sh_type_is_pinnable(v, sp->u.sh.type)
         && sp->up == 0 ) 
        sp->up = entry_pa;
    
    return 1;
}


/* Decrease the refcount of a shadow page.  As for get_ref, takes the
 * physical address of the shadow entry that held this reference. */
static inline void sh_put_ref(struct vcpu *v, mfn_t smfn, paddr_t entry_pa)
{
    u32 x, nx;
    struct page_info *sp = mfn_to_page(smfn);

    ASSERT(mfn_valid(smfn));
    ASSERT(!(sp->count_info & PGC_count_mask));

    /* If this is the entry in the up-pointer, remove it */
    if ( entry_pa != 0 
         && !sh_type_is_pinnable(v, sp->u.sh.type)
         && sp->up == entry_pa ) 
        sp->up = 0;

    x = sp->u.sh.count;
    nx = x - 1;

    if ( unlikely(x == 0) ) 
    {
        SHADOW_ERROR("shadow ref underflow, smfn=%lx oc=%08x t=%#x\n",
                     mfn_x(smfn), sp->u.sh.count, sp->u.sh.type);
        BUG();
    }

    /* Guarded by the shadow lock, so no need for atomic update */
    sp->u.sh.count = nx;

    if ( unlikely(nx == 0) ) 
        sh_destroy_shadow(v, smfn);
}


/* Pin a shadow page: take an extra refcount, set the pin bit,
 * and put the shadow at the head of the list of pinned shadows.
 * Returns 0 for failure, 1 for success. */
static inline int sh_pin(struct vcpu *v, mfn_t smfn)
{
    struct page_info *sp;
    
    ASSERT(mfn_valid(smfn));
    sp = mfn_to_page(smfn);
    ASSERT(sh_type_is_pinnable(v, sp->u.sh.type));
    if ( sp->u.sh.pinned )
    {
        /* Already pinned: take it out of the pinned-list so it can go 
         * at the front */
        page_list_del(sp, &v->domain->arch.paging.shadow.pinned_shadows);
    }
    else
    {
        /* Not pinned: pin it! */
        if ( !sh_get_ref(v, smfn, 0) )
            return 0;
        sp->u.sh.pinned = 1;
    }
    /* Put it at the head of the list of pinned shadows */
    page_list_add(sp, &v->domain->arch.paging.shadow.pinned_shadows);
    return 1;
}

/* Unpin a shadow page: unset the pin bit, take the shadow off the list
 * of pinned shadows, and release the extra ref. */
static inline void sh_unpin(struct vcpu *v, mfn_t smfn)
{
    struct page_info *sp;
    
    ASSERT(mfn_valid(smfn));
    sp = mfn_to_page(smfn);
    ASSERT(sh_type_is_pinnable(v, sp->u.sh.type));
    if ( sp->u.sh.pinned )
    {
        sp->u.sh.pinned = 0;
        page_list_del(sp, &v->domain->arch.paging.shadow.pinned_shadows);
        sp->up = 0; /* in case this stops being a pinnable type in future */
        sh_put_ref(v, smfn, 0);
    }
}


/**************************************************************************/
/* PTE-write emulation. */

struct sh_emulate_ctxt {
    struct x86_emulate_ctxt ctxt;

    /* Cache of up to 31 bytes of instruction. */
    uint8_t insn_buf[31];
    uint8_t insn_buf_bytes;
    unsigned long insn_buf_eip;

    /* Cache of segment registers already gathered for this emulation. */
    unsigned int valid_seg_regs;
    struct segment_register seg_reg[6];

    /* MFNs being written to in write/cmpxchg callbacks */
    mfn_t mfn1, mfn2;

#if (SHADOW_OPTIMIZATIONS & SHOPT_SKIP_VERIFY)
    /* Special case for avoiding having to verify writes: remember 
     * whether the old value had its low bit (_PAGE_PRESENT) clear. */
    int low_bit_was_clear:1;
#endif
};

const struct x86_emulate_ops *shadow_init_emulation(
    struct sh_emulate_ctxt *sh_ctxt, struct cpu_user_regs *regs);
void shadow_continue_emulation(
    struct sh_emulate_ctxt *sh_ctxt, struct cpu_user_regs *regs);
struct segment_register *hvm_get_seg_reg(
    enum x86_segment seg, struct sh_emulate_ctxt *sh_ctxt);

#if (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB)
/**************************************************************************/
/* Virtual TLB entries 
 *
 * We keep a cache of virtual-to-physical translations that we have seen 
 * since the last TLB flush.  This is safe to use for frame translations, 
 * but callers need to re-check the actual guest tables if the lookup fails.
 * 
 * Lookups and updates are protected by a per-vTLB (and hence per-vcpu)
 * lock.  This lock is held *only* while reading or writing the table,
 * so it is safe to take in any non-interrupt context.  Most lookups
 * happen with v==current, so we expect contention to be low.
 */

#define VTLB_ENTRIES 13

struct shadow_vtlb {
    unsigned long page_number;      /* Guest virtual address >> PAGE_SHIFT  */
    unsigned long frame_number;     /* Guest physical address >> PAGE_SHIFT */
    uint32_t pfec;     /* PF error code of the lookup that filled this
                        * entry.  A pfec of zero means the slot is empty
                        * (since that would require us to re-try anyway) */
};

/* Call whenever the guest flushes hit actual TLB */
static inline void vtlb_flush(struct vcpu *v) 
{
    spin_lock(&v->arch.paging.vtlb_lock);
    memset(v->arch.paging.vtlb, 0, VTLB_ENTRIES * sizeof (struct shadow_vtlb));
    spin_unlock(&v->arch.paging.vtlb_lock);
}

static inline int vtlb_hash(unsigned long page_number)
{
    return page_number % VTLB_ENTRIES;
}

/* Put a translation into the vTLB, potentially clobbering an old one */
static inline void vtlb_insert(struct vcpu *v, unsigned long page,
                               unsigned long frame, uint32_t pfec)
{
    struct shadow_vtlb entry = 
        { .page_number = page, .frame_number = frame, .pfec = pfec };
    spin_lock(&v->arch.paging.vtlb_lock);
    v->arch.paging.vtlb[vtlb_hash(page)] = entry;
    spin_unlock(&v->arch.paging.vtlb_lock);
}

/* Look a translation up in the vTLB.  Returns INVALID_GFN if not found. */
static inline unsigned long vtlb_lookup(struct vcpu *v,
                                        unsigned long va, uint32_t pfec)
{
    unsigned long page_number = va >> PAGE_SHIFT;
    unsigned long frame_number = INVALID_GFN;
    int i = vtlb_hash(page_number);

    spin_lock(&v->arch.paging.vtlb_lock);
    if ( v->arch.paging.vtlb[i].pfec != 0
         && v->arch.paging.vtlb[i].page_number == page_number 
         /* Any successful walk that had at least these pfec bits is OK */
         && (v->arch.paging.vtlb[i].pfec & pfec) == pfec )
    {
        frame_number = v->arch.paging.vtlb[i].frame_number;
    }
    spin_unlock(&v->arch.paging.vtlb_lock);
    return frame_number;
}
#endif /* (SHADOW_OPTIMIZATIONS & SHOPT_VIRTUAL_TLB) */


#endif /* _XEN_SHADOW_PRIVATE_H */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
