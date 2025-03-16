# Notes

## Sv39 Virtual Address Translation Scheme

### [10.3.2. Virtual Address Translation Process](priv-isa-asciidoc.pdf)

During translation, if `((!PTE_V) || (PTE_W && (!PTE_R)) || /* any reserved bit */)`, then a page fault is generated, otherwise the PTE is valid.
If valid, and if `PTE_R || PTE_X`, then it's deemed as leaf page, otherwise check depth, if yet reached max depth for the architecture, continue, else page fault.

So no, RISC-V Sv39 doesn't support recursive paging scheme:
say we have `512` contiguous physical pages starting at `0`, ignoring the fact `!satp` implies no paging for now, assume `satp == 0`.
What are the contents of the first page if we were to use recursive paging scheme and tries to map all the `512` pages in it?
It would be something like this:

| # of PTE | Physical Address | PPN of PTE | flags      |
| -------- | ---------------- | ---------- | ---------- |
| 0        | 0                | 0          | ???        |
| 1        | 8                | 1          |            |
| 2        | 16               | 2          |            |
| 3        | 24               | 3          |            |
| ...      | ...              | ...        |            |
| 510      | 4080             | 510        |            |
| 511      | 4088             | 511        |            |

All seems well, except the RISC-V algorithm doesn't stop at the right depth: it stops for only two **disjoint** reasons, either the flags in the PTE is `PTE_R || PTE_X`, or the depth overflowed the architecture; the former means the page table tree traversal is a success, while the latter simply raises a page fault.
You may implement software recursive scheme as you wish, but RISC-V spec simply doesn't specify such a scheme.

## The First User Space Process `user/initcode.S`

After the kernel internal housekeeping is done (`kernel/main.c`, XV6 makes direct map VA for all the PA (by default 128 MiB), after which some of the PA are dubbed as kernel stacks and mapped with some high VA (so these PA are actually mapped twice, still safe, since they are `kalloc`-ed but not `kfree`-ed, so normally we only access them via these high VA): XV6 defines a fixed amount of maximum processes, so the XV6 kernel knows exactly where the kerenl VA are. And many more stuff.), the kernel attempts to create the first process, which is the special `user/initcode.S`, which in turn calls `exec("/init")`. Note that this process has only three pages upon start: one at user space VA zero for the code and the stack, two for trompoline and trapframe as usual (since it needs system call `exec`). Then `exec` does the heavy lifting.

## Alignments

How to align arbitrary non-negative `n` to some multiple of other positive `alignment`, i.e. find the smallest integer `N` satisfying `(N % alignment == 0) && (N >= n)`?
`kernel/riscv.h` provides a nice implementation (the `PGROUNDUP` macro): first add `alignment - 1`, then substract the remainder.
If the original input is exactly multiple of `alignment`, this is no-op.
If the original input is a bit more than exact multiple of `alignment`, then by adding `alignment - 1`, it must surpass the next multiple, and then we trim it down.
Note that if we ensure that the function is always fed with valid `alignment` e.g. `PGSIZE` (4096) and we don't consider overflow, this function is branch-less!

``` Rust
fn align_up(n: usize, alignment: usize) -> usize {
    match alignment {
        0 => panic!(),
        1 => n,
        _ => {
            let tmp = n.checked_add(alignment - 1).unwrap();
            tmp - tmp % alignment
        }
    }
}
```

## About ASLR (Address Space Layout Randomization) and KASLR (Kernel Address Space Layout Randomization)

The story dates back to shared libraries...

### [SO](https://unix.stackexchange.com/questions/512849/whats-inside-the-kernel-part-of-virtual-memory-of-64-bit-linux-processes#comment948570_512854)

> The kernel’s memory map on x86-64 is documented in the kernel itself. The kernel maps
>
> - user-space (for the current process)
> - PTI data structures
> - all the physical memory
> - the kernel’s data structures, in various blocks, with holes for ASLR
> - the kernel itself
> - its modules
>
> Having a full mapping of physical memory is convenient, but its relevance is debated compared to the security risks it creates, and its address-space burden (since physical memory is effectively limited to half the address space as a result; this prompted the recent expansion to five-level page tables with 56-bit addresses).

> Robert Love’s Linux Kernel Development is a good introduction, despite its age; follow that up with Corbet et al’s [Linux Device Drivers](https://lwn.net/Kernel/LDD3/). Regarding the security aspects, see [Kees Cook’s talk at KR 2017](https://www.youtube.com/watch?v=b2_HAH2kX04) (slides [here](https://outflux.net/slides/2017/kr/kspp.pdf), latest version [here](https://outflux.net/slides/2019/uc/kspp.pdf)), I think he mentioned the issues during that talk. – Stephen Kitt Commented Apr 17, 2019 at 8:05

### [dev.io](https://dev.to/satorutakeuchi/a-brief-description-of-aslr-and-kaslr-2bbp)

> KASLR can be considered as the kernel version of ASLR. Its concept is the same as ASLR. With enabling KASLR, the Linux kernel locates its code/data at the random addresses at every boot time. Of course, the Linux kernel should be built as PIE.

### [lwn.net, Jake Edge, 2013.Oct.9, Kernel address space layout randomization](https://lwn.net/Articles/569635/)

> These kinds of attacks rely on knowing where symbols of interest live in the kernel's address space.

> ASLR in user space randomizes the location of various parts of an executable: stack, mmap region, heap, and the program text itself.

> Cook's kernel ASLR (KASLR) currently only randomizes where the kernel code (i.e. text) is placed at boot time. KASLR "has to start somewhere", he said. In the future, randomizing additional regions is possible as well.

### [redhat blog](https://www.redhat.com/en/blog/position-independent-executables-pie)

> As the address of the symbol in memory is not a part of the main binary the loader adds a level of indirection in the procedure linkage table (the `.plt` section).

### [SO difference between aslr and pie](https://stackoverflow.com/questions/54747917/difference-between-aslr-and-pie)

> And even before that, even in real mode, we had segment registers. This enabled programmers to move thing basically everywhere they wanted with a 16-bytes granularity.

> If a program is compiled without PIE its text and data sections cannot be relocated in memory, however, ASLR can be applied to the stack, heap, and dynamic libraries that it uses, such as libc.

> One important caveat to this simple explanation is that Windows can apply ASLR to non-PIE executables by [embedding relocation information](https://stackoverflow.com/a/39216123/15675011) into the executable, and these relocations are resolved by the linker on the fly when the code is loaded into memory.

### [SO on QEMU](https://stackoverflow.com/questions/58420670)

### [SO on QEMU](https://stackoverflow.com/questions/55189463)

### [memfault on linker scripts](https://interrupt.memfault.com/blog/how-to-write-linker-scripts-for-firmware)

Note that before linking, `nm` may show some symbols like this: `00000000 T main`, and after linking it might become this: `00000294 T main`.

Using `riscv` branch of XV6, we may again see this in action:

``` bash
$ riscv64-elf-nm kernel/main.o | grep main
0000000000000000 T main
$ riscv64-elf-nm kernel/kernel | grep main
0000000080000e84 T main
```

> For example, it can generate debug information, garbage collect unused sections of code, or run **whole-program optimization (also known as Link-Time Optimization, or LTO)**. See also this [SO thread](https://stackoverflow.com/questions/3322911/what-do-linkers-do).

> Code and data are bucketed into sections, which are contiguous areas of memory. There are no hard rules about how many sections you should have, or what they should be, but you typically want to put symbols in the same section if:
>
> 1. They should be in the same region of memory, or
> 2. They need to be initialized together.

> By convention, we name those sections as follow; see also [ELF spec](https://refspecs.linuxbase.org/elf/elf.pdf) for a full list, for some tools might fail in odd ways if you don't follow these rules.
>
> 1. `.text` for code and constants
> 2. `.bss` for uninitialized data
> 3. `.stack` for our stack
> 4. `.data` for initialized data

### [riscv-collab issue: Difference in ELF with PIE vs NOPIE](https://github.com/riscv-collab/riscv-gnu-toolchain/issues/905)

> [Nelson1225 on May 24, 2021](https://github.com/riscv-collab/riscv-gnu-toolchain/issues/905#issuecomment-846682710)
> I think the difference is how to solve the RELATIVE relocation.
>
> I remember that most of the targets used to encode the symbol values into GOT entries when enabling pie. So that when the dynamic linker (or elf loader) resolving the RELATIVE relocation, they will load the original symbol values from the got entries first, and then plus the load offset and restore the values back to the got entries. A total of one load and one store are required to resolve one RELATIVE relocation.
>
> But RISC-V encodes the symbol value to the addend of RELATIVE relocation directly, so it doesn’t matter whether or not the symbol value is written into got entry. Our dynamic linker will plus the load offset to the addend of RELATIVE relocation, and then store the value to got entries. Therefore, we only need one store when relocating one RELATIVE relocation, this should reduce the burdens of dynamic linker.
> 
> As I know, x86 uses the former method, so they should always encode the symbol values into got entries with or without pie. I’m not sure if it’s still the same now, since I haven’t seen the details in a while.

### [technovelty](https://www.technovelty.org/linux/plt-and-got-the-key-to-code-sharing-and-dynamic-libraries.html)

Shared libraries are meant to be shared among executables. This means there are two major things for OS/dynamic linker:

1. Executables might well have different combinations of required shared libraries, loading them with arbitrary orders, thus they need to be position independent.
  - An alternative is for the OS to assign each installed shared library a specific address, some call *prelinking*, but this is quickly unmanageable, and just don't work well with 32-bit systems due to smaller address spaces.
2. Share code: since the library `.text` section are read-only, multiple executables might well share the exact same pages with the help from OS.
  - As any other object file, there's accompanied `.data`: these cannot be shared across processes! Thus they'd need to be placed *relative* to the `.text` section.
3. Based on aforementioned two requirements, shared libraries have to work disregard of the address it's loaded. Executables, however, need to assess the address during run time.

For `i386`, due to the fact there's no way retriving PC directly, a hack dubbed *thunk* is often deployed: the `call` instruction automatically pushes the return address onto stack, and the thunk is simply copying whatever is on the stack, in this case basically PC, onto some register then return. Then we may do relative addressing as usual.

As for how executables figure out where the symbols provided by dynamic libraries are, the `.got`/`.plt`/`.rela.dyn`/`.rela.plt` sections of the ELF are used.

For plain values e.g. `extern int32_t provided_by_some_other_dynamic_lib;`, the access of the variable would go through some indirection, first do relative addressing to find the entry in `.got`, that entry is then used as address of `provided_by_some_other_dynamic_lib`. Who filled this entry and when? It's the dynamic loader examining **relocation sections** e.g. `.rela.dyn`: it contains various information about dynamically loaded symbols, e.g. name, type of the symbol (e.g. `R_X86_64_GLOB_DAT`), and most importantly after figuring out where that symbol lives, fill-in the `.got` entry with address of the symbol.

For function calls, it's slightly more complicated (which makes the caller assembly slightly more elegant). An example would be more easy to explain.

``` C
int foo(void);
int function(void) {
    return foo();
}
```

``` bash
$ gcc -shared -fPIC -o libtest.so test.c
$ objdump -S libtest.so | grep -A5 '<function>'
0000000000001109 <function>:
    1109:       55                      push   %rbp
    110a:       48 89 e5                mov    %rsp,%rbp
    110d:       e8 1e ff ff ff          call   1030 <foo@plt>
    1112:       5d                      pop    %rbp
    1113:       c3                      ret
$ objdump -j .plt --disassemble-all libtest.so
0000000000001020 <foo@plt-0x10>:
    1020:       ff 35 ca 2f 00 00       push   0x2fca(%rip)        # 3ff0 <_GLOBAL_OFFSET_TABLE_+0x8>
    1026:       ff 25 cc 2f 00 00       jmp    *0x2fcc(%rip)        # 3ff8 <_GLOBAL_OFFSET_TABLE_+0x10>
    102c:       0f 1f 40 00             nopl   0x0(%rax)

0000000000001030 <foo@plt>:
    1030:       ff 25 ca 2f 00 00       jmp    *0x2fca(%rip)        # 4000 <foo@Base>
    1036:       68 00 00 00 00          push   $0x0
    103b:       e9 e0 ff ff ff          jmp    1020 <_init+0x20>
$ objdump -j .got.plt --disassemble-all libtest.so
0000000000003fe8 <_GLOBAL_OFFSET_TABLE_>:
    3fe8:       08 3e                   or     %bh,(%rsi)
        ...
    3ffe:       00 00                   add    %al,(%rax)
    4000:       36 10 00                ss adc %al,(%rax)
    4003:       00 00                   add    %al,(%rax)
    4005:       00 00                   add    %al,(%rax)
        ...
$ readelf --relocs libtest.so
Relocation section '.rela.plt' at offset 0x520 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000004000  000300000007 R_X86_64_JUMP_SLO 0000000000000000 foo + 0
```

What happens here is when we see the dynamic call, we `call` with some address within `.plt` section, where the instructions just in turn jumps back to the next instruction which is `push $0x0`... wait what, we just jump around only to execute the next instruction? (the `ss adc` doesn't matter here: it's interpreted as operand for `jmp`, which is the next instruction (little Endian) `push $0x0`.)

Well what would actually happen is **lazy binding** happening in the next few instructions: `.got` typically also contains an identifier and resolution function provided by the dynamic linker, and in the second `jmp` right after the `push $0x0`, we load the identifier name we're looking for (the function name) and call into the dynamic linker. The dynamic linker then has required information: we're `libtest.so` looking for function `foo`, then it would patch the `.got.plt` contents, s.t. next time we encounter the function we actually jump from callsite to `.plt` after which to the actual function, rather than falling back into the dynamic linker stub/helper.

Note that we may change behavior of the identifier resolution stub with e.g. `LD_PRELOAD`, s.t. symbols would then have precedences.

### [k3170makan introduction to elf format](https://blog.k3170makan.com/2018/11/introduction-to-elf-format-part-vii.html)

## KASLR in XV6

One may now realize that ASLR in general is not that easy a task to do: you shall not random all the way, else you might run into trouble growing memory, and most importantly you need a powerful ELF loader/dynamic linker s.t. they know where to find the symbols and how to modify the `.got`/`.got.plt` entries. Solving the relocations is a non-trivial task.

Then how the heck could one implement KASLR on XV6? I mean coding a proper ELF loader that handles all the relocation/`.got`/`.got.plt` is probably already more complex than the XV6 kernel itself, and you have to do so since if you pass in the `-fpic`/`-fpie` options to the compiler (say `riscv64-elf-gcc` on Archlinux), the assembly code does contain lots of load from values in `_GLOBAL_OFFSET_TABLE_`/`_PROCEDURE_LINKAGE_TABLE_`, but the `.got`/`.got.plt` contain little values: if you run this (see also *GNU Make implicit rules*, don't forget to link with `-pie`), your program wouldn't even able to boot into supervisor mode, since all the indirections via `.got`/`.got.plt` are broken.

It seems you **do** need a proper loader to do so, which QEMU doesn't provide.

Except... you don't. No, you don't have to do all the relocations to resolve the `.got`/`.got.plt` on such a *__tiny__* kernel that may be compiled with `-mcmodel=medany`.

[GCC manual for RISC-V](https://gcc.gnu.org/onlinedocs/gcc/RISC-V-Options.html)
> `-mcmodel=medany`
> Generate code for the medium-any code model. The program and its statically defined symbols must be within any single 2 GiB address range. Programs can be statically or dynamically linked.
> The code generated by the medium-any code model is position-independent, but is not guaranteed to function correctly when linked into position-independent executables or libraries.

Let's ignore the second part for a sec, and `grep jal kernel/kernel.asm | grep -v jalr`. Nothing. All jumps are already using relative address, if you remember to turn off the implicit Make rules.

So on XV6 all we need is `-pie` flag to the linker, and the kernel is already KASLR-ready.

### Criticisms

[Spender@grsecutiry](https://forums.grsecurity.net/viewtopic.php?f=7&t=3367)
[security SE](https://security.stackexchange.com/questions/69054/does-kaslr-really-provide-more-security-against-exploits)

Basically, generic ASLR may be brute-forced, just guess: if fail, the program crashes, and we retry.
Kernel is less prone to this, since frequent kernel panics would quickly attract attention, but we're not safe, either: **any** leak may help in figuring out the exact layout of this boot of the kernel, and that's a huge surface of attack!
As pointed out in [Jake Edge lwn.net](https://lwn.net/Articles/569635/): (quote from Kees Cook)

> The `INET_DIAG` socket API uses the address of a kernel object as a handle. That address is opaque to user space, but it is a real kernel pointer, so it can be used to determine the kernel location. Changing it to some obfuscated value would fix the problem, but the network maintainers are not willing to do so, he said.

### TRNG implementations

[pds6502@reddit](https://www.reddit.com/r/RISCV/comments/1cy8zs2/comment/l59pjzt)
> If you can sense temperature, such as from some hardware register or bandgap device, use that.
> Thermal energy is one of the most random sources of noise known to humankind.
> Whenever we needed white noise generation we always would put a simple 50Ω resistor in front of a high gain differential amp with no other input signals connected--digitizing the result was a simple matter of comparator and threshold.


### PRNG implementations

[Linear Feedback Shift Register](https://en.wikipedia.org/wiki/Linear-feedback_shift_register)

[reddit](https://www.reddit.com/r/RISCV/comments/1cy8zs2/comment/l597uu3)
[users.ece.cmu.edu](https://users.ece.cmu.edu/~koopman/lfsr/index.html)

``` C
if (i & 1) {
    i = (i >> 1) ^ feed;
} else {
    i = (i >> 1);
}
```

Maybe also feed the value over some hash algorithms!

#### [When to use `/dev/random` vs `/dev/random`](https://unix.stackexchange.com/questions/324209)

Basically, always `/dev/urandom` unless you're on a device with low entropy and just booted, or you need a _information theoretic secure one-time pad_; for the latter, maybe you know better than this anyway.

## System Call `exec` and User Processes in XV6

The `kstack` field of each `struct proc` is set early during boot in `proc_mapstacks` in `kvmmake`, and never unset during XV6 lifetime.
The `trapframe` is a page allocated on process start and deallocated on process dead, *unique* to that instantce of process: it caches registers representing the state of that process.
The `trampoline` is a (shared) physical page that's mapped as read only and not accessible to user space in high VA for each process for entering the kernel. In XV6, similar to `trapframe`, it's mapped/unmapped at the beginning/end of lifetime of an instance of a process. Though we don't really need to unmap it, no?

Upon initialize of a generic process, aside from its page table (in which contain `trampoline` at `TRAMPOLINE` and `trapframe` at `TRAPFRAME` which does *not* contribute to process size btw), kernel stack `kstack`, and PID, the only interesting thing that got initialized is the `ra` field of `context` field of `struct proc` pointing to `forkret` (`kernel/proc.c`) and `sp` pointing to *end* of `kstack` of that process.

For the first process `initcode`/`init`, two other fields are initialized: the `epc` in its `trapframe` set to zero for user program counter, whereas the `sp` in its `trapframe` set to `PGSIZE` for user stack pointer.

# Questions

- Why `sfence.vma` twice when `kvminithart`? In particular, why `sfence.vma` after chaing `satp`, how are there any stale entries if we just flushed it?
- Why `kernelvec` is provided by assembly code, rather than generic C code?
  - It provides **supervisor trap vector**, i.e. the function would be living in the `stvec` register, i.e. traps go here, then `kerneltrap`. Need to save context!
- What's kernel address space randomization? Does shuffling sections and/or shift some pages count? What did we benifit from such a measure?
  - Maybe after page table tree creation, shuffle the PTE leaves?
    - entropy source?
      - The `seed` CSR, e.g. `csrrw rd, seed, x0`, see also [lists.riscv.org](https://lists.riscv.org/g/tech-privileged/topic/risc_v_tech_crypto_ext/90925122), gives some basic randomness.
    - it seems we need LOTS of `memcpy`...
      - not really, since KASLR or ASLR in general isn't about the structure of the mapping between VA and PA, it's what are put where on the VA.
    - other CPUs shall not be running yet, so their stack may be used for this purpose if we need some work space...
      - well yes but actually no, since we already have `kalloc`, why bother?
  - randomize the linked list behind `kalloc`?
    - no, this merely changes where the kernel page table tree lives in the physical memory, its VA-PA K-V mapping is still direct map
    - well ackchyually above is not the reason: again, what PA are responsible for what VA is not the point, it's what are where in in terms of VA.
- How does the Linux kernel achieve KASLR without `-mcmodel=medany`? How does it handle all the relocations?
- Why `uvmfree` (`kernel/vm.c`) tries to `kfree` pages starting from VA `0` given the size parameter non-zero? What are placed there in the user address space?
  - It's since the `exec` system call implementation of XV6 simply allocates all the `vaddr` plus required `memsz` as specified in each `ELF_PROG_LOAD` program header in the ELF header, from zero! (`kernel/exec.c`)
  - I.e. the XV6 allocates eagerly all the memory asked by the ELF, s.t. memory from user VA 0 till max in ELF header (probably some program header's `vaddr + memsz`).
  - Thus it needs to free all those pages.
- Why `freewalk` (`kernel/vm.c`), which should be called on `satp` to page table trees of which all the L0 pages had been removed and the purpose of the call being freeing the L1 pages, seem to deviate from the RISC-V hardware algorithm?
  - It's a boolean logic encapsulation.
  - `!PTE_V` PTEs considered freed
  - We assume no leaves, thus `!(PTE_R || PTE_X)` which is  `(!PTE_R) && (!PTE_X)`, assming pages are valid, this implies `!PTE_W`, since by Sv39, `PTE_W` implies `PTE_R`
- `kstack` in `struct proc`, lifetime seem absurd: kernel VA allocated in `proc_mapstacks`, assigned to the process in `procinit`, but not cleared in `free_proc`? See also `kernel/proc.c`
- Why `myproc` twice in `exec` system call? (`kernel/exec.c`)
  - Seems to be related to possible scheduling due to `end_op` (`kernel/log.c` and `kernel/virtio_disk.c`), this call may change CPU
  - But we're pointing to the global fixed array of process table, right? Then we should not really care?
- Why user process `trampoline` is mapped/unmapped at birth/death of process, unlike `kstack` which is mapped till XV6 itself dies? Seems like a job that's unnecessarily done multiple times...
