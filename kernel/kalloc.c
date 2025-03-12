// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

static void freerange(void *pa_start, const void *pa_end);
static void free_range_exclude_subrange(void *pa_start, void *pa_end, const void *const, const uint64);
#define KASLR_RA_OFFSET_FROM_SP                      "104"
#define FIXME_READ_DTS_INSTEAD_OF_HARDCODE_QEMU_CPUS (3u)

/**
 * first address after kernel,
 * defined by `kernel/kernel.ld`
 */
extern char kernel_end_marked_by_ld[];

struct kmem_linked_list_node
{
        struct kmem_linked_list_node *next;
};

struct
{
        struct spinlock               lock;
        struct kmem_linked_list_node *free_pages;
        uint64                        num_pages;
} kmem;

typedef void (*fp_void_to_void)();

/**
 * Naive PRNG implementation
 * Intended usage is for the HART zero to do KASLR upon boot.
 */
static uint64 krnd64()
{
    // initial seed should be s.t. LSB equal to 1
    // making sure upon the first call LFSR is already at work.
    static uint64 linear_feedback_shift_register = 0X4269114514ACCEED;
    static uint64 lfsr_feed                      = 0;

    if (!lfsr_feed)
    {
        uint64 time = r_time();
        uint32 prng = r_seed();
        lfsr_feed   = time ^ prng ^ (((uint64)prng) << 32);
    }

    // https://www.reddit.com/r/RISCV/comments/1cy8zs2/comment/l597uu3
    if (1 & linear_feedback_shift_register)
    {
        // LSB set
        // let's do linear feedback shift
        linear_feedback_shift_register = (linear_feedback_shift_register >> 1) ^ lfsr_feed;
    }
    else
    {
        // LSB not set,
        // note that this is equivalent to rotate
        linear_feedback_shift_register >>= 1;
    }

    return linear_feedback_shift_register;
}

/**
 * Free the physical pages, except some of which would be KASLR relocated kernel.
 *
 * Return the how much offset we moved the kernel during KASLR:
 * zero if too few memory to make KASLR work, kernel not moved,
 * non-zero if KASLR worked, and we may later free the space occupied by old kernel.
 */
static uint64 kinit_kaslr_worker()
{
    initlock(&kmem.lock, "kmem");

    const uint64 free_ram_start       = PGROUNDUP((uint64)kernel_end_marked_by_ld);
    const uint64 free_pages           = (PHYSTOP - free_ram_start) / PGSIZE;
    const uint64 kernel_size_in_pages = (free_ram_start - KERNBASE) / PGSIZE;
    void        *kaslr_start          = 0;
    if (free_pages < kernel_size_in_pages)
    {
        /*
         * kaslr without sufficient RAM is troublesome:
         * we would need some temporary scratch workspace for moving things around.
         *
         * Just don't do it since XV6 is already tiny,
         * unless you want to port XV6 onto some puny RISC-V platform.
         */
        freerange((void *)KERNBASE, (void *)free_ram_start);
        return 0;
    }
    else
    {
        kaslr_start = (void *)(free_ram_start + PGSIZE * (krnd64() % (free_pages - kernel_size_in_pages + 1)));
        free_range_exclude_subrange(kernel_end_marked_by_ld, (void *)PHYSTOP, kaslr_start, kernel_size_in_pages);
        /* copy kernel only after the allocator initialization! */
        memcpy(kaslr_start, (void *)KERNBASE, kernel_size_in_pages * PGSIZE);
        /*
         * Technically UB here:
         * Should've use `uintptr_t`; C99 defines it as integer s.t. `void *` -> `uintptr_t` -> `void *` is valid
         *
         * https://stackoverflow.com/a/57280960/25255815
         * > All that is guaranteed about conversions is that `(void*)p==(void*)(uintptr_t)p`.
         * > There isn't guarantee that `(uintptr_t)p == (uintptr_t)p`,
         * > nor even that a pointer produced through a round-trip cast can be meaningfully dereferenced in any way
         * whatsoever.
         */
        return ((uint64)kaslr_start) - ((uint64)KERNBASE);
    }
}

void kinit_kaslr(fp_void_to_void *relocated_main, atomic_bool *kaslr_done,
                 atomic_uint_fast8_t *cpus_yet_jumped_to_relocated_main)
{
    uint64 kaslr_offset, ra, sp;
    /* store the return address for later we shall return to relocated kernel */
    __asm__ volatile("addi %0, ra, 0x0" : "=r"(ra));
    __asm__ volatile("addi %0, sp, 0x0" : "=r"(sp));

    kaslr_offset = kinit_kaslr_worker();
    if (0 == kaslr_offset)
    {
        panic("I don't want to handle KASLR corner case i.e. insufficient RAM for now...");
    }
    extern void main();
    *relocated_main = (void *)(((uint64)(void *)main) + kaslr_offset);
    atomic_store_explicit(cpus_yet_jumped_to_relocated_main, FIXME_READ_DTS_INSTEAD_OF_HARDCODE_QEMU_CPUS - 1,
                          memory_order_release);
    atomic_store_explicit((atomic_bool *)(void *)(((uint64)(void *)kaslr_done) + kaslr_offset), true,
                          memory_order_release);

    while (0 != atomic_load_explicit(cpus_yet_jumped_to_relocated_main, memory_order_acquire))
    {
        /* wait for other HART to jump to relocated main */
    }

    /*
     * Hack stored `ra` on `sp`,
     * s.t. we don't return to the original kernel (which would later be repurposed as free memory).
     * `KASLR_RA_OFFSET_FROM_SP` comes from inspecting the assembly: check RISC-V calling conventions!
     *
     * Directly calling `"sd %0, KASLR_RA_OFFSET_FROM_SP(sp)"` somehow won't compile, so another register is used.
     * Luckily the resulting assembly doesn't introduce yet another move of `sp`.
     */
    __asm__ volatile("sd %0, " KASLR_RA_OFFSET_FROM_SP "(%1)" : : "r"(ra), "r"(sp));

    return;
}

static void freerange(void *pa_start, const void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

/**
 * Given start and end address,
 * for all the whole pages that lies inside `start..end` (in Rust terms)
 * (except a contiguous chunk which is meant for KASLR relocated kernel),
 * declare them as free memory.
 */
static void free_range_exclude_subrange(void *pa_start, void *pa_end, const void *const kaslr_start,
                                        const uint64 kernel_size_in_pages)
{
    freerange(pa_start, kaslr_start);
    freerange((void *)(kaslr_start + kernel_size_in_pages * PGSIZE), pa_end);
}

/**
 * Free the page of physical memory pointed at by pa,
 * which normally should have been returned by a call to `kalloc`.
 * (The exception is when initializing the allocator;
 * see `kinit` above.)
 *
 * Note that `kalloc` is protected by spinlock,
 * so it shall never give out the same memory,
 * so we do NOT bother checking if `pa` is already free!!!
 *
 * Memory corruption ensues if user freed memory twice!!!!!
 *
 * TODO
 * How does GNU/Linux typically catch such error?
 * Some sort of map kept by the C standard library?
 * In general how does Linux manage memory?
 * It can't possibly be just a global spinlock...
 */
void kfree(void *pa)
{
    struct kmem_linked_list_node *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < kernel_end_marked_by_ld || (uint64)pa >= PHYSTOP)
    {
        panic("kfree");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct kmem_linked_list_node *)pa;

    acquire(&kmem.lock);
    r->next         = kmem.free_pages;
    kmem.free_pages = r;
    kmem.num_pages += 1;
    release(&kmem.lock);
}

/**
 * Allocate one 4096-byte page of physical memory.
 *
 * Protected by spinlock,
 * s.t. so long as no one corrupts memory via `kfree`,
 * the memory given out from here never aliases.
 *
 * Return value is zero if cannot be allocated,
 * else it's pointer intended for kernel's own usage.
 */
void *kalloc(void)
{
    struct kmem_linked_list_node *r;

    acquire(&kmem.lock);
    r = kmem.free_pages;
    if (r)
    {
        kmem.free_pages = r->next;
        kmem.num_pages -= 1;
    }
    release(&kmem.lock);

    if (r)
    {
        memset((char *)r, 5, PGSIZE); // fill with junk
    }

    return (void *)r;
}

/**
 * How many memory (in pages) are there?
 *
 * N.B. this function is protected by spinlock.
 */
uint64 sys_get_free_pages(void)
{
    uint64 ret = 0;
    acquire(&kmem.lock);
    ret = kmem.num_pages;
    release(&kmem.lock);
    return ret;
}
