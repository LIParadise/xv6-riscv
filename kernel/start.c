#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__((aligned(16))) char stack0[4096 * NCPU];

/*
 * Note that this alternative version won't work:
 * in `kernel/entry.S`, we use `la` RISC-V instruction,
 * which loads the address of symbol,
 * so with the following snippet, `stack0` on `.data` like this:
 *
 * ``` bash
 * localhost ~/xv6-riscv: riscv64-elf-objdump -t kernel/kernel | grep stack
 * 0000000080007910 g     O .bss   0000000000008000 __priv_stack
 * 0000000080007880 g     O .data  0000000000000008 stack0
 * 0000000080001736 g     F .text  000000000000009e proc_mapstacks
 * ```
 *
 * ``` asm
 * // kernel/kernel.asm
 * 0000000080000000 <_entry>:
 *     80000000:	00008117          	auipc	sp,0x8
 *     80000004:	88010113          	addi	sp,sp,-1920 # 80007880 <stack0>
 *     80000008:	6505                	lui	a0,0x1
 *     8000000a:	f14025f3          	csrr	a1,mhartid
 *     8000000e:	0585                	addi	a1,a1,1
 *     80000010:	02b50533          	mul	a0,a0,a1
 *     80000014:	912a                	add	sp,sp,a0
 *     80000016:	04e000ef          	jal	80000064 <start>
 * ```
 *
 * But the real stack space lives on the `.bss` section!!!
 */
/*
__attribute__((aligned(16))) char __priv_stack[4096 * NCPU];
char                             *stack0 = __priv_stack;
*/

// entry.S jumps here in machine mode on stack0.
void start()
{
    // set M Previous Privilege mode to Supervisor, for mret.
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);

    // set M Exception Program Counter to main, for mret.
    // requires gcc -mcmodel=medany
    w_mepc((uint64)main);

    // disable paging for now.
    // `satp`: supervisor address translation and protection
    w_satp(0);

    // delegate all interrupts and exceptions to supervisor mode.
    w_medeleg(0xffff);
    w_mideleg(0xffff);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

    // configure Physical Memory Protection to give supervisor mode
    // access to all of physical memory.
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    // ask for clock interrupts.
    timerinit();

    // keep each CPU's hartid in its tp register, for cpuid().
    int id = r_mhartid();
    w_tp(id);

    // switch to supervisor mode and jump to main().
    __asm__ volatile("mret" ::: "memory");
}

// ask each hart to generate timer interrupts.
void timerinit()
{
    // enable supervisor-mode timer interrupts.
    w_mie(r_mie() | MIE_STIE);

    // enable the sstc extension (i.e. stimecmp).
    w_menvcfg(r_menvcfg() | (1L << 63));

    // allow supervisor to use stimecmp and time.
    w_mcounteren(r_mcounteren() | 2);

    // ask for the very first timer interrupt.
    w_stimecmp(r_time() + 1000000);
}
