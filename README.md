# Suspenders

Suspenders is a header-only C11 library. A coroutine is a C function with
its own stack. When that function waits, the stack stays put and the OS
thread runs something else. One header contains the switch, the scheduler,
channels, locks, task queues, and byte-stream I/O.

Version 1.2.0. MIT, see `LICENSE`.

## What

| Piece | Role |
|---|---|
| Context switch | Save the callee-saved registers, swap stacks, continue. |
| Scheduler | One or more OS threads. Each owns its ready lists, its timers, and its I/O. |
| Channels | Two coroutines exchange a value, or a fixed ring holds it until the other side arrives. |
| Locks, conditions, wait groups | Waiting parks the coroutine. The OS thread keeps running. |
| Queues | Submit a function to run later, at a priority, alone or beside other tasks. |
| Hoses | `read` and `write` on `tcp://`, `udp://`, `quic://`, `unix://`, `tty://`. The call site looks blocking. The worker does not block. |
| C++17 (`suspenders.hpp`) | RAII handles and lambdas over the same calls. |

Errors are return codes (`SUSPENDERS_OK`, `TIMEDOUT`, `CANCELED`, `CLOSED`,
`EMPTY`, `FULL`, …) plus a thread-local `suspenders_errno`. The library does
not abort.

## Why it is built this way

Each choice below is the small construction that makes the next one cheap.
The rejected alternative is stated where it would change the cost.

### The continuation is the stack

**What.** Suspension returns from `suspenders_yield`, `suspenders_chan_recv`,
`suspenders_hose_read`, and the other blocking calls back into the same
frame, with locals intact.

**How.** The switch saves the ABI callee-saved registers and the stack
pointer, then loads the other coroutine's. On x86_64 that is `rip`, `rsp`,
`rbp`, `rbx`, `r12`–`r15`. On aarch64 it is `x19`–`x30`, `sp`, and `d8`–`d15`.
The offsets are `_Static_assert`ed. The stack is 16-byte aligned before
entry. Default stack size is 1 MB, taken from a Memento arena.

**Why.** A protocol's state is already the call stack. Keeping it means the
source stays a straight-line function.

**Why not a callback or a stackless coroutine.** Every wait would become an
object you allocate, name, and resume by hand. That object is a worse stack:
it cannot hold the compiler's locals, and it splits one function into a
switch on an explicit state.

**Why not a guard page under the stack.** Memento's guarded arenas put
`PROT_NONE` at the high end of a bump block. A fiber stack grows down, so
that page sits above the initial stack pointer, where overflow does not
go. Stacks therefore use an ordinary arena. Deep recursion in a coroutine
is your bug; the library will not catch it.

### A switch is a few stores

**What.** Measured on a Jetson Orin (aarch64, Cortex-A78AE held at 1.73 GHz,
clang 22 `-O3`, warm cache): a full yield through the scheduler and into
another coroutine is 33 ns, about 57 cycles. The assembly switch alone,
with no scheduler, is 6.8 ns, about 12 cycles.

**How.** File-scope assembly. No libc context call.

**Why.** The scheduler's job is to decide who runs next. If the switch
itself is a syscall, that decision is dominated by the kernel.

**Why not `swapcontext`.** It also saves a signal mask, it is not a handful
of stores, and its register layout is not ours to pin with a
`_Static_assert`.

Cycles in the table below come from wall time and that max clock. Where
`perf` is available, `perf stat -e cycles` on one mode of
`suspenders-bench-cycles` is the direct count. Expect about twice the time
on a throttled clock.

### Waiting parks a coroutine, not the worker

**What.** `suspenders_init(n, hint)` creates `n` worker threads. `0` means
one. `suspenders_run` turns the caller into worker 0 and returns when no
coroutine is left alive. You may spawn more work and call `run` again.
`suspenders_shutdown` tears the runtime down, including Memento, so the
cycle can repeat. Call it on the thread that called `init`, after `run`
has returned.

**How.** Each worker has four ready lists (`REALTIME`, `HIGH`, `NORMAL`,
`LOW`), a timer heap, and an I/O backend. A new coroutine enters a global
injector and is pinned to the worker that first runs it. A wake from
another worker is a push onto that owner's MPSC inbox and a write to its
eventfd. The waker holds `waker_busy` until the waiter has committed, so
the waiter cannot be resumed halfway through the decision.

**Why.** After the pin, the ready list is private. The yield path takes no
lock. The inbox exists only for the wake that originates elsewhere.

**Why not one OS thread per connection.** The stack, the kernel scheduler,
and the cache footprint are then paid per wait. The thing you have
thousands of is the wait, not the core.

**Why not one shared run queue.** Every yield would touch a lock or an
atomic that other cores bounce. That cost is larger than the switch we
just measured.

**Why not a separate fast path when there is only one worker.** The
handshake is paid anyway. On the same Jetson a channel rendezvous is 69 ns,
about 119 cycles, per send or recv. Roughly 40 cycles of that is the
handshake. One code path stays correct under the thread sanitizer. A
second path for the single-worker case would be faster and would be a
second set of bugs.

**Why four lists, not a heap of coroutines.** Priority is which list is
scanned first. A heap orders by a key on every pop. Four FIFOs do not.
`suspenders_boost` raises a coroutine's effective QoS; a mutex owner is
boosted to its waiter's QoS and drops back when that wait ends. Without
that, a low-priority holder blocks a high-priority waiter and the four
lists have lied.

Windows runs one worker (WSAPoll and fibers). More than one worker is
POSIX-only, because the cross-worker kick is an eventfd.

### I/O completion is a scheduler event

**What.** A hose is a connection. `suspenders_hose_read` returns the bytes
to the coroutine that called it. On Linux with kernel ≥ 5.19 the worker
submits the operation to io_uring and arms its wake there. Older kernels,
including 5.15, use `poll` instead. macOS and BSD use kqueue. Other POSIX
uses `poll`. Define `SUSPENDERS_FORCE_POLL`, or run `suspenders-tests-poll`,
to force `poll` even when io_uring is available.

**How.** Transports are a vtable registered at init: `tcp://`, `udp://`,
`quic://`, `unix://`, `tty://`. Add your own with
`suspenders_transport_register`. A deadline is absolute nanoseconds on
`suspenders_now_ns`. On io_uring the deadline is a linked timeout, so the
kernel cancels the operation. It is not left running while the coroutine
has already moved on. Cancel of a coroutine in an io_uring operation
submits `IORING_OP_ASYNC_CANCEL`.

**Why.** The coroutine should read the way a thread reads. The worker
should learn that the read finished in the same loop that runs timers and
inboxes. liburing 2.0+ is linked only into the one translation unit that
defines `SUSPENDERS_IMPLEMENTATION`. Callers of the header do not link it.

**Why not a private I/O thread.** Completions would still have to enter
this scheduler. That is the inbox again, plus a hop.

**Why not io_uring `SQPOLL` by default.** `SUSPENDERS_IOURING_SQPOLL`
replaces `io_uring_enter` with a kernel polling thread. On a large machine
that can win. On this 6-core Jetson the poller took a core from the
workers and TCP loopback (64 bytes) went from 16.0 µs to 22.4 µs. The flag
is off unless you set it. `SUSPENDERS_PIN_WORKERS` pins workers to CPUs
when you want that experiment controlled.

**Why not eBPF in front of the ring.** There is no stable way, on the
kernels this library runs, to put a BPF program inside an SQE and wake a
coroutine with the result. A reuseport program that hashes a QUIC
connection id onto one socket per worker would matter only when accept
fan-in is the bottleneck, and it needs `CAP_BPF`. The measurement and the
rejected designs are in `docs/ebpf-iouring.md`.

### A queue is a list of functions, drained by coroutines

**What.** `suspenders_queue_create(label, qos, concurrency)` returns a
queue. `async` submits and returns. `sync` submits and waits. `after`
submits when a delay has passed. `barrier_async` runs with the queue to
itself. `concurrency = 1` is a FIFO. A larger value runs that many tasks
at once. `suspenders_get_global_queue` returns a process-wide queue at a
QoS. `suspenders_pool_t` is a concurrent queue with a shorter name.

**How.** Daemon coroutines drain the queue. An idle queue does not keep
`suspenders_run` alive. A queue with pending tasks does.
`suspenders_queue_destroy` finishes what is already queued, then frees it.

**Why.** The work is "run this function", not "resume this stack". A queue
is the right object when you do not need the caller's locals preserved
across the wait. A coroutine is the right object when you do.

### Channels move values, not ownership of a lock

**What.** `suspenders_chan_create(elem_sz, buf_sz)`. `buf_sz == 0` is a
rendezvous: the sender's buffer and the receiver's buffer are the same
transfer, and neither call returns until the other has arrived. A positive
`buf_sz` is a ring of that many slots, under a ticket lock so waiters
proceed in arrival order. `close` lets receivers take what is still
buffered, then returns `SUSPENDERS_CLOSED`. A send on a closed channel is
an error. `select` chooses uniformly among the cases that can proceed and
returns that case's index, or a negative error.

**Why.** The rendezvous copies once, from sender to receiver. A lock around
a shared cell would copy twice and still need the same sleep and wake.

**Why not an unbounded queue as the channel.** A channel is backpressure.
If the sender never waits, the buffer is a leak with an API.

### Memory returns to the thread that allocated it, at the size requested

**What.** Control blocks, stacks, channels, and queue nodes come from
[Memento](https://github.com/StraylightRunMOO/memento) 3.0.0. CMake fetches
it. `-DSUSPENDERS_MEMENTO_SOURCE=/path` uses a local tree.

**How.** Each thread has its own size-class heap. The hot path is the
inline thread cache. Free takes the pointer and the size you passed to
alloc; there is no header on the block to recover it. A free from the
wrong thread pushes the block onto the owner's MPSC stack. The owner
reclaims it when idle. `shutdown` calls `memento_shutdown`. Drop any other
Memento users first.

**Why.** The switch and the channel are tens of nanoseconds. A locked
allocator, or a header stored beside every block, is in the same budget
and does not belong there.

**Why not the system allocator on the hot path.** It serializes threads and
it remembers the size because the caller is not required to. This caller
is required to. That is the contract that removes the header.

### QUIC is a byte stream with a handshake

**What.** `quic://host:port` dials. `quic://0.0.0.0:port` listens. One
connection, one bidirectional stream, `read` and `write` as on TCP. QUIC
v1, TLS 1.3, X25519, AES-128-GCM, ALPN `susp`. Compiled only when OpenSSL 3
is present. Without it the scheme is not registered.

**How.** The record layer rides the same UDP suspend path as any other
hose. CertificateVerify is checked against the certificate's key. Nothing
checks the name, and there is no trust anchor.

**Why.** A hose is "bytes in, bytes out, the coroutine waits". The
handshake exists so those bytes are not plaintext on the wire.

**Why not a general QUIC endpoint.** Migration, 0-RTT, and many streams are
a different product. This transport is the one stream a hose already is.
You bring a trust decision yourself; the library will not invent a PKI.

## What it costs

Same machine and build as above. `./build/suspenders-bench-cycles [yield|chan|queue|all] [N]`.

| Operation | Time | Cycles at 1.73 GHz |
|---|---|---|
| Yield, schedule, resume the other coroutine | 33 ns | ~57 |
| Assembly switch, no scheduler | 6.8 ns | ~12 |
| Channel rendezvous, per send or per recv | 69 ns | ~119 |
| Queue submit and serial dispatch | 80 ns | ~139 |
| Hose TCP, loopback, 64 bytes, round trip | ~16 µs | — |

The channel row is the switch plus the cross-worker handshake, including
when only one worker exists. That is the cost recorded under "Why not a
separate fast path" above.

## How to build

Linux x86_64 and aarch64 are the tested targets. You need GCC or Clang in
C11 (`_GNU_SOURCE` on Linux), CMake 3.16 or later, and, on Linux, liburing
2.0 or later for the implementation unit. MSVC C11/C17 builds the Windows
path. The C++ facade needs C++17.

```bash
./build.sh
cd build && make -j$(nproc)
./build/suspenders-tests                  # --filter=SUBSTR --list --timeout=SECS
SUSPENDERS_TEST_WORKERS=4 ./build/suspenders-tests
cd build && ctest
```

Sanitizers: `cmake .. -DSUSPENDERS_SANITIZE=address,undefined` or `thread`.

Built programs: `suspenders-demo`, `suspenders-tests`,
`suspenders-tests-poll`, `test-memento`, `suspenders-benchmark`,
`suspenders-switch`, `suspenders-bench-channels`, `suspenders-bench-hose`,
`suspenders-bench-cycles`. C examples: `tcp_echo`, `tcp_pingpong`,
`udp_echo`, `unix_echo`, `channel_demo`, `suspend_resume_demo`,
`thread_pool`, `dispatch_demo`, `event_loop`. C++ examples: `simple_cc`,
`thread_pool_cc`, `dispatch_demo_cc`, `event_loop_cc`.

Under Valgrind, define `SUSPENDERS_VALGRIND` in the implementation unit so
coroutine stacks are registered, and run `suspenders-tests-poll`. Valgrind
does not emulate io_uring reliably. With the poll backend the suite is
clean of errors and leaks. Without the registration, every stack switch
looks like a wild access.

## How to call it

Define both implementation macros in one translation unit. Include Memento
first. Every other unit includes the headers and defines nothing.

```c
#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

void worker(void *arg) {
    int *val = arg;
    (*val)++;
    suspenders_yield();
    (*val)++;
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);   /* one worker; 256 is the io_uring queue hint */

    int counter = 0;
    suspenders_go(worker, &counter);

    suspenders_run();
    suspenders_shutdown();
    return counter == 2 ? 0 : 1;
}
```

Nothing runs until `suspenders_run`. A blocking call outside a coroutine
returns `SUSPENDERS_PERM`. From a foreign thread the legal entries are
`suspenders_spawn`, `suspenders_go`, `suspenders_resume`, and
`suspenders_cancel`. A foreign spawn sits in the injector until some worker
picks it up, and then it is pinned there. `init`, `run`, and `shutdown`
stay on one thread.

```c
int  suspenders_init(unsigned num_workers, unsigned queue_hint);
void suspenders_run(void);
void suspenders_shutdown(void);

suspenders_cr_t *suspenders_spawn(void (*fn)(void *), void *arg, suspenders_qos_t qos);
suspenders_cr_t *suspenders_go(void (*fn)(void *), void *arg);  /* NORMAL */
void suspenders_yield(void);
void suspenders_suspend(void);               /* parked until resume */
void suspenders_resume(suspenders_cr_t *cr);
void suspenders_boost(suspenders_cr_t *cr, suspenders_qos_t qos);
void suspenders_exit(void);

int  suspenders_cancel(suspenders_cr_t *cr); /* blocking call returns CANCELED */
bool suspenders_canceled(void);
int  suspenders_deadline(uint64_t deadline_ns);
void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void *), void *arg);
void suspenders_cleanup_pop(int execute);

suspenders_chan_t *suspenders_chan_create(size_t elem_sz, size_t buf_sz);
int suspenders_chan_send(suspenders_chan_t *ch, void *val);
int suspenders_chan_recv(suspenders_chan_t *ch, void *out);
/* also try_send, try_recv, send_dl, recv_dl, close */
int suspenders_select(suspenders_chan_op_t *ops, int n);  /* or select_dl */
```

`suspenders_self`, `suspenders_getid`, `suspenders_setname`,
`suspenders_getname`, and `suspenders_stack_size` inspect the running
coroutine. Locks, rwlocks, conditions, and wait groups are values you
initialize in place; each blocking operation has a `_dl` form that takes
an absolute deadline and returns `SUSPENDERS_TIMEDOUT`.
`suspenders_sleep_ns` and `suspenders_timer_create` use the same clock.

```c
suspenders_queue_t *q = suspenders_queue_create("net", SUSPENDERS_QOS_HIGH, 1);
suspenders_queue_async(q, fn, arg);
suspenders_queue_sync(q, fn, arg);
suspenders_queue_after(q, delay_ns, fn, arg);
suspenders_queue_barrier_async(q, fn, arg);
suspenders_queue_destroy(q);

suspenders_hose_t h;
suspenders_hose_init(&h, &buf);
suspenders_hose_dial(&h, "tcp://127.0.0.1:8080");   /* listen / accept likewise */
ssize_t n = suspenders_hose_read(&h, dst, len);     /* write, readv, writev */
suspenders_hose_close(&h);
```

The C++ header is the same inclusion rule.

```cpp
#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

int main() {
    suspenders::Context ctx(4);          // init … shutdown
    suspenders::Channel<int> ch(8);

    suspenders::spawn([&] {
        suspenders::Mutex m;
        suspenders::LockGuard g(m);
        ch.send(42);
    }, suspenders::QoS::High);

    suspenders::spawn([&] {
        if (auto v = ch.recv()) printf("%d\n", *v);  // std::optional
    });

    ctx.run();
}
```

The facade covers `Context`, `Task`, `Channel<T>` (including `select`),
`Mutex`, `LockGuard`, `RWLock`, `Cond`, `WaitGroup`, `CleanupGuard`,
`Queue`, `Hose`, and `Buffer`. Handles move and do not copy.

## Working on the code

C11, four-space indent, K&R braces. The bar is `-std=c11 -pedantic -Wall
-Wextra -Werror`. Tests pass with one worker and with four, under
ASan+UBSan and under TSan, and again on the poll backend. Benchmarks do
not regress. An assembly change keeps the asserted offsets true and says,
in a comment, which register each slot is.
