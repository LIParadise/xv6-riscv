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

## [preshing.com, memory barriers are like source control operations](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations/)

> Like compiler reordering, processor reordering is invisible to a single-threaded program. It only becomes apparent when [lock-free techniques](http://preshing.com/20120612/an-introduction-to-lock-free-programming) are used – that is, when shared memory is manipulated **without any mutual exclusion between threads**.

> As Larry and Sergey modify their working copies of the repository, their modifications are constantly _**leaking**_ in the background, to and from the central repository, at totally random times.
> Once Larry edits the file X, his change will leak to the central repository, but there’s no guarantee about when it will happen. It might happen immediately, or it might happen much, much later. He might go on to edit other files, say Y and Z, and those modifications might leak into the respository **before** X gets leaked. In this manner, stores are effectively **reordered** on their way to the repository.
> Similarly, on Sergey’s machine, there’s no guarantee about the timing or the order in which those changes leak **back** from the repository into **his** working copy. In this manner, loads are effectively reordered on their way out of the repository.

### `LoadLoad` barrier

> A `LoadLoad` barrier effectively prevents reordering of loads performed before the barrier with loads performed after the barrier.

> In our analogy, the #LoadLoad fence instruction is basically equivalent to a pull from the central repository.
> It could very well pull an _older_ revision than the head, as long as that revision is **at least as new as the newest value which leaked from the central repository into his local machine**.

So in essence, with a `LoadLoad` fence instruction, we may rest assured that the values after the fence are **at least as new as** the most fresh value we've seen to date.
The most disastrous kind of problem mitigated by `LoadLoad` fence is when we may access some data only if some flag is set: with this instruction, we can be sure that the data isn't fetched before the flag is actually asserted: it's at least as new as the flag.

``` C
if (global_value_ready) {
    generic_load_load_fence();
    return global_value;
    // It doesn’t matter exactly when that happens;
    // once the leaked flag has been observed,
    // one issues a #LoadLoad fence to prevent reading values which are older than the flag itself.
}
```

### `StoreStore` barrier

> In our analogy, the `StoreStore` fence instruction corresponds to a **push** to the central repository.
> As an added twist, let’s suppose that `StoreStore` instructions are **not** instant.

If one sees something sequenced-after the `StoreStore` barrier, one must be able to see all the previous writes (with `LoadLoad` fence).

``` C
global_value = x;
generic_store_store_fence();
// once some other guy detects this,
// one can be confident he’ll see the correct value of `global_value`
global_value_ready = 1;
```

> What’s interesting is that, for this pattern to work, `global_value` does not even need to be an atomic type; it could just as well be a huge structure with lots of elements.

### `StoreLoad` barrier

> A `StoreLoad` barrier ensures that all stores performed before the barrier are visible to other processors, and that all loads performed after the barrier receive the latest value that is visible at the time of the barrier. In other words, it effectively prevents reordering of all stores before the barrier against all loads after the barrier, respecting the way a [sequentially consistent](http://preshing.com/20120612/an-introduction-to-lock-free-programming#sequential-consistency) multiprocessor would perform those operations.

> In terms of the analogy, a `StoreLoad` barrier could be achieved by pushing all local changes to the central repostitory, **waiting for that operation to complete**, then pulling the absolute _latest_ head revision of the repository.
> If we throw a `LoadStore` barrier into that operation, which shouldn’t be a big deal, then what we get is a **full memory fence** - acting as all four barrier types at once.
> [As Doug Lea also points out](http://g.oswego.edu/dl/jmm/cookbook.html), it just so happens that on all current processors, every instruction which acts as a #StoreLoad barrier also acts as a full memory fence.

> `StoreLoad` is unique. It’s the only type of memory barrier that will prevent the result `r1 == r2 && r2 == 0` in the example given in [Memory Reordering Caught in the Act](http://preshing.com/20120515/memory-reordering-caught-in-the-act); the same example I’ve repeated earlier in this post.

``` C
// it might be the case in which both assertions passed!
static int32_t r0 = 0;
static int32_t r1 = 0;

// thread 1
r0 = 1;
assert(r1 == 0);

// thread 2
r1 = 1;
assert(r0 == 0);
```

> How is `StoreLoad` different from a `StoreStore` followed by a `LoadLoad`? 
> Remember, the push operation may be delayed for an arbitrary number of instructions, and the pull operation might not pull from the head revision.

With `StoreStore` followed by `LoadLoad`, the CPU only needs to make sure these two things:
1. As the next write _leaks_ into the global visible memory, all previous writes must had been also made available there, too.
  - The next write might be miles away after the `LoadLoad` fence, before which we might have lots of load instructions!
2. All the values it loads after `LoadLoad` must be at least as new as the latest amongst all the previous values it had loaded.
  - The last load instruction might be miles backwards it might be the case it's so ancient it witnessed dinosaurs going extinct.
So `StoreLoad` is stronger after all: out of the four fences and combos, both the assertions in the previous example are not right iff we have this fence.

### How Far Does This Analogy Get You?

> Fortunately, the analogy presented in this post corresponds to a [weak memory model](http://preshing.com/20120930/weak-vs-strong-memory-models). If you can wrap your head around it, and enforce correct memory ordering using the fence instructions given here, you should be able to handle most CPUs.
> The analogy also corresponds pretty well to the abstract machine targeted by both C++11 (formerly known as C++0x) and C11. Therefore, if you write lock-free code using the standard library of those languages while keeping the above analogy in mind, it’s more likely to function correctly on any platform.

> For instance, there are also [data dependency barriers](http://www.mjmwired.net/kernel/Documentation/memory-barriers.txt#305). I’ll describe those further in a future post. Still, the four types given here are the big ones.

> If you’re interested in how CPUs work under the hood – things like stores buffers, cache coherency protocols and other hardware implementation details – and why they perform memory reordering in the first place, I’d recommend the [fine](http://www.rdrop.com/users/paulmck/scalability/paper/whymb.2010.07.23a.pdf) [work](http://www.kernel.org/doc/Documentation/memory-barriers.txt) of Paul McKenney & David Howells. Indeed, I suspect most programmers who have successfully written lock-free code have at least a passing familiarity with such hardware details.

## Generic Questions

- So why exactly does kernels also choose to turn on virtual memory?
    - one reason is that real hardwares presents vastly different memory-mapped devices and physcial DRAM layouts; using virtual memory makes later kernel code easier: not necessarily directy map!
- program headers (`objdump -p`) and object file sections (`objdump -h`), how do they relate to each other?
