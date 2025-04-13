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

### Page Table May Not Cover All Possible VA-PA mappings

Depending on the available physical pages, some possible VA to PA mapping are not possible.

For example, let us consider first a simple page table with only 3 pages of total physical ram, `satp` being the last page, and `VA - 4096 = PA`. A possible config looks like this:

| PA # page     | VA # page    | Index of PPN   | 44-bit PPN     | Comment                            |
| ------------- | ------------ | -------------- | -------------- | ---------------------------------- |
| 0             | 1            | 1              | 0              | level-0 page table, PPN is PA page |
| 0             | 1            | 2              | 1              | level-0 page table, PPN is PA page |
| 0             | 1            | 3              | 2              | level-0 page table, PPN is PA page |
| 1             | 2            | 0              | 0              | level-1 page table
| 2             | 3            | 0              | 1              | level-2 page table, `satp`         |

So far so good. But it's also ok if you're somewhat uncomfortable with this approach, since does not cover all possible cases. In particular, how about this mapping?

| PA # page     | VA                     | Index of PPN   | 44-bit PPN     |
| ------------- | ---------------------- | -------------- | -------------- |
| 0             | 1 GiB (`1 << 30`)      | ?              | ?              |
| 1             | 2 GiB                  | ?              | ?              |
| 2             | 3 GiB                  | ?              | ?              |

Due to how VA is to be translated into PA in Sv-39, i.e. 39 bits, 9, 9, 9, then 12, these three VA (1 GiB, 2 GiB, and 3 GiB) _must_ occupy 3 consecutive PTEs in the top-level level-2 page table page i.e. that pointed to by `satp`. Obviously these 3 entries must point to differnet physical pages (differnet 44-bit PPN). But remember, in this config we have only 3 physical pages. _There might simply be no enough physical pages if VA are too far apart from each other._

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

## XV6, From Supervisor Mode to First User Process

First, HART0 (in my fork, after KASLR is done) collects all the _PA_ in `kmem`, after which initializes the _kernel direct virtual memory map_. Direct map matters since `kmem` gives/reclaims PA.

Next, it sets up the processes. In vanilla XV6, process count is fixed and the PCBs are kept in a static global array [`struct proc proc[NPROC]`](kernel/proc.c). It sets the process state (for scheduling) to `UNUSED`, initializes per-process `struct spinlock`, and assign them the (high) kernel virtual memory VA of `kstack`: these physical pages are mapped _twice_, one in kernel direct map, one in high VA. This is safe since those pages are `kalloc`-ed but never `kfree`-ed, so the only way accessing them is via these high `kstack` VA. Also, the global `struct spinlock wait_lock` is initialized, too.

XV6 then sets up the `stvec` register, the Supervisor mode Trap VECtor, for dealing with interruptions happening as the HART is in supervisor mode.

### `userinit`

The [`userinit`](kernel/proc.c) function first `allocproc`, which allocates the PID and _page table_, maps the _trampoline_ and _trapframe_ at high VA in the page table, and set the PCB `context` field `ra` to `forkret` and `sp` to its kernel stack.

1. [`allocproc`](kernel/proc.c)
    1. User virtual memory space
        - [`trampoline`](kernel/trampoline.S) at `TRAMPOLINE` high user space VA
            - No allocation except user page table tree: it's just making existing kernel `.text` available in the user VA
            - No `PTE_U`
        - [`trapframe`](kernel/proc.h) (which XV6 just allocated for this process via kernel direct map) at `TRAPFRAME` high user space VA
            - Thus this physical page is mapped twice here, one via kernel direct map via the `struct proc` PCB, one in the high VA user space of the process
            - Allocated by the kernel, of which kernel direct map VA is recorded in the `struct proc` PCB, and mapped again in the user VA.
            - No `PTE_U`
    2. Kernel virtual memory space
        - Allocate the page table `pagetable` of the `struct proc` PCB
        - Allocate the `trapframe` of the `struct proc` PCB
            - which is then mapped again in the user virtual memory space
        - The `context` field of the `struct proc` PCB
            - `ra` set to [`forkret`](kernel/proc.c)
            - `sp` set to `kstack` of that process (again via the `struct proc` PCB)
2. [`userinit`](kernel/proc.c)
    1. Allocate a fresh page at VA `0` in the user memory space in which resides [`initcode`](kernel/proc.c)
        - Do not forget the `sz` field of the `struct proc` PCB!
            - Note that `trampoline` and `trapframe` are not counted
            - These two fields track the size of the user memory space
    2. Set `epc` of the `trapframe` in `struct proc` PCB to `0`, meaning when return, it starts from the first instruction
    3. Set `sp` of the `trapframe` in `struct proc` PCB to `PGSIZE`, meaning that only (for now) page of the user space contains both the stack and `.text`...
        - Safety concerns?

[supervisor.adoc](https://github.com/riscv/riscv-isa-manual/blob/869612154a1c7646994567cd14da3784c568dc92/src/supervisor.adoc)
> Note that writing `satp` does not imply any ordering constraints between page-table updates and subsequent address translations, nor does it imply any invalidation of address-translation caches. If the new address space’s page tables have been modified, or if an ASID is reused, it may be necessary to execute an SFENCE.VMA instruction (see `sfence.vma`) after, or in some cases before, writing `satp`.

### Interrupt Handlers e.g. `stvec`: Why Asssembly Code?

One may wonder why assembly rather than calling specific functions just like how user space `ecall` system calls?

Well you always have to register _some machine code somewhere_. That's what `stvec` is for. But you can't just put a generic C function call there: the calling convention is different. One of the things interrupt handlers need to make sure is it shall not disturb the original context, rather it should be transparent to all other code, thus _all_ the context needs to be saved somewhere, but generic C calling convention assumes some registers are _caller-saved_. Thus generic C call won't work: you always have to write some assembly manually for this special case.

### XV6 Context Switch Questions

- What's the purpose of [`struct spinlock wait_lock`](kernel/proc.c)? When and how to use it?
- `sfence.vma`: why the spec suggests when recycling ASID (which XV6 implicitly does since it does no ASID for now), one should `sfence.vma` _after_ changing the `satp` to with the recycled ASID? Wouldn't that imply implicitly referencing stale physical address being possible?
- Safety concerns when `userinit` sets the user `.text` and stack to the same page?

## [preshing.com, memory ordering at compile time](https://preshing.com/20120625/memory-ordering-at-compile-time)

> The cardinal rule of memory reordering, which is universally followed by compiler developers and CPU vendors, could be phrased as follows:
> _Thou shalt not modify the behavior of a single-threaded program._

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

## [preshing.com, atomic vs non-atomic operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations)

> Any time two threads operate on a shared variable concurrently, and one of those operations performs a write, both threads **must** use atomic operations.
> If you violate this rule, and either thread uses a non-atomic operation, you’ll have what the C++11 standard refers to as a _**data race**_ (not to be confused with Java’s concept of a data race, which is different, or the more general [race condition](http://en.wikipedia.org/wiki/Race_condition)). The C++11 standard doesn’t tell you why data races are bad; only that if you have one, “undefined behavior” will result ([§1.10.21](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf)). The real reason why such data races are bad is actually quite simple: They result in torn reads and torn writes.

### Non-Atomic CPU Instructions

> A memory operation can be non-atomic even when performed by a single CPU instruction.

``` assembly
strd r0, r1, [r2]
```

> On some ARMv7 processors, this instruction is not atomic. When the processor sees this instruction, it actually performs two separate 32-bit stores under the hood ([§A3.5.3](http://web.eecs.umich.edu/~prabal/teaching/eecs373-f10/readings/ARMv7-M_ARM.pdf)).
> Interestingly, a torn write is even possible on a single-core device: A system interrupt – say, for a scheduled thread context switch – can actually occur between the two internal 32-bit stores! In this case, when the thread resumes from the interrupt, it will restart the `strd` instruction all over again.

[Jeff Preshing](https://preshing.com/20130618/atomic-vs-non-atomic-operations/#IDComment721196542)
> You can be 100% sure of it if the compiler vendor guarantees it. For example, Microsoft [makes atomicity guarantees](http://msdn.microsoft.com/en-us/library/aa691278.aspx) for C#. I haven't found the same guarantee in Microsoft's C++ compiler documentation, but there is [Bruce Dawson's article](http://msdn.microsoft.com/en-us/library/windows/desktop/ee418650(v=vs.85).aspx) which states "you can assume that reads and writes of naturally aligned native types are atomic" and gives several examples in C. He was working at Microsoft at the time and the article is hosted on MSDN, so I think we can take that as fairly solid guarantee of compiler behavior.

## [preshing.com, the happens-before relation](https://preshing.com/20130702/the-happens-before-relation)

> Let A and B represent operations performed by a multithreaded process. If A happens-before B, then the memory effects of A effectively become visible to the thread performing B before B is performed.

``` C
int A = 0;
int B = 0;

void foo()
{
    A = B + 1;              // (1)
    B = 1;                  // (2)
}
```

> In this case, though, the store to `A` doesn’t actually influence the store to `B`. (2) still behaves the same as it would have even if the effects of (1) had been visible, which is _effectively_ the same as (1)’s effects being visible. Therefore, this doesn’t count as a violation of the _happens-before_ rule. I’ll admit, this explanation is a bit dicey, but I’m fairly confident it’s consistent with the meaning of _happen-before_ in all those language specifications.

Let's recall [the cardinal rule of memory reordering... _Thou shalt not modify the behavior of a single-threaded program._](https://preshing.com/20120625/memory-ordering-at-compile-time). C++11/C11 defines this as [_sequenced-before_ relationship](https://sabrinajewson.org/rust-nomicon/atomics/multithread.html), but we all know that instruction reorder can and do happen even in single threaded execution on single CPU. But the thing is there's no contradiction between _sequenced-before_ and _happens-before_: since all the reordering are s.t. there's no visible side effect to behavior of a single-threaded program, so effectively we're fine, so we're fine.

### Transitivity: is _happens-before_ relationship a partial ordering?

Strictly abide by the C++11/C11, **no**.

[Stefan](https://preshing.com/20130702/the-happens-before-relation/#IDComment799486860)
> At least in C++11, strictly speaking, happens-before is not transitive. According to the standard, §1.10:12 (I am referring to the N3337 draft), an evaluation A happens before an evaluation B if A is sequenced before B, or A inter-thread happens before B.
> For example, assume that operation A is dependency-ordered before B (see §1.10:11 for a definition; this is where consume operations come into play). In particular this means that A inter-thread happens before B. Further assume that B is sequenced before C.
> Then A happens before B, B happens before C, but A is not required to happen before C by the standard.
> This shows that happens-before is not transitive in C++11.
[preshing](https://preshing.com/20130702/the-happens-before-relation/#IDComment834746109)
> Hi Stefan,
> You are totally right. In the current specifications of other languages, happens-before is transitive; [Java](http://docs.oracle.com/javase/specs/jls/se7/html/jls-17.html#jls-17.4.5) says so explicitly, and in [Go](http://golang.org/ref/mem#tmp_1) and [LLVM](http://llvm.org/docs/LangRef.html#memory-model-for-concurrent-operations), it's defined as a partial ordering, which implies transitivity. But in C++11, strictly speaking, it is not always transitive.
> It comes pretty close: If we ignore consume operations and the dependency-ordered-before relation in C++11, the remaining forms of happens-before end up being transitive.
> But it was a mistake for me to say that it was always transitive in C++11. I've removed that statement from the post. Thanks for the precision!

Thank god the `memory_order_consume` was purged in [C++26](https://en.cppreference.com/w/cpp/atomic/memory_order)... not only it's extremely confusing and always teetering on the edge of being misused, but it also disrupts the partial ordering property of _happens-before_.

## [preshing.com, the synchronizes-with relation](https://preshing.com/20130823/the-synchronizes-with-relation)

> ”Synchronizes-with” is a term invented by language designers to describe ways in which the memory effects of source-level operations – even non-atomic operations – are guaranteed to become visible to other threads.

> One thing they have in common is that whenever there’s a synchronizes-with relationship between two operations, typically on different threads, there’s a [_happens-before_ relationship](http://preshing.com/20130702/the-happens-before-relation) between those operations as well.

> The promise is made in [§29.3.2 of working draft N3337](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf):
> > An atomic operation A that performs a release operation on an atomic object M synchronizes with an atomic operation B that performs an acquire operation on M and takes its value from any side effect in the release sequence headed by A.

> As for the condition that the read-acquire must “take its value from any side effect” – let’s just say it’s sufficient for the read-acquire to read the value written by the write-release. If that happens, the _synchronized-with_ relationship is complete, and we’ve achieved the coveted _happens-before_ relationship between threads. Some people like to call this a _synchronize-with_ or _happens-before_ “edge”.
> Most importantly, the standard guarantees (in [§1.10.11-12](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf)) that whenever there’s a _synchronizes-with_ edge, the _happens-before_ relationship extends to neighboring operations, too. This includes all operations before the edge in Thread 1, and all operations after the edge in Thread 2.

(Here Jeff Preshing used the (looser) term _happens-before_ for operations done within one single thread, when we know such relation is actually implied by the stronger [_sequenced-before_ relation](https://sabrinajewson.org/rust-nomicon/atomics/acquire-release.html) within one thread. But yeah the idea is the same: when some _load-acquire_ see some value written by some other _store-release_, _happens-before_ relation ensues.)

> For example, in Java version 5 onward, every store to a `volatile` variable is a write-release, while every load from a `volatile` variable is a read-acquire. Therefore, any `volatile` variable in Java can act as a guard variable, and can be used to propagate a payload of any size between threads. Jeremy Manson explains this in his blog post on [`volatile` variables in Java](http://jeremymanson.blogspot.ca/2008/11/what-volatile-means-in-java.html). He even uses a diagram very similar to the one shown above, calling it the “two cones” diagram.

> Just as _synchronizes-with_ is not only way to achieve a _happens-before_ relationship, a pair of _write-release_/_read-acquire_ operations is not the only way to achieve _synchronizes-with_; nor are C++11 atomics the only way to achieve acquire and release semantics.

```
Happens-before
├── Program order (within a single thread)
└── Synchronizes-with (between threads)
    ├── Mutex lock/unlock
    ├── Thread create/join
    └── Acquire & release semantics
        ├── C++11 atomic types
        ├── volatile types in Java
        ├── volatile types in Microsoft C/C++
        └── Acquire & release fences
            ├── C++11 fences
            ├── Minitomic fences
            └── Platform-specific memory fences
```

> Interestingly, the Go programming language is a bit of convention breaker. Go’s memory model is [well specified](http://golang.org/ref/mem), but the specification does not bother using the term “_synchronizes-with_” anywhere. It simply sticks with the term “_happens-before_”, which is just as good, since obviously, _happens-before_ can fill the role anywhere that _synchronizes-with_ would. Perhaps Go’s authors chose a reduced vocabulary because “_synchronizes-with_” is normally used to describe operations on different threads, and Go doesn’t expose the concept of threads.

Ackchyually... Java does _more_ than requiring acquire-release when operating on `volatile`:

[Manos](https://preshing.com/20130823/the-synchronizes-with-relation/#IDComment733115349)
> "Of course, acquire and release semantics are not unique to C++11. For example, in Java version 5 onward, every store to a volatile variable is a write-release, while every load from a volatile variable is a read-acquire." Does this mean that volatile keyword in Java enforces acquire - release semantics? Because [Bartosz Milewski](https://bartoszmilewski.com/2008/11/11/who-ordered-sequential-consistency) claims that "Java enforces sequential consistency on all access to volatile variables." Also, if I run an test program similar to what described in [https://stackoverflow.com/questions/14861822](https://stackoverflow.com/questions/14861822) by Anthony Williams, son a x86 machine, I confirm that volatile in Java enforces sequential consistency.
[preshing](https://preshing.com/20130823/the-synchronizes-with-relation/#IDComment733127934)
> Java 5+ volatile does both. It enforces acquire & release semantics, and it enforces sequential consistency on all access to volatile variables. Similarly, C++11 atomic types do both when all atomic operations use `memory_order_seq_cst` (the default).

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

## [preshing.com, Acquire and Release Fences](https://preshing.com/20130922/acquire-and-release-fences/)

> - An **acquire fence** prevents the memory reordering of any **read** which **precedes** it in program order with any **read** or **write** which follows it in program order.
> - A **release fence** prevents the memory reordering of any **read** or **write** which **precedes** it in program order with any **write** which follows it in program order.

> In other words, in terms of the barrier types [explained here](http://preshing.com/20120710/memory-barriers-are-like-source-control-operations), an **acquire fence** serves as both a `LoadLoad` + `LoadStore` barrier, while a **release fence** functions as both a `LoadStore` + `StoreStore` barrier. That’s all they purport to do.

> Note that the guard variable must “leak” from Larry’s private workspace over to Sergey’s all by itself. When you think about it, acquire and release fences are just a way to piggyback additional data on top of such leaks.

[Francesco](https://preshing.com/20130922/acquire-and-release-fences/#IDComment1037429346)
> HI Jeff!
> Maybe it could seem like a dumb question, but I need to understand the very basic of this mechanics :)
> If a thread will call SendTestMessage in a loop and another will call TryReceiveMessage in a loop too, there is the chance that the values copied from shared memory into result by the consumer will present values belonging to different calls of SendTestMessage?
> There is nothing to prevent that the consumer thread could recognize if the producer has overwritten some value in the shared memory...
[Jeff Preshing](https://preshing.com/20130922/acquire-and-release-fences/#IDComment1037445264)
> Yes, if you try to send/receive more than one "message" by calling SendTestMessage and TryReceiveMessage in a loop, exactly as they're written here, you will have problems for the reasons you point out. The sample code shown here is very simple -- it only works for a single message.
> If you want to send/receive multiple messages (in a nonblocking way), you'll have to write each message to a different location in memory. A lock-free queue will do the job for you. Or, a pointer to a single message dynamically allocated on the heap, with some kind of safe memory reclamation scheme for when the pointer is replaced.

[Fredi](https://preshing.com/20130922/acquire-and-release-fences/#IDComment1039893834)
> Hi Jeff,
> I was thinking about the following scenario where a release fence might achieve the same result as an acquire fence. In thread_two, you want to acquire the value of 'val', but since both acquire and release fences prevent #LoadStore reordering, the store 'val=123' cannot be reordered with the load from g_sync and therefore I would argue that, in this particular case (!), a release fence would achieve the exact same result as an acquire fence and the load in thread_one can never see 123

``` C
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

static atomic_bool g_sync = false;
static uint32_t yjsp = 0;

void thrd_a() {
    uint32_t tokugawa = yjsp;
    yjsp = 42069;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&g_sync, true, memory_order_relaxed);
}

void thrd_b() {
    while (!atomic_load_explicit(&g_sync, memory_order_relaxed)) {}
    atomic_thread_fence(memory_order_release); // on purpose, for thought experiment: it should be a `LoadStore` fence, right?
    yjsp = 114514;
}
```

[Jeff Preshing](https://preshing.com/20130922/acquire-and-release-fences/#IDComment1039900132)
> Neat example. Your reasoning makes sense to me and I would even bet that on real hardware, you would never see `yjsp == 114514` after both threads are joined. However I would advise against doing so simply because the C++ standard doesn't directly support that particular usage.

## [preshing.com, Acquire and Release Fences Don't Work the Way You'd Expect](https://preshing.com/20131125/acquire-and-release-fences-dont-work-the-way-youd-expect/)

> Raymond Chen [defined acquire and release semantics as follows](http://blogs.msdn.com/b/oldnewthing/archive/2008/10/03/8969397.aspx), back in 2008:
> > An operation with **acquire semantics** is one which does not permit subsequent memory operations to be advanced before it. Conversely, an operation with **release semantics** is one which does not permit preceding memory operations to be delayed past it.

> Raymond’s definition applies perfectly well to Win32 functions like `InterlockedIncrementRelease`, which he was writing about at the time. It also applies perfectly well to [atomic operations](http://preshing.com/20130618/atomic-vs-non-atomic-operations) in C++11, such as `store(1, std::memory_order_release)`.
> It’s perhaps surprising, then, that this definition does not apply to standalone [acquire and release fences](http://preshing.com/20130922/acquire-and-release-fences) in C++11! Those are a whole other ball of wax.

### In C++11, a Release Fence Is Not Considered a “Release Operation”

> In the language of C++11, only a store can be a release operation, and only a load can be an acquire operation. (See §29.3.1 of [working draft N3337](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf).) A memory fence is neither a load nor a store, so obviously, it can’t be an acquire or release operation. Furthermore, if we accept that acquire and release semantics apply only to acquire and release _operations_, it’s clear that Raymond Chen’s definition does not apply to acquire and release fences. In my own post about [acquire and release semantics](http://preshing.com/20120913/acquire-and-release-semantics), I was careful to specify the kind of operations on which they can apply.

### Nor Can a Release Operation Take the Place of a Release Fence

``` cpp
Singleton* tmp = new Singleton;
g_dummy.store(0, std::memory_order_release);
m_instance.store(tmp, std::memory_order_relaxed);
```

> This time, we really _do_ have the problem that Herb Sutter was worried about: The store to `m_instance` is now free to be reordered before the store to `g_dummy`, and possibly before any stores performed by the `Singleton` constructor. The fox is in the henhouse, and mayhem ensues!

> (Interesting side note: An early draft of the C++11 standard, [N2588](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2588.pdf), dating back to 2008, actually tried to define memory fences in a manner similar to this example. There was no standalone `atomic_thread_fence` function in that draft; there was only a member function on atomic objects, `fence`. For convenience, the draft included a `global_fence_compatibility` object, similar to the `g_dummy` object used here. A paper by Peter Dimov [revealed some shortcomings](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2633.html) in this design. As a result, the C++11 standard committee ditched the approach in favor of the standalone fence function we have today.)

## [preshing.com, Double-Checked Locking is Fixed In C++11](https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/)

> Suppose you have a class that implements the well-known [Singleton](http://en.wikipedia.org/wiki/Singleton_pattern) pattern, and you want to make it thread-safe. The obvious approach is to ensure mutual exclusivity by adding a lock.
> It’s a totally valid approach, but once the singleton is created, there isn’t really any need for the lock anymore. [Locks aren’t necessarily slow](http://preshing.com/20111118/locks-arent-slow-lock-contention-is), but they don’t scale well under heavy contention.
> The **double-checked locking pattern** avoids this lock when the singleton already exists.

> Another such platform is the recently introduced ARMv8 architecture. ARMv8 offers `ldar` and `stlr` instructions, which are similar to Itanium’s `ld.acq` and `st.rel` instructions, except that they also enforce the heavier [`StoreLoad`](https://preshing.com/20120710/memory-barriers-are-like-source-control-operations/#storeload) ordering between the `stlr` instruction and any subsequent `ldar`. In fact, ARMv8’s new instructions are intended to implement C++11’s `SeqCst` atomics, described next.

### Using C++11 Data-Dependency Ordering

> In all of the above examples I’ve shown here, there’s a *synchronizes-with* relationship between the thread that creates the singleton and any subsequent thread that avoids the lock. The guard variable is the singleton pointer, and the payload is the contents of the singleton itself. In this case, the payload is considered a **data dependency** of the guard pointer.

> It turns out that when working with data dependencies, a read-acquire operation, which all of the above examples use, is actually overkill! It’s sufficient to perform a **consume operation** instead. Consume operations are meant to eliminate one of the `lwsync` instructions on PowerPC, and one of the `dmb` instructions on ARMv7. I’ll write more about data dependencies and consume operations in a [future post](http://preshing.com/20140709/the-purpose-of-memory_order_consume-in-cpp11).

## [The C `register` keyword](https://stackoverflow.com/questions/578202)

On the surface it's like `inline`: a mere *hint* to the compiler that the programer thinks that the variable is better kept in CPU register rather than memory. And just like `inline`, compiler is free to ignore such suggestions, whether due to its optimization algorithms or didn't bother implement it.

But more importantly, in C, it **disables** the address of operator on the variable, i.e. there shall be **no pointers** to it, which makes aliasing impossible, which unlocks some optimization tricks. OTOH C++ is more free in this regard: if you take address of some `register` variable, then it's the `register` keyword that got ignored.

## Generic Questions

- So why exactly does kernels also choose to turn on virtual memory?
    - one reason is that real hardwares presents vastly different memory-mapped devices and physcial DRAM layouts; using virtual memory makes later kernel code easier: not necessarily directy map!
- program headers (`objdump -p`) and object file sections (`objdump -h`), how do they relate to each other?
- Relationship between `__sync_synchronize`, `memory_order_seq_cst`?
  - If some flag is set only after `__sync_synchronize`/`std::atomic_thread_fence(memory_order_seq_cst)`, may we assume that `atomic_load_explicit(memory_order_relaxed)` suffices to _synchronize-with_/_happens-before_?

### [The sequential consistent order of C++11 vs traditional GCC built-ins like `__sync_synchronize`](https://stackoverflow.com/questions/79529347)

First of all, `__sync_synchronize` according to [GCC](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fsync-Builtins.html) is a *full memory barrier*, so it provides `StoreStore`/`LoadLoad`/`LoadStore`, and in particular `StoreLoad` all at once. That's what it does, again, full memory barrier.

OTOH `SeqCst` is something that guarantees some global total ordering, s.t. it's consistent with [*not coherence-after relation*](https://sabrinajewson.org/rust-nomicon/atomics/seqcst.html) and [_**strongly** happens-before relation_](https://en.cppreference.com/w/cpp/atomic/memory_order).

So these are really two completely separate regimes: one is from a practical hardware implementation PoV, one is from a mathematical total ordering (within all the atomic operations/fences which are `SeqCst`) that's consistent with some other partial ordering.

However in practice, a full memory barrier often suffice as a `atomic_thread_fence(memory_order_seq_cst)`, in particular it has the `StoreLoad` barrier semantics: note in particular that given `atomic_store_explicit(&atomic_x, 42069, memory_order_seq_cst)` sequenced before `atomic_load_explicit(&atomic_y, memory_order_seq_cst)`, since the former `SeqCst` operation on `atomic_x` strongly happens-before the `SeqCst` operation on `atomic_y`, these two may *not* be reordered: one way achieving this is via the `StoreLoad` barrier.

Note that an `atomic_thread_fence(memory_order_acquire)` may be achieved with barrier (`LoadStore` + `LoadLoad`), and `atomic_thread_fence(memory_order_release)` may be achieved with barrier (`LoadStore` + `StoreStore`), so an `atomic_thread_fence(memory_order_acq_rel)` may indeed be implemented with (`LoadLoad` + `LoadStore` + `StoreStore`), e.g. PowerPC [`lwsync`](https://www.cl.cam.ac.uk/%7Epes20/cpp/cpp0xmappings.html). It does **not** require a `StoreLoad` semantics here. However just like we've just seen, `SeqCst` requires the `StoreLoad` barrier semantics - however you achieve this, see also [ARMv8](https://developer.arm.com/documentation/102336/0100/Load-Acquire-and-Store-Release-instructions).

So I _suppose_ on a *cache-coherent shared memory* CPU, a full memory barrier really is what you'd need when implementing `SeqCst` semantics..., and possibly more? But this would need some formal proofs that the total ordering it suggests really **is** a valid `SeqCst` total order... Anyway _intuitively_ it is what we need, since the ordering it produces should naturally be consistent with _happens-before_ relation (due to the fact they are known to be able to be implemented with memory barriers `LoadLoad`/`LoadStore`/`StoreStore`).

[Peter Cordes](https://stackoverflow.com/questions/79529347/the-sequential-consistent-order-of-c11-vs-traditional-gcc-built-ins-like-sy/79529575?noredirect=1#comment140256002_79529575)
> `SeqCst` requires a `StoreLoad` barrier between `SeqCst` stores and `SeqCst` loads, but putting a full barrier after each `SeqCst` store is just an implementation detail on ISAs without AArch64's special interaction between `stlr` and `ldar` where `ldar` has to wait for any `stlr` ops to drain from the store buffer, but otherwise `stlr` is just a *store-release*.

[Peter Cordes](https://stackoverflow.com/questions/79529347/the-sequential-consistent-order-of-c11-vs-traditional-gcc-built-ins-like-sy/79529575?noredirect=1#comment140256012_79529575)
> `SeqCst` is like `Acquire` + `Release` with the additional requirement that no `SeqCst` operation can reorder with any other `SeqCst` operation. `Acquire` is `LoadLoad` + `LoadStore` and `Release` is `StoreStore` + `LoadStore`; only `SeqCst` ever requires a `StoreLoad` barrier.

[Nate Eldredge](https://stackoverflow.com/questions/79529347/the-sequential-consistent-order-of-c11-vs-traditional-gcc-built-ins-like-sy/79529575?noredirect=1#comment140254189_79529347)
> The two keys are (1) have the variables in separate cache lines; (2) have a test that you can repeat quickly, without re-running the program or spawning new threads every time, and that isn't reliant on any particular timing synchronization between the threads.

``` cpp
//! Nate Eldredge
//! https://stackoverflow.com/questions/79529347/the-sequential-consistent-order-of-c11-vs-traditional-gcc-built-ins-like-sy/79529575?noredirect=1#comment140254189_79529347

// compile with g++ -O3 -std=c++20
// reproduces LoadLoad reordering rapidly on ARM Cortex A-72, Cortex A-76, and Apple M3 at least

#include <thread>
#include <atomic>
#include <iostream>
#include <cstdint>
#include <cassert>

alignas(256) std::atomic<uint64_t> x{1}, y{1};

void thr1() {
    uint64_t c = 2;
    while (true) {
        x.store(c, std::memory_order_seq_cst);
        y.store(c, std::memory_order_seq_cst);
        c++;
    }
}

void thr2() {
    while (true) {
        uint64_t xx, yy;
        yy = y.load(std::memory_order_relaxed);
        if (yy != 0) { // always true but imposes control dependency
            xx = x.load(std::memory_order_relaxed);
            if (xx < yy) {
                std::cout << "Got x=" << xx << ", y=" << yy << std::endl;
                std::exit(1);
            }
        }
    }
}

int main() {
    std::thread t1(thr1);
    std::thread t2(thr2);
    t1.join();
    t2.join();
    return 0;
}
```
