#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/**
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

/**
 * kernel.ld sets this to end of kernel `.text` (page aligned).
 */
extern const char etext[];
/**
 * kernel.ld sets this to end of kernel `.text`, `.data`, and `.rodata`.
 * Not aligned.
 */
extern const char kernel_end_marked_by_ld[];

extern char trampoline[]; // trampoline.S

/**
 * Make a direct-map page table for the kernel.
 *
 * The KASLR offset is kinda tricky here:
 * we compiled with `-static-pie` and linked with `-pie` and `--no-dynamic-linker`,
 * s.t. all the symbols are resolved using relative addresses directedly encoded in asm,
 * s.t. no dynamic linker is required as loading.
 *
 * This has a side-effect, though:
 * all the `.data`/`.rodata` are also accessed by code in `.text` via relative addressing,
 * in fact the same treatment applies to also the linker defined (`PROVIDE`) symbols.
 *
 * In our KASLR implementation, when we're in this function,
 * the KASLR had been done and we're in relocated kernel,
 * in particular they are offset!
 */
pagetable_t kvmmake(const uintptr_t kaslr_offset)
{
    pagetable_t kpgtbl;

    kpgtbl = (pagetable_t)kalloc();
    memset(kpgtbl, 0, PGSIZE);

    // uart registers
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PLIC
    kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

    {
        // KASLR: repurpose/reclaim memory occupied by old kernel,
        // after which map them as regular memory
        printf("debug: KASLR offset %lu == 0x%llx, pages %lu\n", kaslr_offset, (unsigned long long)kaslr_offset,
               sys_get_free_pages());
        freerange((void *)(uintptr_t)KERNBASE,
                  GENERIC_PTR_SUB(void *, PGROUNDUP((uintptr_t)kernel_end_marked_by_ld), kaslr_offset));
        printf("debug: KASLR offset %lu == 0x%llx, pages %lu\n", kaslr_offset, (unsigned long long)kaslr_offset,
               sys_get_free_pages());
        kvmmap(kpgtbl, KERNBASE, KERNBASE, kaslr_offset, PTE_R | PTE_W);
    }

    // map KASLR relocated kernel text executable and read-only.
    kvmmap(kpgtbl, kaslr_offset + KERNBASE, kaslr_offset + KERNBASE, (uint64)etext - (kaslr_offset + KERNBASE),
           PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    kvmmap(kpgtbl, (uintptr_t)etext, (uintptr_t)etext, (uintptr_t)PHYSTOP - (uintptr_t)etext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // allocate and map a kernel stack for each process.
    proc_mapstacks(kpgtbl);

    if (!kmem_sane_check() || !kmem_sane_check())
    {
        panic("kmem insane: kvmmake");
    }

    return kpgtbl;
}

/**
 * Non-reentrant function: only called once after boot.
 *
 * Initialize the one `kernel_pagetable` after reclaiming the ram occupied by the old kernel (KASLR).
 */
void kvminit(const uintptr_t kaslr_offset)
{
    kernel_pagetable = kvmmake(kaslr_offset);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
    // wait for any previous writes to the page table memory to finish.
    sfence_vma();

    w_satp(MAKE_SATP(kernel_pagetable));

    // flush stale entries from the TLB.
    sfence_vma();
}

/**
 * Return the address of the PTE in page table pagetable
 * that corresponds to virtual address va.
 * If alloc != 0, create any required page-table pages.
 *
 * N.B.
 * 1. The fresh L0 page table is `memset` to all zero,
 *    thus if user add new pages to the page table via only this function,
 *    and that if user is sure that this input VA must not be in the table,
 *    user may check the `PTE_V` bit of the returned PTE:
 *    if that bit is set, it's an error in `kalloc`, giving out aliased memory.
 * 2. Return `NULL` if page absent and either of the following:
 *    a. alloc flag not set
 *    b. `kalloc` failed, probably because out of memory
 *
 * The risc-v Sv39 scheme has three levels of page-table
 * pages. A page-table page contains 512 64-bit PTEs.
 * A 64-bit virtual address is split into five fields:
 *   39..63 -- must be zero.
 *   30..38 -- 9 bits of level-2 index.
 *   21..29 -- 9 bits of level-1 index.
 *   12..20 -- 9 bits of level-0 index.
 *   0..11 -- 12 bits of byte offset within the page.
 */
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V)
        {
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(kpgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

/**
 * Create PTEs for virtual addresses starting at va that refer to
 * physical addresses starting at pa.
 *
 * va and size MUST be page-aligned.
 *
 * Returns 0 on success,
 * non-zero if walk() couldn't allocate a needed page-table page,
 * panic if VA of the `pagetable_t` leads to a remap.
 */
int mappages(pagetable_t pagetable, const uint64 va, const uint64 size, const uint64 pa, const int perm)
{
    if ((va % PGSIZE) != 0)
        panic("mappages: va not aligned");

    if ((size % PGSIZE) != 0)
        panic("mappages: size not aligned");

    if (size == 0)
        panic("mappages: size");

    pte_t *pte;
    for (uint64 v = va, p = pa; v < va + size; v += PGSIZE, p += PGSIZE)
    {
        if (0 == (pte = walk(pagetable, v, 1)))
            return -1;
        if (*pte & PTE_V)
            panic("mappages: remap");
        *pte = PA2PTE(p) | perm | PTE_V;
    }
    return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
    uint64 a;
    pte_t *pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
    {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if ((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free)
        {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
    }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate()
{
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
    char *mem;

    if (sz >= PGSIZE)
        panic("uvmfirst: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);

    // this function is only used during boot,
    // and source is `initcode.S` assembly from kernel `.data`,
    // so no aliasing shall ever occur: `kmem` shall not contain kernel code/data.
    memcpy(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
    char  *mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE)
    {
        mem = kalloc();
        if (mem == 0)
        {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
        {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
    {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++)
    {
        const pte_t pte = pagetable[i];
        if (pte & PTE_V) // `!PTE_V` deemed as not allocated in XV6
        {
            if (pte & (PTE_X | PTE_R | PTE_W))
            {
                // By Sv39 standard,
                // 1. leaf iff `PTE_X || PTE_R`, which is assumed to be absent in this function
                // 2. valid PTE cannot be `(!PTE_R) && PTE_W`
                //
                // This function assumes no leave and PTEs are valid,
                // thus the check.
                panic("freewalk: invalid PTE or leaf");
            }
            else
            {
                // this PTE points to a lower-level page table.
                //
                // Sv39 requires hardware to check tree traversal depth,
                // issuing page faults if deeper than expected;
                // we omit such mechanism here.
                const uint64 child = PTE2PA(pte);
                freewalk((pagetable_t)child);
                pagetable[i] = 0;
            }
        }
    }
    kfree((void *)pagetable);
}

/**
 * Free user memory pages, then free page-table pages.
 *
 * See also `proc_freepagetable` in `kernel/proc.c`:
 * it handles the trampoline and trapframe,
 * and we care only about the user memory (`PTE_U`).
 */
void uvmfree(pagetable_t pagetable, uint64 sz)
{
    if (sz > 0)
    {
        /*
         * This is how much we kernel had given to the process
         * (exclude trampoline and trapframe):
         * XV6 isn't the best in either functionality, space efficiency, or speed,
         * as it always puts `.text` at process VA 0,
         * and eagerly create every page the process had asked for (no CoW).
         *
         * In turn, freeing the PTE leaves is as simple as this one single call.
         */
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    }
    freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t *pte;
    uint64 pa, i;
    uint   flags;
    char  *mem;

    for (i = 0; i < sz; i += PGSIZE)
    {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa    = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if ((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char *)pa, PGSIZE);
        if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
        {
            kfree(mem);
            goto err;
        }
    }
    return 0;

err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;
    pte_t *pte;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA)
            return -1;
        pte = walk(pagetable, va0, 0);
        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
            return -1;
        pa0 = PTE2PA(*pte);
        n   = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    uint64 n, va0, pa0;
    int    got_null = 0;

    while (got_null == 0 && max > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0)
        {
            if (*p == '\0')
            {
                *dst     = '\0';
                got_null = 1;
                break;
            }
            else
            {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}
