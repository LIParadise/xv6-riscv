# Chapter 3.3 Code: Creating An Address Space

## [6.S081: Virtual Memory](https://pdos.csail.mit.edu/6.828/2021/slides/6s081-lec-vm.pdf)

So what exactly is **recursive paging**?
Seems to be present on AMD64 systems..., how exactly does it work, in particular on RISC-V Sv-39 systems?
[osdev forum](https://forum.osdev.org/viewtopic.php?t=36910)
[osdev wiki](https://wiki.osdev.org/User:Neon/Recursive_Paging)
[reddit](https://redd.it/qz8wk1)

Consider the case we have only 2 pages, and `satp` is set to the first page (non-null thx to e.g. QEMU placing RAM at `0x80_000_000`, or maybe just ignore the fact that `!satp` means no paging for now).
What should the first page look like if it 1 page alone represents the 3 pages required for a Sv-39 page table tree addressing, assuming direct map?
In fact let's consider there's 512 pages (`0..512`) instead of 2 pages, we claim that we may do direct map like the following:
Note that for each page in the range `0..512` (in Rust terms), the 27-bit VA page number is always 9-bit zero, 9-bit zero, followed by 9-bit non-null, we may set the first entry PPN in `satp` to be identity map, in this case zero.
Such a scheme helps save some pages if the process barely uses RAM: instead of using 3 out of 512 pages for paging, we now uses only 1.

| # of PTE | 44-bit PPN              | Flags (ignored for now) |
| -------- | ----------------------- | ----------------------- |
| 0        | 0x_000_0000_0000 == 0   |                         |
| 1        | 0x_000_0000_0001 == 1   |                         |
| 2        | 0x_000_0000_0002 == 2   |                         |
| ...      | ...                     |                         |
| 509      | 0x_000_0000_01FD == 509 |                         |
| 510      | 0x_000_0000_01FE == 510 |                         |
| 511      | 0x_000_0000_01FF == 511 |                         |

### Questions

> G flag prevents TLB flushes of a PTE

But why?

[Privileged Architecture Version 20240411](priv-isa-asciidoc.pdf)
> The G bit designates a global mapping. Global mappings are those that exist in all address spaces. For non-leaf PTEs, the global setting implies that all mappings in the subsequent levels of the page table are global.
> Note that failing to mark a global mapping as global merely reduces performance, whereas marking a non-global mapping as global is a software bug that, after switching to an address space with a different non-global mapping for that address range, can unpredictably result in either mapping being used.

So it's since some VA-PA key-value pair is ubiquitous that they are present on basicall all the page tables, s.t. no need to flush these entries.

[reddit, understanding paging implementation](https://redd.it/1g5d8lw)
[SO, TLB with process identifier](https://stackoverflow.com/questions/76500243)
[openhwgroup, CVA6](https://docs.openhwgroup.org/projects/cva6-user-manual/03_cva6_design/MMU.html)

## Generic Questions

- So why exactly does kernels also choose to turn on virtual memory?
    - one reason is that real hardwares presents vastly different memory-mapped devices and physcial DRAM layouts; using virtual memory makes later kernel code easier: not necessarily directy map!
- program headers (`objdump -p`) and object file sections (`objdump -h`), how do they relate to each other?
