# Suspenders (libsuspenders)

A high-performance, header-only C11 library providing stackful coroutines
(fibers) with sub-100ns context switching, a multi-worker QoS scheduler,
Go-style channels with `select`, libdispatch-style task queues, and
io_uring-backed async I/O ("hoses").

Design goals for v1.0: feature parity with [neco], performance in libuv's
class, ergonomics like libdispatch — in one header.

[neco]: https://github.com/tidwall/neco

## Measured performance

On a Jetson Orin (aarch64, Cortex-A78AE @ 1.73 GHz max), clang-22 `-O3`:

| Operation | Time | Cycles (@ max clock) | Target |
|-----------|------|----------------------|--------|
| Context switch (yield → scheduler → other coroutine) | 33 ns | ~57 | < 150 ✓ |
| Raw assembly switch (no scheduler) | 6.8 ns | ~12 | — |
| Channel rendezvous (per send or recv) | 69 ns | ~119 | < 80 ✗* |
| Queue async submit + serial dispatch | 80 ns | ~139 | — |
| Hose TCP round trip (loopback, 64 B) | ~16 µs | — | — |

\* The channel op misses the aspirational 80-cycle target by design: every
wake goes through the *decider handshake* that makes wakeups safe across
scheduler workers (the waker holds `waker_busy` until the waiter has fully
committed, so a waiter on another worker can never be resumed mid-decision).
That protocol is paid even in single-worker mode; the ~40-cycle premium buys
a TSan-clean multi-worker scheduler with no channel-side special cases.

Reproduce with `./build/suspenders-bench-cycles [yield|chan|queue|all] [N]`.
Cycle figures are derived from wall time (aarch64 generic timer) and the max
CPU clock; where `perf` is available, `perf stat -e cycles` on a single mode
gives exact counts.

## Requirements

- Linux x86_64 or aarch64 (first-class). The io_uring backend is used when the
  kernel is **≥ 5.19**; older kernels (e.g. 5.15 LTS) fall back to poll at
  init. Force poll anytime with `-DSUSPENDERS_FORCE_POLL` /
  `suspenders-tests-poll`.
- kqueue (macOS/BSD), poll (generic POSIX), and WSAPoll (Windows) backends.
  Windows is single-worker (WSAPoll + fibers); multi-worker is POSIX-only.
- GCC or Clang with C11 (`_GNU_SOURCE` is required on Linux); MSVC C11/C17 OK
  for the Windows path
- liburing 2.0+ on Linux (implementation TU only — consumers of the header
  don't need it)
- **Memento** v2.2.1+ (FetchContent by default; override with
  `-DSUSPENDERS_MEMENTO_SOURCE=/path/to/memento`)
- CMake 3.16+ for the test/benchmark/example tree
- C++17 for the optional `suspenders.hpp` facade

## Building

```bash
./build.sh                       # mkdir build, cmake, make
cd build && make -j$(nproc)      # incremental
./build/suspenders-tests         # unit tests (--filter=SUBSTR, --list, --timeout=SECS)
cd build && ctest                # tests via CTest
```

`SUSPENDERS_TEST_WORKERS=N` re-runs the whole suite on an N-worker scheduler.
Sanitizer builds: `cmake .. -DSUSPENDERS_SANITIZE=address,undefined` (or `thread`).

Targets: `suspenders-demo`, `suspenders-tests`, `suspenders-tests-poll`
(same suite forced onto the poll backend), `test-memento`,
`suspenders-benchmark`, `suspenders-switch`, `suspenders-bench-channels`,
`suspenders-bench-hose`, `suspenders-bench-cycles`, plus the C examples
(`tcp_echo`, `tcp_pingpong`, `udp_echo`, `unix_echo`, `channel_demo`,
`suspend_resume_demo`, `thread_pool`, `dispatch_demo`, `event_loop`) and C++
examples (`simple_cc`, `thread_pool_cc`, `dispatch_demo_cc`, `event_loop_cc`).

## Quick start

Header-only, two-step inclusion — define the implementation macros in exactly
one translation unit, Memento first:

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
    suspenders_init(0, 256);   /* 0 => 1 worker; 256 = io_uring SQ hint */

    int counter = 0;
    suspenders_go(worker, &counter);

    suspenders_run();          /* blocks until all coroutines finish */
    suspenders_shutdown();
    return counter == 2 ? 0 : 1;
}
```

Every other translation unit just includes the headers with no macros.

## API tour

All blocking operations return `SUSPENDERS_OK` or a `SUSPENDERS_*` error code
(`TIMEDOUT`, `CANCELED`, `CLOSED`, `EMPTY`, `FULL`, …) and set the
thread-local `suspenders_errno`; `suspenders_strerror(err)` names them.
The library never aborts.

### Runtime

```c
int  suspenders_init(unsigned num_workers, unsigned queue_hint);
void suspenders_run(void);       /* caller becomes worker 0 */
void suspenders_shutdown(void);
```

`suspenders_init(n, hint)` starts an `n`-worker scheduler (`0` means 1).
Each worker owns private per-QoS ready queues, an io_uring instance, and a
timer heap. Fresh spawns land in a global injector; a coroutine is pinned to
the worker that first runs it, and cross-worker wakes go through a lock-free
MPSC inbox plus an eventfd kick. `suspenders_run()` may be called again after
spawning more work; `suspenders_shutdown()` fully resets the runtime so
init/run/shutdown cycles can repeat.

### Coroutines

```c
suspenders_cr_t *suspenders_spawn(void (*fn)(void*), void *arg, suspenders_qos_t qos);
suspenders_cr_t *suspenders_go(void (*fn)(void*), void *arg);   /* NORMAL qos */
void suspenders_yield(void);
void suspenders_suspend(void);                 /* park until resumed */
void suspenders_resume(suspenders_cr_t *cr);   /* any thread */
void suspenders_boost(suspenders_cr_t *cr, suspenders_qos_t qos);
void suspenders_exit(void);
```

QoS levels: `SUSPENDERS_QOS_REALTIME`, `_HIGH`, `_NORMAL`, `_LOW` — four
priority ready queues per worker, with priority inheritance: blocking on a
mutex boosts the owner to the waiter's QoS.

Introspection: `suspenders_self()`, `suspenders_getid()`,
`suspenders_setname()/getname()`, `suspenders_stack_size()`.

### Cancellation, deadlines, cleanup

```c
int  suspenders_cancel(suspenders_cr_t *cr);   /* any thread */
bool suspenders_canceled(void);
int  suspenders_deadline(uint64_t deadline_ns); /* self-cancel timer */
void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void*), void *arg);
void suspenders_cleanup_pop(int execute);
```

Cancel wakes the target out of any blocking call (sleep, channel, mutex,
in-flight io_uring op via `ASYNC_CANCEL`), which returns
`SUSPENDERS_CANCELED`; the coroutine unwinds itself, running any remaining
cleanup handlers at exit. Every blocking API also has a `_dl`
(absolute-deadline, `suspenders_now_ns()` clock) variant that returns
`SUSPENDERS_TIMEDOUT`.

### Channels

```c
suspenders_chan_t *suspenders_chan_create(size_t elem_sz, size_t buf_sz); /* 0 = rendezvous */
int suspenders_chan_send(suspenders_chan_t *ch, void *val);
int suspenders_chan_recv(suspenders_chan_t *ch, void *out);
int suspenders_chan_try_send / try_recv / send_dl / recv_dl / close;
void suspenders_chan_destroy(suspenders_chan_t *ch);

suspenders_chan_op_t ops[] = {
    { .ch = a, .val = &x, .is_send = false },
    { .ch = b, .val = &y, .is_send = true  },
};
int idx = suspenders_select(ops, 2);            /* or suspenders_select_dl */
```

Go semantics: unbuffered channels rendezvous (zero-copy direct handoff),
buffered channels ring-buffer under a ticket lock, `close` lets receivers
drain the buffer before returning `SUSPENDERS_CLOSED`, send-on-closed is an
error, and `select` picks a ready case at random (returns the ops index, or
a negative error).

### Sync primitives

Coroutine-aware value types — blocking suspends the coroutine, never the
worker thread:

```c
suspenders_mutex_t      m;  suspenders_mutex_init/lock/lock_dl/trylock/unlock
suspenders_rwlock_t    rw;  ..._rdlock/wrlock (+_dl, try) /unlock
suspenders_cond_t       c;  ..._wait/wait_dl/signal/broadcast
suspenders_waitgroup_t wg;  ..._add/done/wait/wait_dl
```

### Task queues (libdispatch-style)

```c
suspenders_queue_t *q = suspenders_queue_create("net", SUSPENDERS_QOS_HIGH, 1);
suspenders_queue_async(q, fn, arg);            /* fire and forget    */
suspenders_queue_sync(q, fn, arg);             /* submit and wait    */
suspenders_queue_after(q, delay_ns, fn, arg);  /* delayed submit     */
suspenders_queue_barrier_async(q, fn, arg);    /* runs alone         */
suspenders_queue_destroy(q);                   /* drains, then frees */

suspenders_queue_t *g = suspenders_get_global_queue(SUSPENDERS_QOS_NORMAL);
```

`concurrency = 1` gives a serial queue with strict FIFO ordering; higher
values dispatch that many tasks in parallel. Queues are drained by daemon
coroutines, so an idle queue never keeps `suspenders_run()` alive — but
pending tasks do. `suspenders_pool_t` is a thin wrapper over a concurrent
queue.

### Async I/O (hoses)

Transport-agnostic descriptors over a registry (`tcp://`, `udp://`,
`unix://`, `tty://`); every operation suspends the calling coroutine and is
completed by the backend (real async `recv/send/accept/connect/readv/writev`
SQEs on io_uring; readiness + nonblocking syscall on kqueue/poll):

```c
suspenders_hose_t h;
suspenders_hose_init(&h, &buf);
suspenders_hose_dial(&h, "tcp://127.0.0.1:8080");     /* or _dl */
suspenders_hose_listen/accept/accept_dl
ssize_t n = suspenders_hose_read(&h, dst, len);        /* or _dl  */
ssize_t n = suspenders_hose_write(&h, src, len);       /* or _dl  */
suspenders_hose_readv/writev/shutdown/set_option/peername/sockname
suspenders_hose_close(&h);
```

I/O deadlines use io_uring `LINK_TIMEOUT`, so a timed-out op is truly
canceled in the kernel, not just abandoned. Custom transports plug in via
`suspenders_transport_register()`.

Timers: `suspenders_timer_create(ms, repeat, cb, arg)`,
`suspenders_sleep_ns(ns)`, `suspenders_sleep_dl(deadline)`,
`suspenders_now_ns()`.

## C++17 facade (`suspenders.hpp`)

RAII wrappers over the whole C API — header-only, zero-overhead, movable but
non-copyable handles:

```cpp
#define MEMENTO_IMPLEMENTATION      // one TU only, same as C
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

int main() {
    suspenders::Context ctx(4);            // init(4) … shutdown() via RAII
    suspenders::Channel<int> ch(8);        // buffered, type-safe

    auto t = suspenders::spawn([&] {       // lambdas with captures
        suspenders::Mutex m;
        suspenders::LockGuard g(m);
        ch.send(42);
    }, suspenders::QoS::High);

    suspenders::spawn([&] {
        if (auto v = ch.recv()) printf("%d\n", *v);   // std::optional<int>
    });

    ctx.run();
}
```

Provided: `Context`, `Task` (resume/boost/cancel/state), `Channel<T>`
(send/recv/try/close/deadlines, `recv_op()/send_op()` +
`suspenders::select({...})`), `Mutex`/`LockGuard`/`RWLock`/`Cond`/`WaitGroup`,
`CleanupGuard` (RAII cancellation cleanup), `Queue` (async/sync/after/
barrier_async with lambdas; `Queue::global(qos)`), `Hose`, and `Buffer`.

## Memory model

All coroutine control blocks, stacks, channels, queues, and tasks are
allocated through the [Memento](https://github.com/StraylightRunMOO/memento) allocator (FetchContent v2.2.1+):
one size-classed heap per thread, an arena per coroutine stack, and a
lock-free MPSC return path for cross-thread frees (a worker freeing another
worker's memory pushes it to the owner's heap, which flushes when idle).
There is no malloc/free in any hot path, and frees are exact-size — no
size headers on blocks.

## Notes for benchmarkers

- `SUSPENDERS_IOURING_SQPOLL` (compile-time flag) submits via a kernel
  polling thread instead of `io_uring_enter`. On big-core servers this can
  help; on the 6-core Jetson it measured **40% slower** (22.4 µs vs 16.0 µs
  TCP RTT) because the poller steals a core from the workers. Default off.
- `SUSPENDERS_PIN_WORKERS` pins worker threads to CPUs.
- `SUSPENDERS_VALGRIND` (define in the implementation TU) registers coroutine
  stacks with Valgrind so memcheck can follow context switches — without it,
  fiber stack traffic drowns the report in false positives. Use the poll
  backend under Valgrind (`suspenders-tests-poll`); Valgrind's io_uring
  emulation is unreliable. The suite runs 0-errors/0-leaks clean this way.
- The context-switch numbers above are with a warm cache and pinned max
  clock (`cpufreq` governor `performance`); expect ~2× on a throttled clock.

## Common pitfalls

1. `MEMENTO_IMPLEMENTATION` + `SUSPENDERS_IMPLEMENTATION` in exactly one TU,
   Memento included first.
2. Coroutines don't run until `suspenders_run()`.
3. Channel/blocking calls outside a coroutine return `SUSPENDERS_PERM` —
   only `suspenders_resume`/`suspenders_cancel`/`suspenders_spawn` (and
   `suspenders_go`) are thread-safe entry points from foreign threads.
   Foreign spawns land on the global injector and pin to the first worker
   that runs them. `suspenders_init` / `run` / `shutdown` stay on one
   thread; `shutdown` also tears down Memento (`memento_shutdown`), so
   drop any other Memento-backed jobs first.
4. Default stack is 1 MB per coroutine; avoid deep recursion.
5. Call `suspenders_shutdown()` from the thread that called
   `suspenders_init()`, after `run()` returns.

## License

See source files for license information.

## Contributing

- Match the existing style (C11, 4-space indent, K&R braces, `-Werror` clean
  under `-std=c11 -pedantic -Wall -Wextra`).
- The full gate: unit tests green at 1 and 4 workers, ASan+UBSan and TSan
  clean, poll-backend suite green, benchmarks unregressed.
- Assembly changes must keep the `_Static_assert`ed layout valid and include
  register usage comments.
