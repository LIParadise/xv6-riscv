# Weak Ordering - A New Definition

Leslie Lamport
> (Hardware is **sequentially consistent** if) the **result** of any execution is the same as if the **operations** of all the processors were executed in some sequential order, and the operations of each individual processor appear in this sequence in the order specified by its program.

Adve and Hill interprets the **operations** as all the memory operations or accesses e.g. reads and writes, and the **result** as _the union of the values returned by all the read operations_ plus _final state of memory_. Thus the alternate definition:
1. All memory accesses appear to execute _atomically_ in _some **total order**_.
2. All memory accesses of each processor appear to execute in an order _specified by its program_ (**program order**).

Further, a **synchronization model** is _a set of constraints on memory accesses that specify how and when synchronization needs to be done_. In layman's terms, they _specify the operations or primitives that may be used for synchronization, and indicates when there is "enough" synchronization in a program_.

Hardware is said to be **weakly ordered** with respect to a _synchronization model_ if and only if it appears **sequentially consistent** to **all** the software that obey the synchronization model.

The **Data-Race-Free-0** _synchronization model_ is based on two important concepts.

1. The _abstract idealized architecture_
  - All memory accesses are executed atomically and in program order
  - Slightly _stronger_ than sequentially consistent hardware
    - Since all accesses are atomic
2. The **happens-before** relation is a _partial order_ defined for an **execution** of the program on aforementioned idealized architecture
  - Intuitively, two operations by different processors are ordered by **happens-before** only if there exist intervening synchronization operations bwteen them.
  - $\overset{\textit{po}}{\rightarrow}$ program order
  - $\overset{\textit{so}}{\rightarrow}$ synchronization order
    - `op_a` $\overset{\textit{so}}{\rightarrow}$ `op_b` iff both are synchronization operations accessing the _same_ location, and `op_a` _completes before_ `op_2` in the execution.
  - The **happens-before** $\overset{\textit{hb}}{\rightarrow}$ is the _irreflexive transitive closure_ of $\overset{\textit{po}}{\rightarrow}$ and $\overset{\textit{so}}{\rightarrow}$, i.e. $\left( \overset{\textit{po}}{\rightarrow} \cup \overset{\textit{po}}{\rightarrow} \right)^+$.

Some observations:

1. Note the definitions till now remain somewhat abstract and opaque. The term _synchronization operation_ (and thus _happens-before_) are in itself kinda useless without the introduction of the DRF0 synchronization model in the context of the Adve and Hill weak ordering framework.
2. As a spoiler, in the end we would arrive at something similar to the modern **happens-before** relation in C++/C/Rust.
  - Note that what constitutes as _completes before_ for _synchronize operations_ is subtle in C++/C/Rust, e.g. _release sequences_ i.e. in modification order of some atomic variable, thread A _store-release_ some value, followed by only RMW/CAS operations of arbitrary memory ordering, then _load-acquire_ by thread B, C++/C/Rust counts it as _completes before_ in Adve and Hill terms.

So what are _synchronization operations_ upon which we define _happens-before_ relation? Here's the **DRF0, data-race-free-0** synchronization model, in which
1. The synchronization operations are s.t.
  - recognized by hardware
  - each accesses exactly one memory location
  - (and that's it...! more on that later)
2. For _any **execution**_ on the idealized system, i.e. the one all memory accesses are atomic and in program order, all **conflicting** (two accesses that are not both reads) accesses are ordered by _happens-before_
  - Note the word _any **execution**_.
  - The irreflexive transitive closure of $\overset{\textit{po}}{\rightarrow}$ and $\overset{\textit{so}}{\rightarrow}$ i.e. $\left( \overset{\textit{po}}{\rightarrow} \cup \overset{\textit{po}}{\rightarrow} \right)^+$ i.e. the _happens-before_ relation $\overset{\textit{hb}}{\rightarrow}$ is defined for one execution of one program.
  - Though not quite precise, a [metaphor](https://research.swtch.com/hwmm#drf) is _**all** executions on sequentially consistent machine are absent of conflicts_.
  - Adve and Hill noted that ther may be many possible $\overset{\textit{so}}{\rightarrow}$ relations, so in general ther may be more than one $\overset{\textit{hb}}{\rightarrow}$ partial order defined for a program.

And it's not till now we get more insight into what's the _synchronization operation_ (and thus _happens-before_) that Adve and Hill are talking about: if none of the possible **executions** of a software program running on the _abstract idealized hardware_ contain **conflict**, i.e. all pair of memory accesses to the same location that are not both read are ordered by the partial ordering _happens-before_, then that program when run on DRF0 hardware should produce _results_ as if run on sequentially consistent hardware.

Worth mention is [L. Lamport, Time, Clocks, and the Ordering of Events in a Distributed System](https://amturing.acm.org/p558-lamport.pdf), where Lamport talks about message passing between hardware threads, and came up with a similar _happens-before_ relation: the irreflexive transitive closure of message TX/RX and program order, and made a metaphor for it: _happens-before_ in this sense is like characterizing what events may _casually affect_ other events.
And one may that the C++/C/Rust memory model is deeply influenced by this Adve and Hill weak memory model: it's basically a refined version of DRF0, including but not limited to when does one _synchronization operation_ _completes before_ one another, notably the _release sequence_.
Note that the software programs should be s.t. no possible _execution_ on the _abstract idealized machine_ contain _conflict_; in modern C++/C/Rust language, we software programmers are programming against the [abstract machine](https://sabrinajewson.org/rust-nomicon/atomics/multithread.html). Only when we are free of _conflict_, we are guaranteed to have the program behave in a sequential consistent manner on the conforming weak memory model hardware of the C++/C/Rust memory model.

So I guess the main take away from this paper is the **framework of memory/synchronization models**. It suggests we may devise a (weak) memory/synchronization model serving as the _contract_ between software and hardware. We would need an _abstract idealized machine_, and the memory model should provide characterization for _synchronization operations_, _happens-before relation_, and what constitutes as _conflicts_, or _data-races_. Sofware developers write programs against the _abstract idealized machine_, and if it contains no _conflicts_/_data-races_ across all possible _executions_ on that _abstract idealized machine_, conforming hardware should produce _results_ as if program is carried out on sequentially consistent hardware.

- The abstract machine Adve and Hill chose is the one on which all memory accesses are atomic and respect program order, subtly stronger than a sequentially consistent one.
- The C++/C/Rust memory model is a bit more forgiving in the sense it allows `memory_order_relaxed` to _conflict_ in Adve and Hill terms, i.e. not requiring them to be separated by _synchronization operations_: in C++/C/Rust terms, they are not _data-races_, they are merely [_race conditions_](https://sabrinajewson.org/rust-nomicon/atomics/relaxed.html).