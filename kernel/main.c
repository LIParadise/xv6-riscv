#include <stdbool.h>

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

static atomic_bool         started              = false;
static atomic_bool         kaslr_done           = false;
static uintptr_t           kaslr_offset         = 0;
static atomic_uint_fast8_t harts_yet_done_kaslr = 0;

#define kaslr_hack_sp()                                                      \
    do                                                                       \
    {                                                                        \
        __asm__ volatile("add sp, %0, sp" : : "r"(kaslr_offset) : "memory"); \
    } while (0)

// start() jumps here in supervisor mode on all CPUs.
void main()
{
    if (cpuid() == 0)
    {
        consoleinit();
        printfinit();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");

        /*
         * physical page allocator, also KASLR
         * internally handles `ra` s.t. it won't return here, but relocated version of the `main` function
         */
        kinit_kaslr(&kaslr_offset, &kaslr_done, &harts_yet_done_kaslr);

        /*
         * After KASLR, HART 0 "returns" here: it had hacked its `ra`.
         * Set the `sp` to the new location before reclaiming the old RAM.
         */
        kaslr_hack_sp();
        /* TODO: free the old RAM */
        kvminit(kaslr_offset); // create kernel page table
        kvminithart();         // turn on paging
        procinit();            // process table
        trapinit();            // trap vectors
        trapinithart();        // install kernel trap vector
        plicinit();            // set up interrupt controller
        plicinithart();        // ask PLIC for device interrupts
        binit();               // buffer cache
        iinit();               // inode table
        fileinit();            // file table
        virtio_disk_init();    // emulated hard disk
        userinit();            // first user process
        atomic_store_explicit(&started, true, memory_order_release);
    }
    else
    {
        if (atomic_load_explicit(&kaslr_done, memory_order_acquire))
        {
            /*
             * We're in relocated kernel.
             * Modify stack pointer since main thread is gonna reclaim those memory.
             */
            kaslr_hack_sp();
            /*
             * HART 0 should have set this value to 1 less than exact number of HARTs on the system.
             * Notify HART 0 we've relocated and set stack pointer,
             * s.t. it may reclaim the memory occupied by original kernel at will.
             */
            atomic_fetch_sub_explicit(&harts_yet_done_kaslr, 1, memory_order_release);
            while (!atomic_load_explicit(&started, memory_order_acquire))
            {
                /* wait for other misc initialization tasks by HART 0 */
            }
            printf("hart %d starting\n", cpuid());
            kvminithart();  // turn on paging
            trapinithart(); // install kernel trap vector
            plicinithart(); // ask PLIC for device interrupts
        }
        else
        {
            while (0 == atomic_load_explicit(&harts_yet_done_kaslr, memory_order_acquire))
            {
                /* wait for HART 0 to prepare KASLR `kaslr_offset` */
            }
            void (*relocated_main)(void) = (void *)(((uint64)(void *)main) + kaslr_offset);
            relocated_main();
        }
    }

    scheduler();
}
