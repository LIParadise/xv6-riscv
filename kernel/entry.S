        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0
        li a0, 1024*4
        # `csrr`: control status register read
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # jump to start() in start.c
        /*
         * note that so for hartid `n`, its `sp` is at `4096 * n`
         * in particular accessing what's on `sp` directly is **invalid**,
         * which is just normal behavior if you think about it:
         * if you're gonna push something onto the stack, say `uint64_t`,
         * you move the `sp` by `8`.
         */
        call start
spin:
        j spin
