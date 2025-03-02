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

## KASLR (Kernel Address Space Layout Randomization)

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
