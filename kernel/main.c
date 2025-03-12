#include <stdbool.h>

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

static atomic_bool started                                   = false;
static atomic_bool kaslr_done                                = false;
void (*relocated_main)(void)                                 = 0;
static atomic_uint_fast8_t cpus_yet_jumped_to_relocated_main = 0;

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

        // physical page allocator, also KASLR
        // internally handles RA s.t. it won't return here, but relocated version of the `main` function
        kinit_kaslr(&relocated_main, &kaslr_done, &cpus_yet_jumped_to_relocated_main);
        /* TODO: free the old RAM */

        kvminit();          // create kernel page table
        kvminithart();      // turn on paging
        procinit();         // process table
        trapinit();         // trap vectors
        trapinithart();     // install kernel trap vector
        plicinit();         // set up interrupt controller
        plicinithart();     // ask PLIC for device interrupts
        binit();            // buffer cache
        iinit();            // inode table
        fileinit();         // file table
        virtio_disk_init(); // emulated hard disk
        userinit();         // first user process
        atomic_store_explicit(&started, true, memory_order_release);
    }
    else
    {
        if (atomic_load_explicit(&kaslr_done, memory_order_acquire))
        {
            /* we're in relocated kernel */
            while (!atomic_load_explicit(&started, memory_order_acquire))
            {
                /* wait for misc start tasks by HART 0 */
            }
            printf("hart %d starting\n", cpuid());
            kvminithart();  // turn on paging
            trapinithart(); // install kernel trap vector
            plicinithart(); // ask PLIC for device interrupts
        }
        else
        {
            while (0 == atomic_load_explicit(&cpus_yet_jumped_to_relocated_main, memory_order_acquire))
            {
                /*
                 * wait for HART 0 to prepare KASLR `relocated_main`:
                 * it should set to 1 less than `NCPU`.
                 */
            }
            atomic_fetch_sub_explicit(&cpus_yet_jumped_to_relocated_main, 1, memory_order_release);
            relocated_main();
        }
    }

    scheduler();
}
