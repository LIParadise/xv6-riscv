# [CppCon 2017: Fedor Pikus “C++ atomics, from basic to advanced. What do they really do?”](https://www.youtube.com/watch?v=ZQFzMfHIxng)

> Yeah, X is now atomic int. Okay, so what's really going on now in the hardware, that I changed my non-atomic int into atomic int? Well, each CPU does the increment as a single atomic operation. There is a read-modify-write atomic, no interruptions.
> Okay, what's really going on deeper? Well, what's really going on deeper is the core gets **exclusive access** onto the memory. The value trickles up and down through the caches if necessary. Now, that's a simplified picture. They may instead be talking to each other's caches. But that's the idea. Whether the hardware actually implements it in terms of communicating between different caches of different CPUs or through the main memory doesn't matter for our purposes. It will matter for exactly how slow it runs, but conceptually, you can't tell the difference. So this is really how that atomic operation is built up.

## Is atomic the same as lock-free?

This is mostly a C++ problem: in general it's not known until **runtime**, since padding and alignment matters.

``` cpp
std::atomic<T>::is_lock_free();
constexpr is_always_lock_free(); // C++17
```

``` cpp
long x;                               // lock-free
struct A { long x; };                 // lock-free
struct B { long x; long y; };         // Runtime-dependent!
                                      // `struct B` in C on x86-64 has 8-byte alignment
                                      // Depending on the platform, to make 16-byte values atomic, it might require 16-byte aligned
                                      // In particular on x86-64 it's atomic iff happens to be 16-byte aligned: `mmx` registers.
struct C { long x; long y; long z; }; // x86-64 doesn't natively support atomics for greater than 16-byte as of the talk (2017)
struct D { int i; int j; int k; };    // 12-byte, on x86-64 it's not lock-free
```

## Does atomics wait on each other?

It's hardware dependent. On x86-64, granularity of memory is cache line, which is _64 bytes_. Thus we might notice that two threads both `fecth_add(1)` towards the same atomic variable may not necessarily be slower than two threads `fetch_add` two different atomics: those atomics might happen to live on the same cache line, say they are in an array, which would imply the cache line is enduring concurrent access, meaning the situation is effectively the same as when there's only one atomic.

> - Atomic operations do wait on each other
>   - In particular, write operations do
>   - Read-only operations can scale near-perfectly

> - Atomic operations have to wait for cache line access
>   - Accessing different locations in the same cache line still incurs runtime penalty (false-sharing)
>   - Avoid false sharing by aligning per-thread data to separate cache lines
>     - On NUMA machines, maybe even separate pages

## `compare_exchange_weak_explicit`: why `weak`, why allow for spurious fail?

> And the most common explanation that I read is something along the lines of you have these different cores, and they have to communicate to each other, and it's kind of like timing out on a socket. You wait, and if you couldn't get the answer from the other core in some timeout, you just return false.

``` cpp
// pseudo code for CAS hardware implementation
// not a real mutex, but some form of exclusive access implemented in hardware
bool compare_exchange_strong_0(T& old_val, T new_val) {
    Lock L;               // get __exclusive access__
    T tmp = atomic_value; // current value of the atomic
    if (tmp != old_val) {
        old_val = tmp;
        return false;
    } else {
        atomic_value = new_val;
        return true;
    }
}

// Again, pseudo code for hardware.
bool compare_exchange_strong_1(T& old_val, T new_val) {
    T tmp = atomic_value; // read operation is fast: optimize for it!
    if (tmp != old_val) {
        old_val = tmp;
        return false;
    }
    Lock L;               // only now try get exclusive access
    tmp = atomic_value;
    if (tmp != old_val) { // double-checked locking pattern
        old_val = tmp;
        return false;
    } else {
        atomic_val = new_val;
        return true;
    }
}

// hardware pseudo code
bool compare_exchange_weak(T& old_val, T new_val) {
    T tmp = atomic_value; // Read operation is fast: optimize for it!
    if (tmp != old_val) {
        old_val = tmp;
        return false;
    }
    TimedLock TL;         // Exclusive access may be difficult to get. Timing out seems to be a good idea.
    if (!TL.is_locked()) {
        return false;     // This is why spurious failure might happen: exclusive access acquisition timed out.
    }
    tmp = atomic_value;
    if (tmp != old_val) { // Double-checked locking pattern
        old_val = tmp;
        return false;
    } else {
        atomic_val = new_val;
        return true;
    }
}
```

(???)
> That's the double-checked locking pattern, back for you, from the grave. Only the hardware can actually do it.

## Atomic Variables as Gateways to Memory Access (Generalized Pointers)

> And we need these guarantees, we need guarantees of the sort, like if I'm publishing a new node, all other threads must see that memory of the new node in its final state. I'm done preparing it, I did the atomic swap. I must have a guarantee that my non-atomic writes to the list node have completed and become visible to everybody else. Otherwise, this atomic pointer is worthless. Now, that's done by the memory barriers. And memory barriers really go hand-in-hand with atomics. You really can't understand atomics if you don't understand memory barriers.

> There's no guarantees that anybody can see anything.
> So it's a global control of visibility across multiple CPUs, has to be supported in the hardware, in practice it's often not a special instruction although it can be, it's often an attribute on some other instruction.

## Mandatory Gripe about The C++ Standard

> Wouldn't be a talk on C++ if I haven't complained at least once about the C++ standard.

> What did you just say to me by writing this code? You said, be afraid. Why did you say that? Because somebody else is reading that atomic variable at the same time on a different thread, while the object is being destroyed. That's what you said. That's why you're doing an atomic read. Well, of course there is another possibility. You couldn't do any other way of reading it. You really wanted to say non-atomic read. And there isn't one.

``` cpp
class DropGuard<T> {
    std::atomic<size_t> s;
    T* p;
};
C::~C() {
    clean_up(p, s.load(std::memory_order_relaxed));
}
```

## When to Use Atomics?

> Ever tried to do lists with a lock? Well, when you don't want to lock the entire list. You can put a spin lock on every node. That's not very expensive. You're going to deadlock really quickly. Not impossible, but hard to do correctly with locks. Not that hard to do with lock-free, with compare-and-swap. There are other drawbacks to locks. Locks are non-composable. You try to compose locks, you get _deadlocks_. You get _priority inversion_, you get _latency problems_ with locks. Lock-free algorithms exist to solve all of those problems.

## Questions

> - I've heard some state that `compare_exchange_weak_explicit` may yield better performance than `compare_exahcnge_strong_explicit`, how is it possible?
> - But the getting exclusive access part, which is the global locking down of the entire, of everybody's access to the cache line while you're doing your write, that, on some hardware platforms, is very expensive. And yes, it may be better, basically, for this core to give up its write to the exclusive access and let somebody else do their update first. Essentially, if you're doing this kind of timed lock, and if you're waiting for a long time, it means somebody else is really trying to do something to that same cache line or the same memory location, and they're in a better position. It's local, maybe it's local cache to them. So okay, let them do it. So on some platforms, giving up on that exclusive access and essentially going for a spin on the compare-and-swap loop will result in a faster performance overall, across all threads, because you let another thread that can do it faster, you let that one go first. It requires that you have platforms that have a hardware that basically plays something like this double-check locking trick. It's not the only implementation that does it, but something along these lines. On other platforms, so if you look at X86, you can just look at assembler-generated code. Compare-and-exchange, strong compare-and-exchange would generate the same instructions. So X86 just doesn't have enough tricks in its bag to make use of that. Now, you can still say compare-exchange weak, and you wouldn't be wrong. Just because it can spuriously fail doesn't mean it will. Your program obviously has to handle if it might fail, but what if it never fails? Your program has to work for that condition. So it's not wrong to use it if you're just spinning on it and hammering the atomic over and over in this compare-exchange loop, it's usually better to do weak just in case you're on one of those platforms where getting a long-range lock is expensive and it's better to let the owner of that location go first.

> On X86 there is no difference between acquire-release and sequentially consistent.

> - At the very beginning when you showed the demonstration of adding a whole bunch of integers, and you showed that the lock-free one was, of course, much slower in your benchmark, is that primarily due to the sequential consistent memory barriers?
> - Now, in this case it's not really because of the barrier so much. It's because of the **cache line bouncing**. So even if I had a true relaxed, let's assume that I had, on X86 I had the true relaxed, meaning I increment it, and I get no additional guarantees about any other memory except for this atomic variable that I'm incrementing. It would still do the cache line bounce from core to core as I'm incrementing cause I want to write into this variable, says this core. Okay, send me the cache line. I'm going to lock this cache line and increment. Oh, I want to write into this. Send me the cache line all the way back. I'm going to lock it and increment. And the cache line keeps bouncing back and forth between the cores, because each core has to have exclusive access in order to do its increment. So even if you had true relaxed, it wouldn't really make it anywhere close in performance to what you saw in that, the lock-based, the mutex there is a gimmick. The bulk of this algorithm is adding into the local sum accumulator. After I've added thousands of values, yeah, at the end I go and do one add into the global, and nobody cares how I do it. It happens so infrequently. It's once per entire computation, the lock-based. Mutex, spin lock, atomic, whatever. I do thousands of accumulations locally, in my thread local variable, the one on the stack, millions of accumulations, however long you want to run the benchmark. Cause I knew I do one atomic commit. Under lock, do whatever you want, at this point. Doesn't matter. So lock is really a gimmick there. You have to have a lock, but it doesn't matter what you do with it. (...) The atomic lets you write correct code wtihout that lock. Correct, doesn't mean it's not stupid.

> - Can you give a hint of how the memory barriers are actually implemented in hardware? And is there a scope, like do they have anything to do with cache flush and cache load?
> - Yeah there is some sort of global coherency being established, what exactly is being established depends on what it has to establish. So first of all, pretty much everybody, even the systems with very strict memory model like X86 have store buffers. So that's the first thing being affected by the memory barrier. It has to basically flush the store buffers. Because on X86, actually it wouldn't have out of order writes if you didn't have the store buffers. The caches themselves are **DSO**, **double store ordering**. But you have store buffers, so the stores on the individual CPU are accumulated in the store buffer. So while the CPU waits for the cache line to be sent from another CPU and locked for exclusive modification, the values themselves, the ones that you want to write, are accumulated in the store buffer, which means other CPUs can't see them at all. So if you have a memory barrier, the first thing that must happen, everything in that store buffer has to be committed before other CPUs will be allowed to proceed. Beyond that, it depends. So if you're on the same socket, typically you have cache-to-cache communication protocols, meaning you don't have to go through main memory. If you're on different sockets, you go through the UPI link or its equivalent. So you don't go through main memory, but there is basically like a socket on the microscopic level. It's not trivial. It heavily depends on what hardware you're using. And the performance characteristics will depend a lot, again, on the hardware. So if you benchmark the same code on X86 and on ARM, you will see, and for example you measure how it scales, you start taking memory barriers and see basically how much the contention cost you. You would notice totally different, on X86 it might basically, it starts off really fast, and then as soon as it kicks into contention, it just slows down to sequential. And on ARM, you might see that it starts much slower, but it keeps scaling almost perfectly for a lot more threads before you start noticing slowdown. And that has to do with, well, the cores themselves being slower, but the memory model being more relaxed on ARM than on X86.
