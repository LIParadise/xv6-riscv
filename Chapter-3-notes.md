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

// thread 0
r0 = 1;
assert(r1 == 0);

// thread 1
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

## [preshing.com, weak vs strong memory models](https://preshing.com/20120930/weak-vs-strong-memory-models/)

> Technically, once you’ve written (and debugged) portable lock-free code in C11, C++11 or Java, only the software memory model is supposed to matter. Nonetheless, a general understanding of hardware memory models may come in handy. It can help you explain unexpected behavior while debugging, and — perhaps just as importantly — appreciate how incorrect code may function correctly on a specific processor and toolchain out of luck.

### Weak Memory Model

> Any load or store operation can effectively be reordered with any other load or store operation, as long as it would never modify the behavior of a single, isolated thread.
> The C11 and C++11 programming languages expose a weak software memory model which was in many ways influenced by the Alpha.

### Weak With Data Dependency Ordering

> Though the Alpha has become less relevant with time, we still have several modern CPU families which carry on in the same tradition of weak hardware ordering:

> 1. ARM, which is currently found in hundreds of millions of smartphones and tablets, and is increasingly popular in multicore configurations.
> 2. PowerPC, which the Xbox 360 in particular has already delivered to 70 million living rooms in a multicore configuration.
> 3. Itanium, which Microsoft no longer supports in Windows, but which is still supported in Linux and found in HP servers.

> These families have memory models which are, in various ways, almost as weak as the Alpha’s, except for one common detail of particular interest to programmers: they maintain [data dependency ordering](http://www.mjmwired.net/kernel/Documentation/memory-barriers.txt#305). What does that mean? It means that if you write A->B in C/C++, you are always guaranteed to load a value of B which is at least as new as the value of A. The Alpha doesn’t guarantee that. I won’t dwell on data dependency ordering too much here, except to mention that the [Linux RCU mechanism](http://lwn.net/Articles/262464/) relies on it heavily.

> It’s not too hard to visualize. Just imagine a refinement of the [source control analogy](http://preshing.com/20120710/memory-barriers-are-like-source-control-operations) where all modifications are committed to shared memory in-order (no StoreStore reordering), pulled from shared memory in-order (no LoadLoad reordering), and instructions are always executed in-order (no LoadStore reordering). StoreLoad reordering, however, [still remains possible](http://preshing.com/20120515/memory-reordering-caught-in-the-act).

> Under the above definition, the x86/64 family of processors is _usually_ strongly-ordered. There are certain cases in which some of x86/64’s [strong ordering guarantees are lost](http://preshing.com/20120913/acquire-and-release-semantics#comment-20810), but for the most part, as application programmers, we can ignore those cases. It’s true that a x86/64 processor can [execute instructions out-of-order](http://en.wikipedia.org/wiki/Out-of-order_execution), but that’s a hardware implementation detail – what matters is that it still keeps its memory interactions in-order, so in a multicore environment, we can still consider it strongly-ordered. Historically, there has also been a little confusion due to [evolving specs](http://jakob.engbloms.se/archives/1435).

### Strong Memory Model

> A strong hardware memory model is one in which every machine instruction comes implicitly with [acquire and release semantics](http://preshing.com/20120913/acquire-and-release-semantics). As a result, when one CPU core performs a sequence of writes, every other CPU core sees those values change in the same order that they were written.

> Under the above definition, the x86/64 family of processors is _usually_ strongly-ordered.

> Yep, that's right... usually! It's documented in Volume 3, Section 8.2.3 of Intel's x86/64 Architecture Specification. I haven't pored through AMD's specification but my understanding from other people on the web is that it's more or less the same. That's why x86/64 is often said to be strongly ordered. As you mention, StoreLoad is usually the only kind of reordering which can occur, so that's the type I demonstrated in an earlier post.
> Having said that, the strong ordering guarantees of x86/64 go out the window when you do certain things, which are also documented in the same section of Intel's docs:
> Marking memory memory as non-cacheable write-combined (for example using VirtualProtect on Windows or mmap on Linux); something only driver developers normally do.
> Using fancy SSE instructions like movntdq or "string" instructions like rep movs. If you use those, you lose StoreStore ordering on the processor and can only get it back using an sfence instruction. To be honest, I often wonder if there's any risk of a compiler using those instructions when optimizing lock-free code. Personally, I haven't seen it happen yet.

> There is a second type of reordering that isn't covered by the "four possible" reorderings like StoreLoad, and which is allowed on x86: CPUs are allowed to see *their own stores* out of order with respect to the stores from other CPUs, and you can't explain this by simple store-load reordering.
> This is explained in the Intel manual with statements like "Any two CPUs *other than those performing the stores* see stores in a consistent order". The underlying hardware reason is store-forwarding: a CPU may consume its own stores from the store buffer, long before those stores have become globally visible, resulting in those stores appearing "earlier" to that CPU than to all the other CPUs.
> This is often glossed over by people that say x86 only exhibits "StoreLoad" reordering: in fact it has StoreLoad reordering plus "SLF reordering" (SLF being store-to-load forwarding). SLF doesn't fit cleanly in into that 2x2 matrix of reorderings, it kind of has to be described explicitly. 

### Sequential Consistency

> In a [sequentially consistent](http://preshing.com/20120612/an-introduction-to-lock-free-programming#sequential-consistency) memory model, there is no memory reordering. It’s as if the entire program execution is reduced to a sequential interleaving of instructions from each thread. In particular, the result `r1 == r2 == 0` from [Memory Reordering Caught in the Act](http://preshing.com/20120515/memory-reordering-caught-in-the-act) becomes impossible.

### Further Details

> There are many other subtle details filling out the spectrum of memory models, but in my experience, they haven’t proved quite as interesting when writing lock-free code at the application level. There are things like control dependencies, causal consistency, and different memory types. Still, most discussions come back the four main categories I’ve outlined here.

> If you really want to nitpick the fine details of processor memory models, and you enjoy eating formal logic for breakfast, you can check out the [admirably detailed work](http://www.cl.cam.ac.uk/~pes20/weakmemory/) done at the University of Cambridge. Paul McKenney has written an [accessible overview](http://lwn.net/Articles/470681/) of some of their work and its associated tools.

## [g.oswego.edu/dl/jmm/cookbook.html](https://web.archive.org/web/20160827171043/https://g.oswego.edu/dl/jmm/cookbook.html)

> `StoreLoad` barriers: suppose we have `Store1; StoreLoadBarrier; Load2`, `StoreLoad` ensures that `Store1`'s data are made visible to other processors (i.e., flushed to main memory) before data accessed by `Load2` and **all** subsequent load instructions are loaded.

This might help explain what's _receive the latest value that is visible at the time of the barrier_ as claimed by [preshing, memory barriers are like source control operations](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations/): the load operations following the barrier won't execute until the store are made visible to other cores.

We may re-visit the above example C program:
WLOG suppose thread 0 sees `r1 == 0`; note that the load operation ain't done without thread 0 had made its write i.e. `r0 = 1` visible to other cores.
So what might thread 1 see as it loads `r0`? If it sees `r0 == 0`, then that operation should be done before thread 0 had _made the `r0 = 1` visible to other cores_, which since `StoreLoad` fence, meaning thread 1 had set `r1 = 1` **before** the load from `r0`, then thread 0 cannot possibly see `r1 == 0` since thread 1 _should have made its `r1 = 1` visible_, again thanks to `StoreLoad` barrier.

Yeah so C++11/C11 _happens-before_ relationship really is something monumental, if not for the `SeqCst` compatibility with modern hardware manufactures...

## [Can a speculatively executed CPU branch contain opcodes that access RAM?](https://stackoverflow.com/questions/64141366)

> The cardinal rules of speculative out-of-order (OoO) execution are:
> 
> 1. Preserve the illusion of instructions running sequentially, in program order
> 2. Make sure speculation is contained to things that can be rolled back if mis-speculation is detected, and that can't be observed by other cores to be holding a wrong value. Physical registers, the back-end itself that tracks instruction order yes, but not cache. Cache is coherent with other cores so stores must not commit to cache until after they're non-speculative.

> Cache has to be correct at all times because another core could ask you to share it at any moment. Unlike registers which are private to this core.

## [Acquire and Release Semantics](https://preshing.com/20120913/acquire-and-release-semantics/)

> **Acquire Semantics** is a property that can only apply to operations that **read** from shared memory, whether they are [read-modify-write](http://preshing.com/20120612/an-introduction-to-lock-free-programming#atomic-rmw) operations or plain loads. The operation is then considered a **read-acquire**. Acquire semantics prevent memory reordering of the read-acquire with any read or write operation that **follows** it in program order.
> **Release semantics** is a property that can only apply to operations that **write** to shared memory, whether they are read-modify-write operations or plain stores. The operation is then considered a **write-release**. Release semantics prevent memory reordering of the write-release with any read or write operation that **precedes** it in program order.

Note that this definition matches up with C++11/C11 default semantics, where you declare/define atomics and directly operate on them instead of `std::atomic_thread_fence`:

``` c
#include <stdatomic.h>
static atomic_uint_fast32_t my_atomic_u32 = 0;
void runs_in_pthread(void) {
    uint32_t last_seen = atomic_load_explicit(&my_atomic_u32, memory_order_acquire);
    /* snip */
    atomic_fetch_add_explicit(&my_atomic_u32, 1, memory_order_release);
}
```

As per [cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence):

> `atomic_thread_fence` imposes _**stronger**_ synchronization constraints than an atomic store operation with the same `std::memory_order`. While an atomic store-release operation prevents all preceding reads and writes from moving past the _store-release_, an `atomic_thread_fence` with `std::memory_order_release` ordering prevents _**all**_ preceding reads and writes from moving past _**all subsequent stores**_.

And as per [cppreference](https://en.cppreference.com/w/cpp/atomic/memory_order)

- `memory_order_acquire`
  - > A load operation with this memory order performs the acquire operation on the affected memory location: no reads or writes in the current thread can be reordered before this load. All writes in other threads that release the same atomic variable are visible in the current thread
- `memory_order_release`
  - > A store operation with this memory order performs the release operation: no reads or writes in the current thread can be reordered after this store. All writes in the current thread are visible in other threads that acquire the same atomic variable

The exact subject over which all preceding R/W may not move past is stated slightly differently: they shall not move past a _store-release_ operation, e.g. RMW like `atomic_fetch_add_explicit(&my_atomic_u32, 1, memory_order_release)` or simply `atomic_store_explicit(&my_atomic_u32, 42069, memory_order_release)`, meanwhile they shall not move past _**all subsequent stores**_ when we have `std::atomic_thread_fence(memory_order_release)`. I.e.

1. The `std::atomic_thread_fence` may or may not show up in ASM (in x86/64 it's probably no more than a compiler fence), and preceding stores _can_ move below it.
  - [cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence)
    - > On x86 (including x86-64), `atomic_thread_fence` functions issue _no_ CPU instructions and only affect compile-time code motion, except for [`std::atomic_thread_fence(std::memory_order_seq_cst)`](http://en.cppreference.com/w/cpp/atomic/memory_order)
  - [preshing.com Acquire and Release Semantics](https://preshing.com/20120913/acquire-and-release-semantics/#IDComment721195773)
    - > The release fence doesn't prevent memory operations moving down below itself.
    - > The role of a release fence, as defined by the C++11 standard, is to prevent previous memory operations from moving past subsequent stores.
    - > Section 29.8.2 of [working draft N3337](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf) of the C++11 standard guarantees that if `r1 = 1` in this example, then the two fences synchronize-with each other, and therefore `r2` must equal `42`. If the relaxed store was allowed to move up above `A = 42`, it would contradict the C++11 standard. 
  - It's more of `StoreStore` fence plus `LoadStore` fence, no matter how it's gonna be implemented by the platform.
2. Since `std::atomic_thread_fence` with the same `Acquire`/`Release` ordering is **strictly stronger** than corresponding _store-release_/_load-acquire_, they may be used to implement the latter.
  - [preshing.com Acquire and Release Semantics](https://preshing.com/20120913/acquire-and-release-semantics)
    - > Once you digest the above definitions, it’s not hard to see that acquire and release semantics can be achieved using simple combinations of the memory barrier types I [described at length in my previous post](http://preshing.com/20120710/memory-barriers-are-like-source-control-operations).
    - > Please note that these barriers are technically more strict than what’s required for acquire and release semantics on a single memory operation, but they do achieve the desired effect.
  - [marabos.nl](https://marabos.nl/atomics/memory-ordering.html#fences)
    - > Putting this together, it means that a happens-before relationship is created between a release fence and an acquire fence if **any** store after the release fence is observed by any load before the acquire fence.
3. Since `std::atomic_thread_fence` are basically equivalent resp. to (`LoadLoad` + `LoadStore`)/(`StoreStore` + `LoadStore`) combo of the [four major fences](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations), the latter may be used to implement the former.
4. There's no guarantee on stores after a _store-release_ (e.g. `atomic_store_explicit(&my_atomic_u32, 1, memory_order_release)`) moving before the store-release or not.
  - So the corresponding `atomic_load_explicit(&my_atomic_u32)` may or may not see some `atomic_store_explict(&my_atomic_flag, true, memory_order_relaxed)`.
  - C++11/C11 gurantee only **happens-before** relationship. Here it's just plain old parallel programming: you may or may not see other thread's work, with little guarantee.
  - In this sense, talking about if some subsequent store may be placed before a _store-release_ is kinda meanlingless in C++11/C11 memory model, since the standard just doens't care (???): you can't really distinguish between stores that are not reordered but just happened to had **leaked** to you (in [preshing.com](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations) terms) before your matching read-acquire, or that the store in program order being behind the store-release did got reordered before the store-release.

### Acquire and Release While Locking

Wait, so why do `std::atomic_thread_fence(memory_order_acquire)`/`atomic_load_explicit(&my_my_atomic_flag, memory_order_acquire)` talk about read/_write_ operations not moved before some read, while `std::atomic_thread_fence(memory_order_release)`/`atomic_load_explicit(&my_my_atomic_flag, memory_order_release)` talk about _read_/write operations not moved after some write?

The reason is the design is to aid in writing **critical sections**: we want **all** the R/W operations that are meant to be mutual exclusion really done in a mutual exclusion manner: you don't want to read something that are supposed to be read outside of critical section after you _release_/`memory_order_release` the lock, let along modify it before you _acquire_/`memory_order_acquire` the lock.

> All the memory operations in between are contained inside a nice little barrier sandwich, preventing any undesireable memory reordering across the boundaries.

## Generic Questions

- So why exactly does kernels also choose to turn on virtual memory?
    - one reason is that real hardwares presents vastly different memory-mapped devices and physcial DRAM layouts; using virtual memory makes later kernel code easier: not necessarily directy map!
- program headers (`objdump -p`) and object file sections (`objdump -h`), how do they relate to each other?
