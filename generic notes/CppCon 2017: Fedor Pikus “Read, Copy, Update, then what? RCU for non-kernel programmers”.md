[CppCon 2017: Fedor Pikus “Read, Copy, Update, then what? RCU for non-kernel programmers”](https://www.youtube.com/watch?v=rxQ5K9lo034)

## Motivation

Notice that concurrent access is in general not so cheap. OTOH _read-acquire_ and _store-release_ remain relatively cheap and rather scalable.

It would be nice if we may take advantage of the fact that the shared data is rarely updated or writer is basically not contended (and thus slapping a `Mutex` won't in anyway hurt performance)...

## Publishing Protocol

The writer prepares some data which is invisible to the outside world when WIP, and only when that data is finally ready the writer reveals it to the outside world via modifying some **root token**.

Often there's 3 major steps: Read, Copy, and Update. Thus the name RCU.
But that's _not_ what makes RCU a RCU. It's the **delete** that matters... More on delete later.

For instance, consider this C snippet:

``` C
struct node {
    struct  node* next;
    uint32_t      val;
};
static atomic_uintptr_t g_head = NULL;

void push_front(uint32_t u) {
    struct node* new_node = (struct node*)malloc(sizeof(struct node));
    new_node -> val  = u;
    new_node -> next = NULL;
    // Classic weak compare exchange loop.
    //
    // Here we intentionally set `next` of the new node to NULL for brevity:
    // If the global head is NULL, this exactly what we need.
    // If the global head points to some node, we retry, just like any other CAS loop.
    while(!atomic_compare_exchange_weak_explicit(&g_head, &new_node->next, new_node, memory_order_release, memory_order_relaxed)) {
        // upon fail, new global head is loaded into `next` field of the `new_node` for next iteration retry.
        // `memory_order_relaxed` suffices since we don't depend on other threads' value during push.
    }
}
```

We Read the current data (by CAS failing and loading current `g_head` pointer), Copy that value into our own field, then Update with _store-release_ operation. Read, Copy, Update, RCU, or the publishing protocol. But again, RCU isn't what makes RCU RCU. It's the delete that's unique to RCU that makes RCU RCU.

## It's Always The Delete

> But the general point is, it's always the delete that vies you the problem. Lock-free algorithms, ABA problem in the lock-free list, same thing. Without the delete, there's no problem. It's always the delete. **Hazard pointers**, same thing. If you didn't have delete, you wouldn't need to worry about the hazard pointers.

(RCU chunked array)
> The easiest solution to that problem for that particular container, is to never delete old reference blocks, period. The memory overhead is negligible you will never notice, if you choose the size of the data block sufficiently large.

> You can if you want use publishing protocol with atomic shared pointer, reclaiming memory by atomic shared reference count and shared pointer, but using publishing protocol to reveal the memory. There is a way to do it, it will work, but it's not RCU.

## RCU memory reclamation: the real RCU

> The RCU implies that you're not changing data in place, you're using the publishing protocol.

> Cooperative handshake protocol

- Reader
  - > `rcu_read_lock` to request access, get root token (and not use the old copy), then `rcu_read_unlock` to announce end of access
  - > reader-side critical section
  - > readers in **quiescent state** don't read shared data
- Writer
  - > make old shared data inaccessible from the root token
  - > `synchronize_rcu` to wait for all readers who called `rcu_read_lock` before last step to call `rcu_read_unlock`
  - > delete old data and reclaim the memory
  - > don't need to wait _all_ readers to exit critical section, which may not happen at all, only those who acquired the old root token

> But there's a way, somewhere, to traverse some data structure inside the RCU system, count something or add up something, and figure out how many readers you currently have outstanding. So conceptually there's a reference count. It could be distributed as a list of nodes. I have a node for every reader, how many nodes I have in the list, that's my reference count. That's one of the implementations.  If you went to Ansel's talk on [libguarded](https://github.com/copperspice/cs_libguarded) he showed you every reader coming in was putting a node on the zombie list. There isn't a reference count in that system, but if you could, let's freeze the systemand have a god's eyes which I could look down on the entire system, I could count the entries in the zombie list. That would be the reference count.

## Kernel RCU

Kernel knows when context switches happen, quiescent states, it runs the threads.
Kernel knows how many threads there are and which ones are running.
Kernel already has various synchronization to do, so no extra memory barrier required: RCU basically free rides those synchronizations.

[???????](https://youtu.be/rxQ5K9lo034?t=2174)
> because the kernel has to make sure, when the readers aren't in the quiescent period, they are not running any instructions, which means something has to synchronize them, at this point, up to this point, nothing else on this thread is running, that's equivalent of a memory barrier, and that's good enough.
probably a typo in the YouTube SRT... "when the readers _are in_ the quiescent period they are not running any instructions" might be more appropriate, as [Hierarchical RCU](https://lwn.net/Articles/305782) suggests:
> This means that any time a given CPU is seen either blocking, in the idle loop, or exiting the kernel, we know that all RCU read-side critical sections that were previously running on that CPU must have completed. Such states are called “quiescent states”, and after each CPU has passed through at least one quiescent state, the RCU grace period ends.

> It doesn't change the basic idea. But it's important to understand the implicit assumptions, what you can and cannot et away with. For example, if you look at the read-lock in the kernel, you would see that it actually does not have an acquire barrier, doesn't need one. It's freeloading on some other barrier. But it doesn't mean that entering the critical section does not require a barrier at all. It just means that they already had one, and they're basically taking advantage of it, which is a good thing, it makes your code faster, and they had to have that other barrier, so it's not wrong, it's just, if you're trying to understand it, you have to be aware of the implicit assumptions in the particular RCU protocol.

## User space RCU implementation (one of many)

Writer global atomic **generation**/**epoch**, garbage queue per generation whenever some data (within that generation) got invalidated (so still accessible by some readers).
Writer may decide to bump generation.
Writer tracks how many readers access each generation.

> Once I revved up the generation, I have to wait for readers to leave. How do I know when the readers leave? Well, remember my readers, how they were storing the pointer to the data. Well, they don't store the pointer to the data anymore. What they do store is the generation at which they entered the critical section, so when they call `rcu_read_lock`, I told them okay, you're calling `rcu_read_lock`, current generation is generation N, hold onto that N, you'll need it. When they're calling `rcu_read_unlock`, they're saying Iaam a reader from genertaion N, on the writer side I'm counting okay, generation N has been superseded by N plus one, I have three readers from generation N, one just exited, two just exited, three just exited, okay, go to town, collect garbage.

Lamport algorithm: one `SeqCst`, one `Acquire`, and one `Release` barrier.
Usually some `SeqCst` is unavoidable, or some more atomics are involved using `Acquire` and `Release` to kinda fake it, at the same time this alternative might or might not be more efficient than just using `SeqCst`.

> You can't jump a generation (during garbage collection in `synchronize_rcu`), you have to collect them in order, because remember, all the ones that were earlier can see everything that came after that, it's just all the ones that came later cannot see what was removed before. But all the ones that came first, they see everything that was there.

Wait what? Well, remember earlier Fedor Pikus mentions

> When the writer does an update, and makes some data inaccessible, let's stick with out list. So I'm pulling a node out of the list, replacing it with a new node, I will put the old node in, let's say generally it's a garbage queue, in my case it's just a list of nodes to delete, and it's another list, but in general it's some queue of garbage.

So in general writers only do partial updates and only revs up generation when it sees fit. And one can imagine it's a sort of linked list or maybe a tree. Thus we just can't jump generations when doing `synchronize_rcu`: they might be accessible from older generations, if there are still some readers still in older generation!

> Well they (readers) don't store the pointer to the data anymore, what they do store is the generation at which they entered the critical section.

So we've seen a RCU that's not exactly reference counted: it's counting how many users of particular generations. It's not exactly `Arc` in Rust terms nor `std::shared_ptr` in C++ terms!

RCUs may have various flavors; [libguarded](https://github.com/copperspice/cs_libguarded) is heavier on the reader side to achieve its design goal, non-blocking readers is. OTOH you might want to optimize for reader overhead. What overhead? Mainly the RMW operation on the generation counter, which is a cache contention on the cache line: remember, _load-acquire_ are cheap, but RMWs are like orders of magnitude more expensive.

Kinda like sharding: each reader thread has its own count: readers shall register in advance with the RCU, since the RCU has to know how many threads you have, so it can allocate this array of reference counts. Writer must loop over all reader slots to add up the count. Or maybe not: use thread ID and some hash algorithm to have fixed number of slots.

> ...please pad it to the cache line. You don't want the false sharing here, to go to all this trouble and then die because your reference counts are in adjacent eight bytes. No, space them out by 64 bytes, well, if you are on x86, or whatever your cache line is.

> That actually move a lot of the work into the writer, because now writer, instead of just accessing one reference count, has to access them all and add them all up.
> Ooh, scary, not atomic. The good thing is the reference counts for the old generation only go down. They don't go up. So once they all reach zero, that's it.
> And that's what makes this schema workable.

Another way is to ditch the generational reference count entirely since it's more or less prepared for nested critical sections, i.e. `rcu_read_lock` inside `rcu_read_lock`. If you don't need that, you may instead prepare each reader a tag field, which is either `None` or `Some` with the generation its current read critical section corresponds to: it's basically a 0/1 counter for each reader. And often you do may live without nested critical section support. Again, pad to cache line to prevent false sharing!

Often RCU cleanup are done by enqueuing callbacks and the nodes to delete and batch the work or maybe hand over to yet another (dedicated) thread, s.t. the writer thread doesn't block.

> - **Guard Object**: each update of each root pointer effectively advances the generation.
>   - more overhead, less unclamied garbage

## Mean Writers and Meek (or just Fault-Tolerant) Readers

What if some readers hold onto some generation for extended an period of time?

Some implementations allows the writer to forcibly reclaim the memory!

- memory reused but not freed (so not `free` but instead local memory allocator)
  - readers just accept that the data might be stale and inconsistent, and maybe they are even totally cool with it.
  - RCU paradise
- memory reused, readers may check if that data is stale
  - maybe when `rcu_read_unlock` there's some flag telling if that data had expired
  - and maybe just redo the required work if it did expired
- yet another scenario is readers are OK to just crash
  - they are stateless and completely fine to be restarted

## QA section

### Frequent Updates?

Batching or delegate clean up to another thread both have its cons. For batching, obviously the writer might effectively be blocked for a while for its doing the clean up jobs, and that might have to throttle or queue the updates, which might or might not be acceptable. For another dedicate cleanup thread, there's contention if updates are frequent (and thus cleanups are frequent, too).

Maybe you need to re-evaluate your design decision, maybe give up on sequential consistency somewhere, or maybe _statistical queue_, give up on sequential consistency on the queue and go for _probabilistic queue_: those are faster at the expense of sequential consistency.