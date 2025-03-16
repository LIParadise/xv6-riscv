https://gist.github.com/cb372/5f6bf16ca0682541260ae52fc11ea3bb

(This is a translation of the original [article in Japanese](https://moraprogramming.hateblo.jp/entry/2019/03/17/165802) by [moratorium08](https://twitter.com/moratorium08).)

(UPDATE (22/3/2019): Added some corrections provided by the original author.)

Writing your own OS to run on a handmade CPU is a pretty ambitious project, but
I've managed to get it working pretty well so I'm going to write some notes
about how I did it.

For those of you who want to skip straight to the code, it's on GitHub:

https://github.com/moratorium08/osmium/

I only wrote the actual OS. [ATP](https://twitter.com/ATPYM4) wrote the core and
[dai](https://twitter.com/__dAi00) implemented the multiply instruction.

https://github.com/hitohira/yokyo

# Summary

I implemented an OS from scratch in Rust that runs on RISC-V(rv32im). When
writing an OS, you need to take care of things like paging, kernel/usermode
privilege separation, interrupts, etc., and I implemented these based on the
RISC-V Privilege spec document. There might be a few things in the RISC-V spec
that I didn't implement, but there's nothing in the implementation that is not
in the spec (in other words there's no code based on spec I came up with
myself). For paging, I implemented the Sv32 scheme.

The major parts of the OS that I implemented are:

* bootloader
* memory mapped UART I/O
* RISC-V Privileged Architecture, Sv32 paging
* Supervisor Mode/User Mode privilege separation
* ELF file parsing and process management
* preemptive multitasking via clock interrupt trap handling
* Copy on Write fork system calls: fork, execve, exit, mmap, etc.
* inter-process communication
* a simple filesystem

I also made a shell as an example of a usermode application that makes use of
these kernel features. It's quite a simple program, but it manages its child 
processes in almost the same way as Linux shells can do (fork, execve, wait, 
exit). In other words, it's proof that the OS has sufficient features to 
support something as powerful as a shell.

# Contents

* Summary
* What's RISC-V?
* Why Rust?
* About the implementation
    * Building Rust programs to target the ISA
    * About the Privilege Architecture
    * UART I/O
    * Paging
    * Parsing ELF files
    * Copy on Write fork
    * Trap handling and preemptive multitasking
    * Inter-process communication
    * Filesystem (simple)
    * System calls
    * Shell
* Try it in an emulator
* Final thoughts
* References
* Appendix: about the CPU assignment

# What's RISC-V?

You should probably just google it. It's an ISA (instruction set architecture)
that's gaining a lot of interest recently. Fundamentally it's similar to MIPS
but it's a clean, modern RISC architecture without all the legacy crud of MIPS.

Writing a CISC core such as x86 is really tough, so if you're going to write an
OS for fun and run it on an FPGA, I think RISC-V is a natural choice. Compiler
tooling with support for this architecture, e.g. gcc and llvm, already exists,
and there is also an emulator called Spike (although I didn't use it myself).

# Why Rust?

A lot of real-world kernel security vulnerabilities are caused by things like
buffer overflows and type errors. Using Rust means more powerful static
analysis, allowing you to build a more secure OS. Of course, in the process of
writing an OS you need to use a few unsafe features, so you might not achieve the
same level of safety as you would when writing a normal Rust program. But it's
easy to distinguish between the safe and unsafe parts of the code, and by
minimising the use of `unsafe` you can focus your efforts on the unstable parts
of the system.

Furthermore, even if you can't use the features of std, you can still make use
of Iterator, Result, Option, pattern matching, traits, etc. to make your code
beautiful.

Anyway, although I enumerated some reasons above, the reason why I adopted Rust
in this project was that I just felt like writing it in Rust.

# About the implementation

Some notes on things I struggled to implement, important implementation points,
etc.

## Building Rust programs to target the ISA

Rust officially supports RISC-V as a target since last year. However, RISC-V has
a few different bases (rv32i, rv64i, etc.) and defines a number of extensions.
So when somebody says "RISC-V" they are actually talking about one of many
variants.

The core I used to run the OS has a 32bit architecture. Currently there are 2
officially supported 32bit ISAs: rv32imac and rv32imc. The letters after the
number stand for spec extensions:  m stands for Integer Multiplication and
Division, a for Atomic Instructions and c for Compressed Instructions.

For this project, we decided making a multicore CPU (in RISC-V terminology, a
CPU with multiple harts) was a bit too ambitious, so our core has only one hart
and we went for the architecture without atomic instructions. We also decided
not to bother with compressed instructions. "Compressed instructions" is a 16-bit
subset of the instruction set that only supports frequently used
instructions, frequently used constants (e.g. 0) and frequently used registers
(e.g. the zero register). According to the RISC-V spec, this subset of
instructions makes up 50-60% of most programs and use of compressed instructions
can reduce code size by 25-30%.

In hindsight, all you need to do to support compressed instructions is fiddle
with the decoder and the instruction fetching code a bit, so maybe it wouldn't
be that hard. But, as we'll see later, since it was required to implement the 
MMU and CSR  (Control and Status Register) in the CPU, we wanted to keep things 
as simple as possible and focus on implementing them.

As I mentioned above, rv32im does not exist as an official target, so I had to
set up my own build environment for cross-compilation. I did this with help from
[Writing an OS in Rust](https://os.phil-opp.com/), the Rust official docs, [The
Embedded Rust Book](https://rust-embedded.github.io/book/) and [RustでRISC-V
OS自作！はじめの一歩](https://qiita.com/tomoyuki-nakabayashi/items/76f912adb6b7da6030c7).
For more details, take a look at [my previous
post](https://moraprogramming.hateblo.jp/entry/2019/01/25/131314) or the GitHub
repo.

The result is that I'm able to make a build that doesn't use the standard
library.

## About the Privilege Architecture

The implementation follows the RISC-V Privilege Architecture spec. RISC-V
currently defines three privilege levels: Machine, Supervisor and User.
According to the spec, no matter what your hardware you have to implement
support for a Machine mode.

For this project I wanted to implement paging, so Supervisor mode was also a
must, meaning I have to implement the whole Machine-Supervisor-User set.
However, to keep things simple, I decided to make it so the machine switches to
Supervisor mode immediately after startup. If we assume that interrupts alway
shave the delegate bit set, the Supervisor has the necessary permissions to
perform all the processing.

I used nearly all of the supervisor's CSR. ATP (the person who wrote the CPU) 
took care of implementing the corresponding instructions (csrrw, csrrc, csrrs, 
etc.) and system-level instructions (sret, ecall) in the CPU.

## UART I/O

I decided to use UART for I/O, so first of all I had to write I/O processing
that uses UART. I made the UART memory-mapped, so you just need to read or write
the correct memory location one byte at a time, but I wanted to provide slightly
richer I/O support. So I implemented `print!` and `println!` macros so you can
do formatted output just like in regular Rust.

This is pretty easy to do in Rust if you implement the `core::fmt::Write`
trait. (`0x80000000` is the UART input address I chose.)


```rust
use core::fmt::Write;

const UART_RX: *const u8 = 0x80000000 as *const u8;
const UART_TX: *mut u8 = 0x80000004 as *mut u8;

struct UART;

fn write_byte(byte: u8) {
    unsafe {
        *UART_TX = byte;
    }
}

impl Write for UART {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for c in s.bytes() {
            write_byte(c);
        }
        Ok(())
    }
}

pub fn print(arg: ::core::fmt::Arguments) {
    UART.write_fmt(arg).expect("failed to send by UART");
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => ($crate::uart::print(format_args!($($arg)*)));
}
#[macro_export]
macro_rules! println {
    () => (print!("\n"));
    ($arg:expr) => (print!(concat!($arg, "\n")));
    ($fmt:expr, $($arg:tt)*) => (print!(concat!($fmt, "\n"), $($arg)*));
}
```

## Paging

RISC-V provides a spec for paging. For this project the architecture is 32bit,
so the only choice of paging scheme is Sv32. Putting it very roughly and
ignoring superpages, it's a 2-step jump: you use the first 10 bits of the
virtual address and the next 10 bits to do a double table lookup, then add the
resulting table entry to the bottom 12 bits of the virtual address to get the
physical address. For more details, refer to the spec.

When it came to implement this, I struggled a bit with accessing the
intermediate page table.

As mentioned above, conversion from a virtual address to a physical address
involves a 2-step table lookup (again, we're ignoring superpages). Now, let T0 
and T1 be the first part of the tables and the second part of the tables 
respectively. When you try to implement the table lookup you
hit a problem, which is that after you've turned on the MMU it's difficult to
access T1. For the T0 table, I was able to pre-assign one entry per process at
startup time so I knew the table's virtual address. But the T1 tables are
assigned dynamically (at startup time not all the memory is mapped into the
kernel's page space. This means that even if you know a table's physical
address, the virtual address you need to access it hasn't been defined.

One solution to this, for example with x86, is to assign one entry in the T0
table to point to itself, meaning you can avoid the need for 2 jumps (I think
this is known as recursive paging).  So x86 paging makes it possible to access 
intermediate tables without having to sacrifice precious memory space.

In Sv32 however, the spec dictates that every page's table entry must be either
a leaf, i.e. the physical address of the head of a page, or a pointer to the
next page table. And if you try to do more than 2 jumps, you get a page fault.
(I'm a bit unsure about this stuff, so please correct me if I've misunderstood.)

Because of this, it's not possible to do recursive paging as described above.
This is because the page table entry pointing to the page table you want to
access is not a leaf. To work around this problem I made another table T0', which
is a duplicate of T0. One entry in T0 maps to T0', but all the other entries in
the two tables are identical.

(see original post for diagram)

Since this design needs one more page for paging, it wastes PAGESIZE memory 
space per process compared with recursive paging, but it achieves the same result.
It can't really be described as beautiful though. If there's anyone out there who
understands this stuff properly, I'd appreciate some advice.

## Parsing ELF files

Rust has a zero-copy ELF parsing library called goblin, but for reasons to do
with its use of synchronous instructions I wasn't able to use it. So I wrote my
own simple parser.

Assuming you can fit the whole ELF in memory, it has a structure that's easy to
parse. If you define a struct like in the code below, you can simply decode the
bytes into the struct (although this code might be missing some validation).

```rust
#[derive(Debug)]
#[repr(C)]
pub struct ElfHeader {
    pub magic: u32,
    pub elf: [u8; 12],
    pub etype: u16,
    pub machine: u16,
    pub version: u32,
    pub entry: u32,
    pub phoff: u32,
    pub shoff: u32,
    pub flags: u32,
    pub size: u16,
    pub phentsize: u16,
    pub phnum: u16,
    pub shentsize: u16,
    pub shnum: u16,
    pub shstrndx: u16,
}

/*...*/

    pub fn new(bytes: *const [u8]) -> Result<Elf<'a>, ElfError> {
        let bytes = unsafe { &*bytes };
        let elf = unsafe {
            let data: *const ElfHeader = bytes.as_ptr() as *const ElfHeader;
            &*(data)
        };
        if elf.magic != ELF_MAGIC {
            return Err(ElfError::InvalidMagic);
        }
        Ok(Elf { bytes, elf })
```

For more details take a look at the code. Maybe this stuff is common knowledge
to experts in the field.

## Copy on Write fork

I implemented `fork` using Copy On Write.

In order to start a new process, you need a command that takes an exact clone of
the current process's memory and uses it to create a new process, i.e. `fork`.

You need a way to copy all of a given process's memory. Of course it's possible
to make a copy of all of the process's mapped memory, but this would involve a
huge amount of memory access and thus would be extremely expensive.

We can improve the situation by taking a copy of the page table, setting a Copy
on Write flag on the writable pages, then only copying the pages when one of the
processes (either the original process or the forked one) actually makes a write
to that page. This is known as Copy on Write and can be thought of as lazily
evaluating the copying of pages. Often after a `fork` has taken place, a call to
`execve` will replace the address space, so not bothering to copy a load of
pages unnecessarily is a good optimisation.

(see original post for diagram)

In order to implement Copy on Write I used 1 bit of the RSW section in page
table entries.

(see original post for diagram)

## Trap handling and preemptive multitasking

As mentioned above, the system is set up so that the supervisor is delegated to handle all
interrupts, so the CSR (Control and Status Registers) are all managed by the
supervisor.

In order to handle a trap, you have to first set the address of the instruction
you want to jump to when a trap happens. You do that by writing the address to
the CSR called `STVEC`. (By the way, I'm using Direct mode, not Vectored mode.)

Having set that, when a trap occurs the program counter will be set to that
address and the processing of the trapframe will begin. When that happens, the
stack is replaced with a dedicated trap-handling stack and the usermode
registers are saved. The CSR called `sscratch` is used to point to the location
where the usermode registers are temporarily stored. Also the `sepc` holds the
program counter at the time the trap occurred, so that is also saved.

https://github.com/moratorium08/osmium/blob/master/kernel/src/trap.rs#L394-L437

After that, the supervisor looks at the `scause` and `stval` registers and
handles the trap appropriately.

To return to usermode after trap handling is complete, you basically do all of
the above in reverse, set all the registers appropriately, put the saved program
counter in `spec` and call `sret`.

https://github.com/moratorium08/osmium/blob/master/kernel/src/trap.rs#L461-L544

This trap-handling mechanism can also be used for clock interrupts. RISC-V uses
2 memory-mapped registers `mtime` and `mtimecmp` to control timer interrupts.
The process for handling clock interrupts is pretty simple: put the currently
running process into Runnable state, and start running one of the currently
Runnable processes. I didn't have time to implement any funky scheduling
algorithms, so I used a pretty simple rule. I sort the processes in order of how
recently the scheduler has checked if they are Runnable, then start checking
them from the oldest one until I find one that is in Runnable state.

https://github.com/moratorium08/osmium/blob/master/kernel/src/trap.rs#L340-L345

https://github.com/moratorium08/osmium/blob/master/kernel/src/proc.rs#L260-L279

## Inter-process communication

There are 2 standard ways to achieve IPC: message-passing using a queue, and
memory sharing, i.e. allowing multiple processes to share the same area of
physical memory. For this project I implemented both.

When using message passing, you have to make a system call every time you
want to send a message. This means that, compared with memory sharing, it's not
very performant when passing large amounts of data (but, according to [Operating System
Concepts](https://www.amazon.co.jp/Operating-System-Concepts-Abraham-Silberschatz/dp/1118063333) this is not always true when you have a large number of cores because of the effect of cache line bouncing). My implementation is heavily
influenced by `xv6`: there is one mailbox per process, and each mailbox can be
read only by that process but written by many processes. Writing to and reading
from mailboxes is asynchronous, so if you want to know whether the message has
actually been received or not, you need the recipient to send you another
message.

If we assume people will use this message-passing mechanism for sending
lightweight synchronisation messages between processes, the use case for memory
sharing is inter-process sharing of large amounts of data. In order to use
memory sharing you only need to make a single system call to create the shared
memory zone, and after that you just do plain memory reads and writes, so there
are no system calls involved and it performs well.

To showcase IPC I made a toy example,
[catch_ball.rs](https://github.com/moratorium08/osmium/blob/master/misc/src/bin/catch_ball.rs).
This programs first does some message passing, then sets up a shared memory zone
and manipulates data inside it.

## Filesystem

Unfortunately I wasn't able to implement a proper filesystem in time for our
final presentation of the assignment (19th Feb), so I just hacked something together to provide
files to use as input for calls to `execve` etc. I hardcoded some static files
and just looked up the file content by name. This sucked, so after the final
presentation I started work on a real filesystem.

Microkernels are kernels that implement things like memory management, process
management and IPC in kernel mode but provide things like filesystems and the
networking stack in userland. This OS follows that design.

To briefly explain my homegrown filesystem spec, just like most mainstream
filesystems it manages "blocks". The very first block is a "superblock"
containing information about the filesystem, and the next is a management
block containing information about which blocks are in use. After that come the
blocks containing actual file data.

The filesystem supports regular files and directories, and directories can
recursively contain both regular files and other directories.

Both regular files and directories have their own metadata sections, and a
double-jump pointer to the actual data. For example you can access the data in a regular file by traversing the 2 tables as shown in the diagram below.

(see original post for diagram)

Directories are structured in the same way and hold pointers to their child
files.

I wrote [the disk
formatter](https://github.com/moratorium08/osmium/blob/master/fs/scripts/disk_formatter.py)
in Python. To be honest I was getting tired so I didn't bother adding into the
OS properly. Writing all this stuff on your own is pretty tough, so if anyone
wants to help out that would be great. I guess I'll get around to implementing
it properly soon.

## System calls

In RISC-V system calls are known as environment calls and you can use the
`ecall` instruction to raise an environment call exception. The arguments to the
call are passed via `a0`, `a1`, ... and the result is returned in `a0`.

## Shell

As an example of a user program that makes use of the features described above,
I built a shell. I didn't have much time so it only includes basic features, but
it's able to perform the basic `fork -> execve -> exit` flow.

```rust
pub extern "C" fn _start() -> ! {
    let mut buf = [0u8; 256];
    println!("** Osh **");
    loop {
        print!("$ ");
        let (len, b) = uart::buffered_readline(&mut buf);
        if len == 0 {
            continue;
        }
        if !b {
            println!("Sorry. too long. Please enter shorter command");
            continue;
        }
        let cmd = match str::from_utf8(&buf) {
            Ok(s) => s,
            Err(_) => {
                println!("Failed to parse your input. Try again.");
                continue;
            }
        };
        if buf[0] == b'e' && buf[1] == b'x' && buf[2] == b'i' && buf[3] == b't'  && len == 4{
            syscall::sys_exit(0);
        }
        match syscall::sys_fork() {
            syscall::ForkResult::Parent(id) => {
                loop {
                    match syscall::sys_check_process_status(id) {
                        syscall::ProcessStatus::Running | syscall::ProcessStatus::Runnable => {
                            syscall::sys_yield();
                        },
                        _ => {
                            break;
                        }
                    }
                }
            },
            syscall::ForkResult::Fail => {
                println!("fork failed");
            },
            syscall::ForkResult::Child => {
                syscall::sys_execve(cmd, len as u32, &[], &[]);
            }
        }
    }
}
```

# Try it in an emulator

I went into a lot of detail about the implementation, but if you want to
see it running in an emulator you can use the docker image.

```
docker run -it moratorium08/osmium
```

It's an image I made from the build environment so it's pretty massive. It might
take a while to download.

The graphics are not pretty, but at least you can see that it's actually working.

# Final thoughts

That's pretty much all I have to say. I didn't implement any networking stuff
but I probably should, as it would unlock a lot more interesting userland
applications. It would also be fun to implement some graphics support using VGA,
maybe even a windowing system. Also the whole implementation is pretty rough
around the edges (memory is not cleaned up properly, you can't clean up
processes, the privileges stuff is sometimes wrong, etc.) so there's plenty of tidying up
and polishing I could do.

My background regarding operating systems is pretty much:

* [I read the source code of Unix
    V6](https://moraprogramming.hateblo.jp/entry/2017/12/02/032133)
* I did all the exercises for the [MIT xv6
    course](https://pdos.csail.mit.edu/6.828/2018/)
* I read [Writing an OS in Rust](https://os.phil-opp.com/) (and did exactly what
    it told me to)
* I did [liva](https://twitter.com/liva_jy)'s systems programming assignment
    (and took the operating systems course)

Before this I'd never implemented my own OS, so it was a good experience. The OS
I made is pretty basic and doesn't break any new ground, so I'd like to add some
more interesting features to it in the future.

Also ATP writing an amazing core was a lifesaver, and hacking on this in a small
group with a short deadline was a lot of fun.

# References

* [xv6(6.828 Operating System Engineering)](https://pdos.csail.mit.edu/6.828/2018/)
* [Writing an OS in Rust](https://os.phil-opp.com/)
* [Redox](https://www.redox-os.org/)

# Appendix: about the CPU assignment

I haven't explained it properly so far, but this was an optional extra part of a
CPU assignment for my computer science course at [university](https://www.is.s.u-tokyo.ac.jp/english/), so it was just for
fun. Previous students to try this extra assignment have done a lot of different
things, including porting xv6 and building an OS from scratch. It's quite
exciting when you see your handmade CPU work for the first time, and I think
quite a lot of people feel the urge to build more on top of that foundation.

In the main part of the CPU assignment, some people focussed heavily on
performance, and others were experts at building cores (ATP, who [got raytracing
working on 3 different architectures](https://github.com/cpu2018-1) and wrote
[the core](https://github.com/hitohira/yokyo) for my OS to use, is an expert on
cores, while uenoku, who [wrote min-caml from scratch in 2
languages](https://github.com/uenoku/mincaml-rs) and [wrote an LLVM
backend](http://uenoku.hatenablog.com/entry/2018/12/25/044244) for his original ISA is an expert on
compilers), but even though I can't claim that level of expertise I thought it
was worth jotting down my thoughts.

For the CPU assignment we had to build a core and an FPU in Verilog, write a
compiler in OCaml, and use them to run a raytracing program on an FPGA,
competing on execution speed.

Once you've got it working, the idea is to optimise it by designing cores with
higher clock speeds, reducing the number of dynamically executed instructions,
increasing the instruction level parallelism in the core, etc.

I was in charge of the core for this main part of the assignment, and I also
helped to optimise the compiler. The architecture was a simple pipeline running
at 210MHz (without instruction parallelism, but with register forwarding and
out-of-order completion of load instructions).

After doing some optimisation work to reduce the instruction count, we ended up
with around 1.88 billion instructions, which took 20.7 seconds to execute. The
first time they did this exercise back in 2006 the record was set at 18.8
seconds, and the record as of 2016 was 4 seconds, so ours was not a spectacular
result. This was probably because we didn't have instruction-level parallelism,
and we could have optimised our multi-cycle floating point operations more.

The point is, as we can plainly see from looking at modern commercial CPUs, that
parallelism is the key to getting good performance.

