// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include <stdatomic.h>

static void free_range_exclude_subrange(void *pa_start, void *pa_end, const void *const, const uint64);

/* check compiled assembly for safety */
#define KASLR_RA_OFFSET_FROM_SP                      "88"
#define FIXME_READ_DTS_INSTEAD_OF_HARDCODE_QEMU_CPUS (3u)

/**
 * first address after kernel,
 * defined by `kernel/kernel.ld`
 */
extern char kernel_end_marked_by_ld[];
/**
 * defined by `kernel/kernel.ld`,
 * where does `.rela.dyn` start?
 */
extern char rela_dyn_start[];
/**
 * defined by `kernel/kernel.ld`,
 * where does `.rela.dyn` end?
 */
extern char rela_dyn_end[];

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

struct rela_dyn
{
        void *symbol_address;
        union rela_type_t {
                void *__not_used_merely_for_alignment;
                /* https://ithelp.ithome.com.tw/articles/10196982 */
                enum rela_type_inner_t
                {
                    R_RISCV_NONE         = 0,  /* No relocation. */
                    R_RISCV_32           = 1,  /* Add 32 bit zero extended symbol value */
                    R_RISCV_64           = 2,  /* Add 64 bit symbol value. */
                    R_RISCV_RELATIVE     = 3,  /* Add load address of shared object. */
                    R_RISCV_COPY         = 4,  /* Copy data from shared object. */
                    R_RISCV_JUMP_SLOT    = 5,  /* Set GOT entry to code address. */
                    R_RISCV_TLS_DTPMOD32 = 6,  /* 32 bit ID of module containing symbol */
                    R_RISCV_TLS_DTPMOD64 = 7,  /* ID of module containing symbol */
                    R_RISCV_TLS_DTPREL32 = 8,  /* 32 bit relative offset in TLS block */
                    R_RISCV_TLS_DTPREL64 = 9,  /* Relative offset in TLS block */
                    R_RISCV_TLS_TPREL32  = 10, /* 32 bit relative offset in static TLS block */
                    R_RISCV_TLS_TPREL64  = 11, /* Relative offset in static TLS block */
                } rela_type_inner;
        } rela_type;
        void *symbol_value;
};

/*
 * TODO: move it to separate module since it's not really `kalloc` related
 */
uint64 krnd64()
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
 * Free the physical pages, except some of which would be skipped for they contain KASLR relocated kernel.
 *
 * Return the how much offset we moved the kernel during KASLR:
 * zero if too few memory to make KASLR work, kernel not moved,
 * non-zero if KASLR worked, and we may later free the space occupied by old kernel.
 */
static uintptr_t kinit_kaslr_worker()
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
        freerange((void *)kernel_end_marked_by_ld, (void *)PHYSTOP);
        return 0;
    }
    else
    {
        /*
         * how to fit 4 elements contiguously in array of length 5?
         * you have 2 choices: `0..=3` or `1..=4`
         * and (5-4+1) is 2.
         */
        kaslr_start =
            GENERIC_PTR_ADD(void *, free_ram_start, PGSIZE *(krnd64() % (free_pages - kernel_size_in_pages + 1)));
        free_range_exclude_subrange(kernel_end_marked_by_ld, (void *)PHYSTOP, kaslr_start, kernel_size_in_pages);
        if (!kmem_sane_check())
        {
            panic("kmem insane: kinit_kaslr (1st)");
        }
        if (!kmem_sane_check())
        {
            panic("kmem insane: kinit_kaslr (2nd)");
        }

        /*
         * copy kernel only after the allocator initialization:
         * this way relocated kernel's memory is already initialized!
         */
        memcpy(kaslr_start, (void *)KERNBASE, kernel_size_in_pages * PGSIZE);
        const uintptr_t kaslr_offset = ((uintptr_t)kaslr_start) - ((uintptr_t)KERNBASE);
        {
            /*
             * In general,
             * KASLR means the kernel has to somehow replicate most of the functionalities of a loader/dynamic linker,
             * specifically the relocations, if any, need to be resolved,
             * of course, if possible, just produce position independent code without any relocations.
             *
             * With compiler option `-static-pie` and linker `--no-dynamic-linker` and `-pie`,
             * we've avoided much of the work of a loader/dynamic linker,
             * but still, some relocations still need to be done,
             * in particular according to `readelf -r`, `.rela.dyn` is all we need for XV6.
             *
             * The main roadblock is due to pointers e.g. function pointers,
             * in particular the `syscalls` (`kernel/syscall.c`) array of function pointers:
             * either you left them all empty (and rely on loader reading `.rela.dyn` and resolve them while loading),
             * or you hardcode the addresses of the functions in,
             * either way we still need to resolve them.
             *
             * There's simply no "relative pointer" in C.
             *
             * In fact `-mcmodel=medany` without `-pie`/`-static-pie`/`--no-dynamic-linker` i.e. the default
             * option of XV6 is already basically position independent, as documented in GCC compiler option,
             * however it too faces the fact that `syscalls` is hardcoded addresses thus need to be adjusted.
             */
            for (const struct rela_dyn *p = (void *)rela_dyn_start; p < (struct rela_dyn *)(void *)rela_dyn_end; ++p)
            {
                if (R_RISCV_RELATIVE == p->rela_type.rela_type_inner)
                {
                    *GENERIC_PTR_ADD(void **, p->symbol_address, kaslr_offset) =
                        GENERIC_PTR_ADD(void *, p->symbol_value, kaslr_offset);
                }
                else
                {
                    panic("I don't know how to handle `.rela.dyn` other than `R_RISCV_RELATIVE`...");
                }
            }

            /*
             * Need to modify the lock for its pointee would got tainted after KASLR done
             * since we would later reclaim the pages on which the original kernel lives:
             * in particular the `name` field is string so in `.text`.
             *
             * We're the only running HART now (HART 0), so re-init lock is fine.
             */
            initlock(GENERIC_PTR_ADD(struct spinlock *, &kmem.lock, kaslr_offset),
                     GENERIC_PTR_ADD(char *, "kmem", kaslr_offset));
        }
        return kaslr_offset;
    }
}

/**
 * KASLR and populate (most) of the memory.
 * In particular the pages occupied by the initial kernel are not yet reclaimed.
 */
void kinit_kaslr(uintptr_t *const p_kaslr_offset, atomic_bool *const kaslr_done,
                 atomic_uint_fast8_t *const harts_yet_done_kaslr)
{
    uint64 kaslr_offset, ra, sp;
    /* store the return address for later we shall return to relocated kernel */
    __asm__ volatile("addi %0, ra, 0x0" : "=r"(ra)::"memory");
    __asm__ volatile("addi %0, sp, 0x0" : "=r"(sp)::"memory");

    kaslr_offset = kinit_kaslr_worker();
    if (0 == kaslr_offset)
    {
        panic("I don't want to handle KASLR corner case i.e. insufficient RAM for now...");
    }

    /* second copy for we HART 0 also need to modify stack pointer later */
    *p_kaslr_offset                                             = kaslr_offset;
    *GENERIC_PTR_ADD(uintptr_t *, p_kaslr_offset, kaslr_offset) = kaslr_offset;

    /* prepare flags before signaling other HARTs it's ok to jump */
    atomic_uint_fast8_t *relocated_hydk = GENERIC_PTR_ADD(atomic_uint_fast8_t *, harts_yet_done_kaslr, kaslr_offset);
    atomic_bool         *relocated_kd   = GENERIC_PTR_ADD(atomic_bool *, kaslr_done, kaslr_offset);
    atomic_store_explicit(relocated_kd, true, memory_order_release);
    atomic_store_explicit(relocated_hydk, FIXME_READ_DTS_INSTEAD_OF_HARDCODE_QEMU_CPUS - 1, memory_order_release);
    atomic_store_explicit(harts_yet_done_kaslr, FIXME_READ_DTS_INSTEAD_OF_HARDCODE_QEMU_CPUS - 1, memory_order_release);

    while (0 != atomic_load_explicit(relocated_hydk, memory_order_acquire))
    {
        /*
         * Wait for other HARTs till they are notified where to jump and had changed their stack pointer:
         * we're gonna repurpose the memory where original kernel used to live later.
         * Every byte counts!
         */
    }

    /*
     * Hack stored `ra` on `sp`,
     * s.t. we don't return to the original kernel (which would later be repurposed as free memory).
     * `KASLR_RA_OFFSET_FROM_SP` comes from inspecting the assembly: check RISC-V calling conventions and asm!
     *
     * Directly calling `"sd %0, KASLR_RA_OFFSET_FROM_SP(sp)"` somehow won't compile,
     * so another register is used.
     */
    ra += kaslr_offset;
    __asm__ volatile("sd %0, " KASLR_RA_OFFSET_FROM_SP "(%1)" : : "r"(ra), "r"(sp) : "memory");

    return;
}

void freerange(void *pa_start, const void *pa_end)
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
    freerange(GENERIC_PTR_ADD(void *, kaslr_start, kernel_size_in_pages *PGSIZE), pa_end);
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

    if (((uint64)pa % PGSIZE) != 0 || (uint64)pa >= PHYSTOP)
    {
        panic("kfree");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, -1, PGSIZE);

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
 * else it's PA/kernel direct VA, filled with junk.
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

/**
 * Basic sane check of memory allocator:
 * no pages shall appear twice in the allocator.
 */
bool kmem_sane_check(void)
{
    bool sanity = true;
    acquire(&kmem.lock);

    uint64                        actual_pages = 0;
    struct kmem_linked_list_node *node         = kmem.free_pages;
    while (node)
    {
        uint64_t *tag = ALIGN_UP(uint64_t *, GENERIC_PTR_ADD(void *, node, sizeof(struct kmem_linked_list_node)));
        if ((uint64_t)(-1) == *tag)
        {
            // `kfree` sets the memory to all bits `1`; mark as walked.
            *tag = actual_pages++;
            node = node->next;
        }
        else
        {
            sanity = false;
            break;
        }
    }

    if (sanity && actual_pages == kmem.num_pages)
    {
        // recover the original setup only if the allocator is OK,
        // since we shall panic if it's not OK anyway.
        for (node = kmem.free_pages; node; node = node->next)
        {
            *ALIGN_UP(uint64_t *, GENERIC_PTR_ADD(void *, node, sizeof(struct kmem_linked_list_node))) = (uint64_t)(-1);
        }
    }
    else
    {
        printf("sanity: %d\n", sanity);
        printf("claimed pages %lu, actual_pages %lu\n", kmem.num_pages, actual_pages);
        sanity = false;
    }

    release(&kmem.lock);
    return sanity;
}
