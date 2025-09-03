# [The Fancy Algorithms That Make Your Computer Feel Smoother](https://www.youtube.com/watch?v=O2tV9q6784k)

> ..., instead processes are **contexts** in which the computer operates. To manage these processes, the operating system uses a special structure called the _process control block_ or **PCB**, this struct is not the process itself, but rather a representation of it, containing all the necessary information about the process, such as its state, program counter, registers and other management data.

> (on FCFS/FIFO) We cannot directly push processes into this queue, because processes are _abstract contexts_ and cannot be treated as discrete elements of any data structure. Instead what the operating system does is push the _process control block_ of each process into the queue.

> If we were to map CPU usage of a program over time, we would see gaps where the CPU remains idle waiting for I/O operations to finish before continuing execution. These gaps are not the exception, but the norm, at least in systems based on the Von-Neumann architecture, which represents over 99% of modern computers. In fact this behavior is so common that it has been continuously studied for decades. Those lapses when a program is using the CPU are called CPU bursts, while those time-lapses when the program is waiting for an I/O operation to complete are called I/O bursts. Every process execution begins with a CPU burst that is followed by an I/O burst, which is followed by another CPU burst, then another IO burst, and so on, eventually the final CPU burst ends with a system request to terminate execution.

> As I mentioned earlier, this behavior has been studied for decades, as the success of CPU scheduling largely depends on it, the durations of CPU bursts have been extensively measured, and while they vary between processes and systems they tend to follow a predictable distribution that looks like this.


``` text
   ^
   | Frequency
   |
   |      (peak)
   |         ^
   |        / \
   |       /   \
   |      /     \
   |     /       \
   |    /         \
   |   /           \
   |  /             \
   | /               \
   |/                 \
   |                   \
   |                    --____________  (near 0)
   +------------------------------------------->
                 CPU-Burst Duration
```

![CPU burst versus I/O burst](https://i.imgur.com/hjADfBA.png)


> This is a critical observation for designing CPU scheduling algorithms, because it highlights that processes often spend significant time waiting for I/O operations. If a scheduling algorithm doesn't account for this behavior, a lot of CPU time will be wasted. So when a process enters an I/O burst, ideally the CPU should be allocated to a different process, one that isn't also waiting for I/O operations.

> ...makes the implementation of our scheuler a little more complex, because now we need to consider that processes can be in different states. Every process begins in the _new_ state. A _new_ process is a program that has just been launched, but its executable file is still being loaded into memory. Once the program is fully loaded and all necessary resources are allocated, the process enters the _ready_ state, meaning it is prepared to execute, but is waiting for CPU time. When the CPU is assigned to a process, it enters the _running_ state. At some point the process will likely make a system call such as an I/O request, which voluntarily returns control of the CPU to the operating system. Keep in mind that an I/O call is not only made to request some resource like manipulating a file or allocating memory, but to simply wait for some event to happen, such as an user keystroke or an incoming network request. In both cases, the process moves to the _waiting_ state until the event happens or the I/O operation is completed. Once the request is fulfilled the process becomes _ready_ again, waiting for the scheduler to allocate CPU time.

> Notice that even when the I/O operation is completed, the process does not immediately resume execution. This happens because while the process was waiting, the CPU has most likely been allocated to another process, so even though it is now _ready_ to continue _running_, it must wait for its turn.

``` text
  +--------+
  |  new   |
  +--------+
      |
      | admitted
      v
  +--------+
  | ready  | <--------------------------+
  +--------+                            |
      | selected                        |
      v                                 | I/O or event completion
  +----------+                          |
  | running  |     X <--------+         |
  +----------+                |         |
      | I/O or event-wait     |         |
      v                       |         |
  +----------+                |         |
  | waiting  | ---------------+---------+
  +----------+
      | Exit-call
      v
  +------------+
  | terminated |
  +------------+
```

> Certain operating systems also more finely delineate process states, for example a terminated process may not be really terminated: after making a system call to exit, the operating system must free allocated resources including memory. Some operating systems make a clear distinction between a process that has been fully terminated and one that is still in the process of termination.

> This state model explains why cues are the fundamental data structure used in CPU scheduling, even though there is a specific _waiting_ state, processes in other states except the _running_ state are also waiting. Given that hundreds of processes may be waiting at any moment, using (per-state) queues to manage their execution order is both logical and efficient.

> ...the **dispatcher**. When a running process enters an I/O waiting state and the CPU becomes available, the dispatcher steps in with one single purpose: allocate the CPU to the process at the head of the _ready_ queue, in other words, perform the context switch. Although the running process is not stored in any queue, a reference to its process control block is always maintained. This allows the dispatcher to save the CPU state into the PCB. When an interruption occurs, this saved state enables the process to resume execution later. The dispatcher then places the PCB into the _waiting_ queue. Next after handling the interrupted process, the dispatcher retrieves the PCB of the next process from the head of the _ready_ queue, identifies it as the new _running_ process, restores its previous CPU state, including registers and the program counter, and reallocates the CPU, allowing the process to resume execution from where it was interrupted.

> In other words, the scheduler determines which process should use the CPU next, by managing the _ready_ queue according to a specific scheduling
policy, while the dispatcher is responsible for the execution of the decision made by scheduler.

> **dispatch latency**, the time it takes for the dispatcher to stop one process and start another.

> Imagine we have one CPU bound process and some I/O bound processes; for convenience I'm going to use the same color for the I/O bound processes. The CPU bound process will get and hold the CPU for a relatively long time; during this time, all the other processes will finish their I/O and will move into the _ready_ queue waiting for the CPU. While the processes wait in the _ready_ queue, the I/O devices are **idle**; eventually the CPU bound process finishes  its CPU burst and moves to an I/O device. All the I/O bound processes which have short CPU bursts execute quickly and move back to the I/O queues. At this point, there's **no** process to dispatch, and the CPU sits idle.

> Convoy effect

> most people in books refer to it as **shortest job first** so once again let's stick to that convention. This algorithm can be implemented using a priority queue, where priority is determined by the length of the **next CPU burst** of the waiting processes. This allows those short CPU burst processes to overtake those CPU hungry processes, reducing the convoy effect. This algorithm is also **provably optimal** in that it gives the **minimum average waiting time** for a given set of processes.

[what is the difference between waiting time and respond time in cpu scheduling](https://stackoverflow.com/questions/40826302/)
> **waiting time**: time a process spends in the (_ready_) queue waiting for the CPU, a crucial metric since it represents the time that processes are ready to run but cannot execute as they wait for their turn to use the CPU.
> **response time**: duration between job submission and getting the first time to be executed by CPU

[CPU Scheduling](https://www.cs.uic.edu/~jbell/CourseNotes/OperatingSystems/5_CPU_Scheduling.html)
> - **Throughput**
>   - Number of processes completed per unit time.
> - **Turnaround Time**
>   - Time required for a particular process to complete, from submission time to completion. (Wall clock time.)
> - **Waiting Time**
>   - How much time processes spend in the _ready_ queue waiting their turn to get on the CPU.
>   - **Load Average**
>     - The average number of processes sitting in the ready queue waiting their turn to get into the CPU. Reported in 1-minute, 5-minute, and 15-minute averages by `uptime` and `who`.
> - **Response Time**
>   - The time taken in an interactive program from the issuance of a command to the _commence_ of a response to that command.

Note that **throughput** and **turnaround time** are not exactly directly inverse to each other: consider two simple pure CPU jobs A and B, A taking 10ms while B taking 20ms, both committed to the OS at time zero. Say we only have one core, and OS decides run A till complete then B till complete, then turnaround time for A is 10ms (perfect), while for B it's 30ms (suboptimal), throughput being one per 15ms. OTOH if OS decides to run B till complete before running A till complete, turnaround time for A is 30ms (suboptimal) while that for B is 20ms (optimal). Throughput is the same, but average turnaround time is different!

Silberschatz, A., Galvin, P.B., & Gagne, G. (2018). Operating Systems Concepts. Wiley.
Turnaround time may not be non-increasing w.r.t. time quantum: consider 4 processes arriving at time zero, scheduler RR with time quantum preempt:
| process | required CPU time |
| ------- | ----------------- |
| P1      | 6                 |
| P2      | 3                 |
| P3      | 1                 |
| P4      | 7                 |
It turns out that with time quantum 1, avg. turnaround time is 11, whereas if time quantum 5, due to FCFS inside RR, it's actually worse at 12.5.

> important considerations to keep in mind whenever scheduler relies on priority cues as we'll explore later, but there's one major issue specifically tied to shortest jobs first: it is impossible to implement perfectly, because unless we could see the future, there is no way to know the exact length of a process's next CPU burst.

> $\tau_{n+1} = \alpha t_n + (1-\alpha) \tau_n ,~ 0 < \alpha < 1$, where $\tau_0$ is typically chosen by OS, either by observing system-wide average or some hard coded value.

> non-preemptive systems rely on processes to behave correctly and return control in a reasonable amount of time. Malware could exploit this by intentionally avoiding system calls to monopolize CPU time. Whenever a scheduling policy causes a **process to wait indefinitely in the ready queue**, this is called **starvation**, because the process is starving for CPU time. Even CPU bound processes can suffer from starvation. For example, in shortest job first, a process with a long estimated CPU burst may be forced to stay in the queue indefinitely if many shorter CPU bursts continue arriving. The only way for this process to get CPU time is if no other process is in the ready queue. In real world systems this is unlikely since hundreds of processes compete for CPU time.

I.e. a long CPU time job in a non-preemptive setup may cause others to starve, and in turn, depending on the scheduler policy, it might cause itself to starve later!

**Round robin** scheduler sets a upper bound on the time a process may hog onto the CPU; if it volutarily relinquishes the CPU, say an I/O syscall, then it's put into the back of the _waiting_ queue, otherwise it's preempted and put at the back of the _ready_ queue, waiting for its next quantum. Typically the timer is a hardware device which is usually directly built into the CPU itself. Too high a time quantum reduces RR to FCFS, but too low a quantum is pure overhead context switching.

``` text
                +--------+
                |  new   |
                +--------+
                    | admitted
                    v
                +--------+
  +-----------> | ready  | <--------------------------+
  |             +--------+                            |
  | RR interrupted  | selected                        |
  |                 v                                 | I/O or event completion
  |             +----------+                          |
  +------------ | running  |     X <--------+         |
                +----------+                |         |
                    | I/O or event-wait     |         |
                    v                       |         |
                +----------+                |         |
                | waiting  | ---------------+---------+
                +----------+
                    | Exit-call
                    v
                +------------+
                | terminated |
                +------------+
```

> Here we need to start talking about another scheduling criteria, the **turnaround time**, defined as the total time taken from a process's creation to
its completion. Mathematically it is the sum of the time spent waiting in the ready queue, the time spent executing on the CPU, and the time spent doing I/O operations. Another useful metric used as scheduling criteria is the **throughput**, defined as the number of processes that are completed per time unit. Again, if the time quantum is smaller than the average context switch time, the CPU will be busy but constantly switching between processes rather than executing them. Maximizing CPU utilization is meaningless if the CPU is not doing useful work.

> However, **turnaround time** is not always the best criterion for measuring scheduling efficiency; often a process can start producing output before completing execution, and as we stated earlier, some processes may even never terminate, thus another important metric is the **response time**, defined as the time from submission of a request to the first response produced.

> **Priority scheduling** is implemented using a priority queue where each process is assigned an integer value representing its priority. In this example I'm using one and zero to distinguish between high and low priority, but the actual range of priority values varies depending on the operating system. Some systems may use a range from 0 to 255, others might go from 0 to 32000, especially when a finer granularity of priority levels is needed. Whether lower numbers represent higher or lower priority also depends on the system.

> Depending on how the queues are managed, an $O(n)$ search may be necessary to determine the highest priority process. In practice, it is often easier to have separate queues for each distinct priority. This is known as **multi-level queue scheduling**, and here there must be scheduling among the queues. One way is the one being shown on the screen right now: each queue has absolute priority over lower priority queues; scheduling simply dispatches the process in the highest priority non-empty queue, but to avoid starving lower priority queues we could schedule cues in another way: we can rotate the _ready_ queue in a round robin fashion, but dispatching a different amount of processes in a row depending on the priority of the queue: to time slice among the queues. **Multi-level queue scheduling** is commonly used to partition processes into several separate queues based on the process type (e.g. real-time processes, system processes, interactive processes, and batch processes, from high priority to low priority), making it better to schedule processes with multiple response time requirement. It even allow us to use a different scheduling algorithm for each queue: the foreground queue might be scheduled by a round robin algorithm for example, while the background queue is scheduled by a first come first serve algorithm, and that's basically what we expect from a general purpose CPU scheduler: to give priority to interactive processes, but not to the point of starving lower priority ones.

> The **multi-level feedback queue** algorithm was designed to adapt to these dynamic changes in behavior. Here's how it works. Every process starts in the highest priority queue; if a process completes its CPU burst within a single time quantum, it can return to that queue after finishing its I/O. If a process needs more CPU time than allowed by the time quantum, it's moved down to a lower priority queue. Lower priority queues have larger time quantums and processes that behave more like I/O bound tasks can be moved back up if they complete their CPU burst in less time than the quantum assigned to the higher priority queue. Over time, processes that have short CPU bursts will tend to stay in higher priority queues, in contrast CPU bound processes will gradually sink to lower priority queues. This is how the system adjusts process priority based on behavior automatically. Keep in mind that this is just one example of multi-level feedback queue scheduling: it comes in many variations, or as I like to say, all colors and flavors: not only can the scheduling algorithm applied to each queue differ, but also the criteria used to decide when a process should be promoted or demoted between queues. For instance, instead of relying solely on the most recently observed CPU burst to adjust a process's priority, the system could use a prediction based on historical data, which might help avoiding unnecessary queue switches in response to isolated or unusual bursts whether extremely short or unexpectedly long.

> **I/O schedulers**