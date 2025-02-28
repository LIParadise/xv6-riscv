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

### Interrupts

Supposedly `push_off`/`pop_off` shall not be called in ISRs, since by default [RISC-V](https://www.reddit.com/r/RISCV/comments/jo0yba) should disable interrupt upon interrupt, so no nesting occurs unless the programmer specifies it to, which is kinda absurd in that case you'd like to toggle interrupt using such utilities.
Plus, by default, no interrupt nesting is enabled on RISC-V, so maybe just don't worry too much about `push_off`/`pop_off` reentrance for now.

#### 3.1.6.1 Privilege and Global Interrupt-Enable Stack in mstatus register, V20190608-Priv-MSU-Ratified

[reddit comment](https://www.reddit.com/r/RISCV/comments/jo0yba/comment/gb7fnri)
> So basically, for machine-mode-only, you have MIE (interrupt enable for machine mode) and MPIE (previous interrupt enable for machine mode). When you enter an interrupt (because MIE is enabled and an interrupt happened), MPIE saves the value of MIE and MIE is set to zero, to ensure no further interrupt happens while servicing the first.
> When returning from the interrupt service routine (via mret), MIE will be restored from MPIE, enabling interrupts again, so the next interrupt can go in and do its thing.
> Alternatively the interrupt service routine can check the MIP register (machine interrupt pending) and service pending interrupts before mret'ing to avoid having to service another interrupt immediately after returning.
> ...instruction-address-misaligned exception must occur at all time
> In this case the MEPC, MCAUSE etc. registers are overwritten. I guess you'll have to save those registers when entering your service routine and restore them before issuing your mret (if you cannot guarantee your servicing code won't issue traps or cause exceptions). This way your program stack saves your machine status for each nested trap and/or exception.
> After all, with unlimited potential for nested exceptions, all that machine state (for instance, the EPC) has to be stored *somewhere* and one certainly does not want to have an extensive hidden stack of registers in the hardware design.

[reddit comment](https://www.reddit.com/r/RISCV/comments/jo0yba/comment/gb86vtt)
> Interrupts are automatically disabled when an interrupt occurs. Usually, interrupt handlers just save some registers (including mepc), and then re-enable interrupts before continuing.
> yes, but exceptions can not be disabled. So if an interrupt happens and an exception occurs inside the interrupt will the system still write mpec?
> Yes. So don't cause any exceptions until you save mepc ;) This is a "double-fault" in intel-esque parlance. Usually, the OS just bails at that point.

#### Other Materials

[reddit post](https://www.reddit.com/r/RISCV/comments/fy09gs)
[five-embeddev](https://www.five-embeddev.com/code/2022/06/29/nested-interrupts/)

### RISC-V Atomics

[openhwgroup](https://docs.openhwgroup.org/projects/cva6-user-manual/01_cva6_user/RISCV_Instructions_RV32A.html)
> The two forms of atomic instruction provided are load-reserved/store-conditional instructions and atomic fetch-and-op memory instructions. Both types of atomic instruction support various memory consistency orderings including unordered, acquire, release, and sequentially consistent semantics.

### Generic Notes

[stack overflow](https://stackoverflow.com/questions/46803230/how-to-call-an-enum-in-an-asm-file#comment80553587_46803230)
> Another observation. The CDECL calling convention makes EAX, ECX and EDX volatile registers. Your functions can clobber those registers and you are fine. However EBX, ESI, EDI, EBP are non-volatile. If you change their values you must ensure you save them at the top of your function and then restore them at the bottom. Usually done by pushing the value to save and poping them to restore. Your program may seem to work but it is possible it may fail unexpectedly. I say this because I see a couple of functions you clobber EBX but you do nothing to save/restore its value.
