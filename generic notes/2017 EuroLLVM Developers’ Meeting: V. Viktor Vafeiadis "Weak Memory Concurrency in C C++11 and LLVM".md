# EuroLLVM

[YouTube](https://www.youtube.com/watch?v=BwKkcTfAd8)

C11 axiomatic graph
execution graphs (possible executions)

> again you put your **program order** edges your **reads from** edges, but whenever an _acquire read reads from a release write_ then this **read from** edge also establishes a **synchronization**. A **synchronization with** edge according to the model and now the model defines **happens before**; an event happens before another one if there is some path of program order edges and synchronization edges that goes from the first and and then to the second one.
> so some other constraint of the model will then say that since this read is knows about this write it can't, because it happens before it, it cannot read from some earlier write, so it cannot read say initial write `0`.

$\text{hb} \overset{\Delta}{=} {\left( \text{po} \cup \text{sw} \right)}^{+}$

Note **sequentially consistent** is often also referred to as the **interleaving** model.

``` c
static atomic_int_fast32_t x = 0;
static atomic_int_fast32_t y = 0;

void thrd_0() {
    x.store(1, memory_order_relaxed);
    printf("%d\n", y.load(memory_order_relaxed));
}

void thrd_1() {
    y.store(1, memory_order_relaxed);
    printf("%d\n", x.load(memory_order_relaxed));
}
```

Note how the TSO x86-64 allows both line to print zero; Power and ARM also may print zero. And C11 captures this idea since nobody happens before any body.

**Coherence**

> in particular, if a program stores with relaxed stores to a location so the few values in order, then you cannot read them out of order (from all hardware threads), so you get what is known as **coherence**; if your program consists of just **one memory location accesses** just one location then your program has **sequential consistency** semantics.
> The model does it by defining some auxiliary relations, **modification order** or $\text{mo}$, **reads from** or $\text{rb}$, but they are not relevant to the rest of this talk.

**Modification order**: $\text{mo}_x$ is the total order of writes to $x$.
**Reads-before**: $\text{rb}_x \overset{\Delta}{=} {\left( \text{rf}^{-1}; \text{mo}_x \right)} \cap \left( \neq \right)$
**Coherence**: $\text{hb} \cup \text{rf}_x \cup \text{mo}_x \cup \text{rb}_x$ being _acylic_ for all memory location $x$.

## Causality cycles with relaxed accesses

> Note that C11 allows the following snippet to end up with `x == 1 && y == 1`

``` c
static atomic_int_fast32_t x = 0;
static atomic_int_fast32_t y = 0;

void thrd_0() {
    if(1 == x.load(memory_order_relaxed)) {
        y.store(1, memory_order_relaxed);
    }
}

void thrd_1() {
    if(1 == y.load(memory_order_relaxed)) {
        x.store(1, memory_order_relaxed);
    }
}
```

> I mean you could try to place a constraint, but then it will rule out the behaviors of other programs that the model wants to allow.

## No Causality Cycles with Non-Atomics

``` c
static int32_t x = 0;
static int32_t y = 0;

void thrd_0() {
    if(1 == x) {
        y = 1;
    }
}

void thrd_1() {
    if(1 == y) {
        x = 1;
    }
}
```

> C11 _forbids_ the outcome `x == y && y == 1`
> This program here if you run it under interleaving semantics, we'll _never_ have a race, because under interleaving semantics, the only values that you could read here are `0`, and so the stores will _never_ happen, and so you have a program that under interleaving semantics has no races, so you'd hope that even with this (C11) weak memory model semantics you'd still have just the same behaviors as interleaving one; this is a guarantee that's called the **data race freedom**, granting if your program has no races under interleaving smenatics, then it should have precisely the interleaving behaviors. To make this happen, the model requires that **read from** edges for non-atomics are included in **happens-before**, this means you can read only from events that were before the writes, so only from the initialization ones, not from concurrent ones (in this example), for non-atomics.

**Non-atomic read axiom**
$\text{rf} \cap \left( {\_} \times \text{NA} \right) \subseteq \text{hb}$

Note it's an _axiom_: the original formulation by Adve and Hill is that if the program were to exhibit no possible execution that has _data race_, where the term _data race_ refers no _synchronization operation_ as defined by the _weak memory model_ done between two events that are not both read, on a sequentially consistent machine, then the program should behave as such on the hardware that respects the _weak memory model_.
Here C++11/C11 does similar: it just assumes that all non-atomic reads are reading from values that guarded by _happens-before_: that's what the _synchronization operation_ in the weak memory model here, i.e. the C++11/C11 _synchronizes with_ relation brings us.
In other words, the criteria for some program to be racy (in particular related to some non-atomic memory location) is via the DRF, i.e. reads of non-atomic memory location are guarded by _happens-before_.

## Is the C11 memory model definition

1. Mathematically sane?
    - **No**, in the sense adding a lock may introduce _more_ behaviors!
2. Not too weak?
    - It's mostly alright if one ignore `memory_order_relaxed`, which causes OOTA.
3. Not too strong?
    - Compilation to x86/Power/ARM are fine, except **no**, it turns out the compilation proofs are actually broken.
4. Actually useful?
    - **No**, in the sense some intended optimizations that compilers have readily performed

## Sequentialization Is Invalid

``` c
static int32_t a = 0;
static atomic_int_fast32_t x = 0, y = 0;

void func_0() {
    a = 1;
}

void func_1() {
    if(1 == x.load(memory_order_relaxed)) {
        if(1 == a) {
            y.store(1, memory_order_relaxed);
        }
    }
}

void func_2() {
    if(1 == y.load(memory_order_relaxed)) {
        x.store(1, memory_order_relaxed);
    }
}
```

Claim: if the three functions were run on separate threads, then it's a valid program with the only possible result in which no atomic store had happened: `a == 1 && x == 0 && y == 0`.
Proof: by DRF and thus non-atomic read axiom, in order to claim the program suffers _data race_, we consider only the cases when non-atomic reads are guarded by _happens-before_, in this case we consider only `a == 0` the initial write got read, meaning store to `y` never happen, meaning store to `x` never happen, meaning both atomics' $\text{mo}$ consist only of the initial `0`, meaning there's _no_ read on `a` in the first place, so the program is indeed considered to be DRF by the C++11/C11 memory model.

So far so good, till we sequentialize the functions... say one thread runs `func_0` and `func_1`, while the other thread runs `func_2`. Now we _do_ have to consider that loads _do_ happen, and this becomes a classic example of `memory_order_relaxed`: one may endup discover that the program ends up with `x == 1 && y == 1`! I.e. sequentializing threads under the C++11/C11 model may well make valid programs _invalid_, in the C++11/C11 sense that is.

One may try to purge the _non-atomic read axiom_, but then the models becomes too _weak_, e.g. now non-atomic values suffer OOTA, too.

Another possible counter measure is by forbidding $\left( \text{po} \cup \text{rf} \right)$-cycles, in an effort eliminating causal loops, but it forbids some reorderings and is costly on Power and ARM.

As for **monotonicity**... it turns out all these operations may introduce _more_ behaviors, which violates **monotonicity**:

Roughly, **monotonicity** refers to that _adding synchronisation should not introduce new behaviours_

1. _reducing parallelism_
    - $C_1 \vert\vert C_2 \leadsto C_1; C_2$
2. _expression evaluation lineaarisation_
3. Adding memory fences
4. Strengthening access mode of an operation
5. Roach motel reorderings
    - move something _into_ locked region

## C++11/C11 Semantics for Sequential Consistent Accesses Is Broken

PLDI 2017: some behaviors around `memory_order_seq_cst` that are not allowed in C++11/C11 but found to be present on Power/ARM with _trailing sync_ compilation scheme.

## The OOTA Problem

> for the rest of the talk let me let me focus however on the main problem of C11; those other ones are more or less fixable, but this one requires a quite different approach, so let me try to explain it, and then give some hints as to how can it can be fixed.

In particular, the OOTA problem implies the C++11/C11 technically _breaks_ the DRF guarantee:

``` c
//! load buffering and data dependency
static atomic_int_fast8_t x = 0, y = 0;
static int8_t a = 0;

void thrd_0() {
    a = atomic_load_explicit(&x, memory_order_relaxed);
    atomic_store_explicit(&y, a, memory_order_relaxed);
}

void thrd_1() {
    atomic_store_explicit(&x, atomic_load_explicit(&y, memory_order_relaxed), memory_order_relaxed);
}
```

Since the execution graph is _exactly_ the same as that we'd get from replacing the store operation in `thrd_0` to storing a `1`, which is a perfectly fine C++11/C11 memory model program and such execution is actually observed on Power/ARM, we then must admit that this snippet here is also admitted by the C++11/C11 memory model, which follows that _any_ value suffices here: _out-of-thin-air_ i.e. **OOTA**!

This is particularly troublesome considering the following variant:

``` c
//! load buffering and control dependencies
static atomic_int_fast8_t x = 0, y = 0;
static int8_t a = 0;

void thrd_0() {
    a = atomic_load_explicit(&x, memory_order_relaxed);
    if(1 == a) {
        atomic_store_explicit(&y, 1, memory_order_relaxed);
    }
}

void thrd_1() {
    if(1 == atomic_load_explicit(&y, memory_order_relaxed)) {
        atomic_store_explicit(&x, memory_order_relaxed);
    }
}
```

Again, by DRF and non-atomic read axiom, this snippet is data-race-free and thus should yield results that are also obtainable on sequentially consistent hardware, in particular on sequentially consistent hardware the only value that `a` may see is the initialization value `0`: nobody writes to it, both atomics should have only one element in their resp. $\text{mo}$ i.e. the initial value `0`, but as mentioned C++11/C11 (unintentionally) allows _OOTA_ here and they may be just about any value: this "may yeild any value" execution is clearly not obtainable from any sequentially consistent hardware!

## Simple Operational Semantics for C11's Relaxed Accesses

Viktor Vafeiadis proposed a _thread local view_, _shared memory as locations with timestamps_, and **promises**, an **operational model**, and proved that it's in many senses similar to the C++11/C11 one.

Key observation is though _promises_ may work like generic writes in the operational model at first glance, it tries not to admit OOTA behaviors (_load buffering_ $\leadsto$ _load buffering_ plus _dependency_): it tries not to admit similar execution in the latter case.

**Thread-local certification**: a thread can promise to write a message, if it can _thread-locally certify_ that its promise will be fulfilled.
Basic coherence of a single thread is preserved since when thread does a _promise_, it _increases the timestamp_, and if a previous load (in $\text{po}$) loads that value (thus breaking coherence), then that _promise_ may **not** be fulfilled: it can't write with the promised timestamp, since the read that broke coherence would required this write to come up with a larger timestamp!

Consider this snippet, which on Power/ARM is known to produce `a == 1`

``` c
static atomic_uint_fast8_t x = 0, y = 0;
static uint8_t a = 0;

void thrd_0() {
    a = atomic_load_explicit(&x, memory_order_relaxed);
    atomic_store_explicit(&x, 1, memory_order_relaxed);
}

void thrd_1() {
    atomic_store_explicit(&y, atomic_load_explicit(&x, memory_order_relaxed), memory_order_relaxed);
}

void thrd_2() {
    atomic_store_explicit(&x, atomic_load_explicit(&y, memory_order_relaxed), memory_order_relaxed);
}
```

The timestamp/view/promise operation model allows this since the store by `thrd_0` may _promise_ the store with some arbitrary timestamp, say `42069`; the load of that promise in `thrd_1` makes its view of `x` timestamp bumped to `42069` too, but its store to `y` may proceed with a smaller timestamp, say `69`, then the store in `thrd_2` and the load in `thrd_0` may proceed with these smaller than a hundred timestamps, justifying the execution.

In fact if we want to have serialization and some decent amount of compiler reodering, we **have** to admit such execution as possible: say `thrd_0(); thrd_1();`, by constant propagation the store to `y` that originates in `thrd_1` turns into a store of constant `1`, and one may reorder it to the first instruction in the combined program, then such an `a == 1` results follows with _interleaving semantics_.
