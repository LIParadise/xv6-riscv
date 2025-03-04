// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

static void freerange(void *pa_start, void *pa_end);

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

void kinit()
{
    initlock(&kmem.lock, "kmem");
    freerange(kernel_end_marked_by_ld, (void *)PHYSTOP);
}

/**
 * Given start and end address,
 * for all the whole pages that lies inside `start..end` (in Rust terms),
 * declare them as free memory.
 */
static void freerange(void *pa_start, void *pa_end)
{
    for (char *p = (char *)PGROUNDUP((uint64)pa_start); p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    {
        kfree(p);
    }
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
 * KASLR needs some free space to relocate the kernel `.text`, `.data`, etc.
 *
 * Assuming we had just boot, just after `kinit`,
 * we know the linked list contains all the available memory,
 * one by one from end of physical memory to kernel end,
 * i.e. this contiguous whole chunk of PA is recorded in reverse in `kmem`.
 *
 * Thus we may allocate contiguous pages of memory
 * via modifying the linked list of PA directly.
 *
 * Return value:
 * If these criteria are met,
 *   - called only once per boot immediately following `kinit`
 *   - `kalloc` yet ever been called
 *   - queried contiguous chunk of memory lies within the free memory
 * Then the queried chunk of memory would be returned.
 *
 * Else undefined.
 */
void *kaslr_alloc(uint64 pa_start, uint64 pages)
{
    static uint8 called = 0;
    if ((!called) && (pa_start >= PGROUNDUP((uint64)kernel_end_marked_by_ld)) && (pages <= kmem.num_pages) &&
        (0 == pa_start % PGSIZE))
    {
        called                             = 1;
        struct kmem_linked_list_node *tail = ((const struct kmem_linked_list_node *)(void *)pa_start)->next;

        if (((PHYSTOP - pa_start) / PGSIZE) > pages)
        {
            ((struct kmem_linked_list_node *)(void *)(pa_start + PGSIZE * pages))->next = tail;
        }
        else
        {
            kmem.free_pages = tail;
        }

        return (void *)pa_start;
    }
    else
    {
        panic((void *)__func__);
        for (;;)
        {
        }
    }
    return 0;
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
