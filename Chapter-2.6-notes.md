## Chapter 2.6

### On the `main` function

About why `void main(void)` since according to the C standard there's only two ways: either `int main(void)` or `int main(int argc, char* argv[])`?
[reddit](https://www.reddit.com/r/C_Programming/comments/qevpih/comment/hhvyxxd)

> 5.1.2.2.1 Program startup
> The function called at program startup is named `main`. The implementation declares no prototype for this function. It shall be defined with a return type of `int` and with no parameters: `int main(void) { /* ... */ }`
> or with two parameters (referred to here as `argc` and `argv`, though any names may be used, as they are local to the function in which they are declared): `int main(int argc, char *argv[]) { /* ... */ }`
> or equivalent; 9) or in some other implementation-defined manner.

Well it's since we're writing OS on basically bare metal: there's no where meaningful for the function to return anyway.

> 5.1.2.1 Freestanding environment
> 1. In a freestanding environment (in which C program execution may take place without any benefit of an operating system), the name and type of the function called at program startup are implementation-defined. \[...\]
> 2. The effect of program termination in a freestanding environment is implementation-defined.

### How does the kernel bootstrap itself?

First, note that QEMU bootloader jumps to fixed location in memory, in particular `0x80_000_000`, as documented in both `kernel/entry.S` and `kernel/memlayout.h`, so we need to put our first line of XV6 instruction there, which is done via **linker script** `kernel/kernel.ld`, when we're still in RISC-V machine mode.

The first thing we do is **setup the stack** s.t. we may run C code: we set all the CPU's stack to be inside some **global variable** in `kernel/start.c`: note that this space is taken into consideration by `kernel/kernel.ld` in the `.bss` section: the original XV6 source didn't bother to declare them `static`, which is nice, for the following reason...

But wait, how did kernel manage all the physical pages, if `pc` register is pointing to god knows nowhere...? Well, in that case sure we cannot do nothing, but hey, remember we've setup the `sp` i.e. where stack is in the `kernel/entry.S`? **The linker script `kernel/kernel.ld` had set that variable, thus our stack, to VMA _less_ then our physical memory starting point i.e. kernel internal code end!** And all the functions are similarly placed **before**, well, kernel end marker, so as long as we don't go stack overflow, the actual physical pages that user space would use later are **not ever touched** even after `kinit` (`kernel/kalloc.c`)!

### global variable `kmem` in `kernel/kalloc.c`

Note that [according to the C89 standard 3.5.7](https://stackoverflow.com/questions/16015656), we note that:

> "Global variables" are defined at file scope, outside any function. All variables that are defined at file scope and all variables that are declared with the keyword `static` have something called **static storage duration**.
> If an object that has static storage duration is not initialized explicitly, it is initialized implicitly as if every member that has arithmetic type were assigned 0 and every member that has pointer type were assigned a null pointer constant.

In practice, `riscv64-elf-objdump -t kernel/kernel | grep kmem` shows that it's indeed in the `.bss` section, which is normally used just like described above: some variables that we initialize to zero by some initializer code right before your actual program got run.

In particular, it's still within `0x80_000_000..end` (see also `kernel/kernel.ld`), i.e. it lives outside of the pages manipulated by `kalloc`/`kfree`.
