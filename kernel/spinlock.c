// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include <stdatomic.h>

void initlock(struct spinlock *lk, char *name)
{
    // lock preparation should be done by HART 0,
    // and it should ensure all locks are initialized before signaling
    // other HARTs they may start using the lock.
    //
    // See also `kernel/main.c`
    atomic_store_explicit(&lk->locked, false, memory_order_relaxed);
    lk->name = name;
    lk->cpu  = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void acquire(struct spinlock *lk)
{
    push_off(); // disable interrupts to avoid deadlock.
    if (holding(lk))
        panic("acquire");

    // Single thread StoreStore compiler fence to ensure interrupt is disabled before we take the lock
    // https://stackoverflow.com/questions/79554522
    // [`zicsr`](https://github.com/riscv/riscv-isa-manual/blob/600b757e543a19b07d79b82120bd10b84f19912e/src/zicsr.adoc)
    // [`rvwmo`](https://github.com/riscv/riscv-isa-manual/blob/600b757e543a19b07d79b82120bd10b84f19912e/src/rvwmo.adoc)
    // Side effects of CSR accesses on RISC-V respect program order, in terms the order of instructions.
    // Interrupt enabled or not is a per-HART thing.
    // So what we need is telling the compiler not to mess around with the store operation,
    // in particular w.r.t. the later CAS lock acquisition.
    atomic_signal_fence(memory_order_release);

    // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
    //   a5 = 1
    //   s1 = &lk->locked
    //   amoswap.w.aq a5, a5, (s1)
    bool is_currently_locked = false;
    while (!atomic_compare_exchange_weak_explicit(&lk->locked, &is_currently_locked, true, memory_order_acquire,
                                                  memory_order_relaxed))
    {
        is_currently_locked = false;
    }

    // Record info about lock acquisition for holding() and debugging.
    lk->cpu = mycpu();
}

// Release the lock.
void release(struct spinlock *lk)
{
    if (!holding(lk))
        panic("release");

    lk->cpu = 0;
    atomic_store_explicit(&lk->locked, false, memory_order_release);

    // StoreStore compiler fence to ensure interrupt is enabled after we relinquish the lock.
    // The original implementation did have this (`__sync_synchronize`) semantics.
    atomic_signal_fence(memory_order_release);

    pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int holding(struct spinlock *lk)
{
    return atomic_load_explicit(&lk->locked, memory_order_relaxed) && lk->cpu == mycpu();
}

/**
 * push_off/pop_off are like intr_off()/intr_on() except that they are matched:
 * it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
 * are initially off, then push_off, pop_off leaves them off.
 *
 * N.B.
 * 1. This is per-CPU operation, assuming no SMP support,
 *    we don't need to care about concurrency,
 *    in particular all we have and all we need are sequenced before relationships.
 * 2. As long as we don't put this function inside some ISR,
 *    we don't really need to care about interrupt nesting.
 * 3. By default, RISC-V assumes no nesting interrupts,
 *    via deasserting `mie` (machine interrupt enable) upon interrupt.
 * 4. Unfortunately from comment in spinlock acquire,
 *    we know that XV6 did somehow has to worry about nested interrupts......
 *    but when exactly did we enable nested interrupts?
 * 5. Consider if this function were somehow reentrant concurrently,
 *    since the critical seciont i.e. the real write operation
 *    comes only after the interrupt is set to off,
 *    only the deepest interrupt writes the per-CPU data,
 *    and since no other interrupt had written anything yet,
 *    the deepest guy writes the correct values.
 */
void push_off(void)
{
    int old = intr_get();

    intr_off();
    if (mycpu()->noff == 0)
        mycpu()->intena = old;
    mycpu()->noff += 1;
}

/**
 * push_off/pop_off are like intr_off()/intr_on() except that they are matched:
 * it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
 * are initially off, then push_off, pop_off leaves them off.
 */
void pop_off(void)
{
    struct cpu *c = mycpu();
    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();
}
