# Chapter 4

## Questions

- Why `trapframe`? In RISCV we have access to user space `pc` (`epc`) and `sp`, so we may just push the stack and place them there?
    - In other words, why trapframe for user space into supervisor mode, but kernel itself just uses `kstack`?
    - Generic C ABI mostly push the stack before access, i.e. however long it pushes, however long the stack belongs to the current routine, so coding a trap handler that pushes further and modify things there should cause no harm, no?
        - This includes passing large objects to functions: the caller pushes large chunks of stack beforehand, and pass it as pointer to the callee.
    - Maybe user stack size?
    - Safety?
        - Should be no harm since we store only user space information in the trapframe, which are all accessible right in user space anyway
        - ...except maybe it's not the case, e.g. where the trap handler `usertrap` locates is indeed in the trapframe...
