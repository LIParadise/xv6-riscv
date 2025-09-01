#include <stdatomic.h>

// Mutual exclusion lock.
struct spinlock
{
        atomic_bool locked;

        // For debugging:
        char       *name; // Name of lock.
        struct cpu *cpu;  // The cpu holding the lock.
};
