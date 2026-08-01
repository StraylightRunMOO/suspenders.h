# Suspenders C API Reference

*libsuspenders 1.1 — header-only coroutines, channels, and async I/O for C11.*

This is the reference for the C API. If you write C++17, see
[API_CPP.md](API_CPP.md) — it wraps everything here in RAII types and lambdas,
and you may never need to touch a `void*` again.

## Getting started

Suspenders is a single-header library. In exactly one translation unit, define
`SUSPENDERS_IMPLEMENTATION` before including it. Memento (the allocator) needs
the same treatment and must come first:

```c
#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
```

Every other file just includes `suspenders.h` — no defines, no link flags
beyond `-lpthread` and `pkg-config --libs liburing` on Linux.

Here is the smallest useful program. It spawns a coroutine, runs it, and exits:

```c
#include <stdio.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

static void hello(void *arg) {
    (void)arg;
    printf("Hello from a coroutine!\n");
}

int main(void) {
    suspenders_init(0, 0);
    suspenders_go(hello, NULL);
    suspenders_run();
    suspenders_shutdown();
    return 0;
}
```

That's it. `suspenders_init` boots the scheduler. `suspenders_go` spawns a
coroutine at normal priority. `suspenders_run` spins the event loop until
every coroutine has finished. `suspenders_shutdown` tears it all down.

Everything interesting happens between `init` and `shutdown`.

## Programming notes

### Error handling

Most functions return an `int` status code. Zero is good. Negative is not.
The codes are:

| Constant | Value | Meaning |
|---|---|---|
| `SUSPENDERS_OK` | 0 | Success |
| `SUSPENDERS_ERROR` | -1 | System error — check `errno` |
| `SUSPENDERS_INVAL` | -2 | Bad argument |
| `SUSPENDERS_NOMEM` | -3 | Out of memory |
| `SUSPENDERS_TIMEDOUT` | -4 | Deadline expired |
| `SUSPENDERS_CANCELED` | -5 | Coroutine was canceled |
| `SUSPENDERS_CLOSED` | -6 | Channel is closed |
| `SUSPENDERS_EMPTY` | -7 | Channel empty (try_recv) |
| `SUSPENDERS_FULL` | -8 | Channel full (try_send) |
| `SUSPENDERS_BUSY` | -9 | Lock held (trylock) |
| `SUSPENDERS_NOTINIT` | -10 | Runtime not initialized |
| `SUSPENDERS_NOTFOUND` | -11 | No such transport |
| `SUSPENDERS_PERM` | -12 | Not allowed in this context |

The thread-local `suspenders_errno` holds the code from the most recent
failing call. `suspenders_strerror()` turns it into a human-readable string.

### Deadlines

Functions with a `_dl` suffix take an absolute deadline in nanoseconds, as
returned by `suspenders_now_ns()`. Pass 0 to wait forever:

```c
uint64_t deadline = suspenders_now_ns() + 500 * 1000000ULL; /* 500 ms */
int rc = suspenders_chan_recv_dl(ch, &val, deadline);
if (rc == SUSPENDERS_TIMEDOUT) {
    /* nothing arrived in time */
}
```

Every blocking call also respects a per-coroutine deadline set with
`suspenders_deadline()` — a blanket timeout for any sequence of operations.

### QoS priorities

Every coroutine runs at one of four priority levels. The scheduler drains
higher levels first:

| Level | Use for |
|---|---|
| `SUSPENDERS_QOS_REALTIME` | Latency-critical accept loops, timer callbacks |
| `SUSPENDERS_QOS_HIGH` | Server listeners, request handlers |
| `SUSPENDERS_QOS_NORMAL` | General work (the default) |
| `SUSPENDERS_QOS_LOW` | Background housekeeping, bulk processing |

Priority is set at spawn time. `suspenders_boost()` temporarily raises a
coroutine's effective priority (priority inheritance) — it reverts when that
coroutine next completes a wait.

### Cancellation

Call `suspenders_cancel(cr)` on any coroutine. If it's blocked, it wakes
immediately with `SUSPENDERS_CANCELED`. If it's running, its next blocking
call fails with `SUSPENDERS_CANCELED`. The request is consumed on delivery —
one cancel, one failure.

A coroutine can check `suspenders_canceled()` to peek without consuming, and
push cleanup handlers that run automatically on exit (including cancellation).

### Memory

Suspenders never calls `malloc` or `free` in hot paths. Every coroutine
stack is carved from a Memento arena; control blocks live inside those arenas.
You don't need to think about this unless you're writing a custom transport —
just know that the library won't surprise your allocator.

---

## Table of contents

- [Runtime](#runtime)
- [Coroutines](#coroutines)
- [Identity and introspection](#identity-and-introspection)
- [Cancellation and cleanup](#cancellation-and-cleanup)
- [Timers and time](#timers-and-time)
- [Sleep](#sleep)
- [Channels](#channels)
- [Select](#select)
- [Mutex](#mutex)
- [Read-write lock](#read-write-lock)
- [Condition variable](#condition-variable)
- [Wait group](#wait-group)
- [Task queues](#task-queues)
- [Coroutine pool](#coroutine-pool)
- [Hoses (async I/O)](#hoses)
- [Transports](#transports)
- [Ticket lock](#ticket-lock)

---

## Runtime

Three calls bracket your program. Everything else happens between them.

### suspenders_init()

```c
int suspenders_init(unsigned num_workers, unsigned queue_hint);
```

Boot the scheduler with `num_workers` worker threads. Pass 0 for a single
worker (no extra threads — the calling thread does all the work inside
`suspenders_run`). `queue_hint` sizes the io_uring submission queue; 0
defaults to 256.

Call this once, from one thread, before spawning anything.

**Returns** `SUSPENDERS_OK` or an error code.

### suspenders_run()

```c
void suspenders_run(void);
```

Enter the event loop. The calling thread becomes worker 0; any additional
workers run on their own threads. Returns when every coroutine has finished
and no task queue work or timer is pending.

You can call `run` again after spawning more work — the runtime stays alive
between `init` and `shutdown`.

### suspenders_shutdown()

```c
void suspenders_shutdown(void);
```

Stop and join helper threads, reclaim parked coroutines and queues, reset
the runtime. Call from the same thread that called `init`, after `run` has
returned (or without calling `run`, to abandon work). Safe to call
`suspenders_init` again afterward.

---

## Coroutines

A coroutine is a function with its own stack. It looks like a thread but it's
cooperatively scheduled — it runs until it yields, suspends, or blocks on a
channel/hose/lock. Context switches take under 100 ns. You can have tens of
thousands of them.

### suspenders_spawn()

```c
suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos);
```

Create a coroutine that will call `func(arg)` at priority `qos`. The
coroutine doesn't run immediately — it's placed in the ready queue (or the
global injector, if spawned from outside a coroutine / from a foreign
thread).

Safe from any thread after a successful `suspenders_init` (including
threads that never called init). Foreign-thread spawns always go through
the global injector. `suspenders_run` / `suspenders_shutdown` must still
run on the init thread.

Coroutines pin to the first worker that runs them. After that, all their
work happens on that worker. Cross-worker wakes (from `resume`, channels,
etc.) route through an MPSC inbox.

**Returns** a pointer to the coroutine, or `NULL` on failure
(`SUSPENDERS_NOTINIT` if the runtime is down, `SUSPENDERS_NOMEM` on OOM).

### suspenders_go()

```c
suspenders_cr_t* suspenders_go(void (*func)(void*), void *arg);
```

Shorthand for `suspenders_spawn(func, arg, SUSPENDERS_QOS_NORMAL)`.

### suspenders_yield()

```c
void suspenders_yield(void);
```

Reschedule the current coroutine. It goes to the back of its priority queue,
giving other coroutines at the same (or higher) priority a chance to run.
Coroutine context only.

### suspenders_suspend()

```c
void suspenders_suspend(void);
```

Park the current coroutine. It will not run again until another coroutine
calls `suspenders_resume()` on it. This is the building block for custom
synchronization — channels and locks use it internally.

### suspenders_resume()

```c
void suspenders_resume(suspenders_cr_t *cr);
```

Wake a suspended coroutine. Safe to call from any worker — if `cr` is pinned
to a different worker, the wake routes through that worker's inbox. No-op if
`cr` is not suspended.

### suspenders_boost()

```c
void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos);
```

Temporarily raise `target`'s effective QoS to `new_qos` (priority
inheritance). The boost reverts the next time the coroutine completes a wait.
Use this when a high-priority coroutine is blocked waiting for a lower-
priority one — boost the blocker so it finishes faster.

**Example** — suspend/resume with manual scheduling:

```c
static suspenders_cr_t *workers[4];

void worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 5; i++) {
        printf("Worker %d: step %d\n", id, i + 1);
        suspenders_suspend();
    }
}

void controller(void *arg) {
    (void)arg;
    for (int i = 0; i < 4; i++)
        workers[i] = suspenders_spawn(worker, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);

    suspenders_yield();  /* let them run their first step */

    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 4; i++)
            suspenders_resume(workers[i]);
        suspenders_yield();
    }
}
```

---

## Identity and introspection

### suspenders_self()

```c
suspenders_cr_t* suspenders_self(void);
```

Return the current coroutine, or `NULL` outside a coroutine.

### suspenders_getid()

```c
uint64_t suspenders_getid(void);
```

Return the current coroutine's unique ID (monotonically increasing from 1),
or 0 outside a coroutine.

### suspenders_setname()

```c
int suspenders_setname(const char *name);
```

Give the current coroutine a name (up to 31 characters). Useful for
debugging.

### suspenders_getname()

```c
const char* suspenders_getname(void);
```

Return the current coroutine's name, or `""` if none was set.

### suspenders_stack_size()

```c
size_t suspenders_stack_size(void);
```

Return the stack size of the current coroutine (default 1 MB).

---

## Cancellation and cleanup

### suspenders_cancel()

```c
int suspenders_cancel(suspenders_cr_t *cr);
```

Request cancellation of `cr`. If it's blocked (on a channel, lock, sleep,
hose, etc.), it wakes with `SUSPENDERS_CANCELED`. If it's running, its next
blocking call fails with that code. The request is consumed on delivery.

**Returns** `SUSPENDERS_OK`, or `SUSPENDERS_INVAL` if `cr` is `NULL`.

### suspenders_canceled()

```c
bool suspenders_canceled(void);
```

Peek at whether cancellation has been requested for the current coroutine.
Does not consume the request.

### suspenders_deadline()

```c
int suspenders_deadline(uint64_t deadline_ns);
```

Set a blanket deadline for the current coroutine. Every blocking call past
this time fails with `SUSPENDERS_TIMEDOUT`. Pass 0 to disarm.

This is orthogonal to per-call `_dl` deadlines — whichever fires first wins.

### suspenders_cleanup_push()

```c
void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void*), void *arg);
```

Register a cleanup handler that runs when the coroutine exits (including
via cancellation). `node` is caller-allocated and must stay in scope until
popped or the coroutine exits. Handlers run LIFO — same as `pthread_cleanup_push`.

### suspenders_cleanup_pop()

```c
void suspenders_cleanup_pop(int execute);
```

Remove the most recently pushed cleanup handler. If `execute` is non-zero,
run it immediately; otherwise discard it.

### suspenders_exit()

```c
void suspenders_exit(void);
```

Terminate the current coroutine, running all remaining cleanup handlers.
No-op outside a coroutine.

---

## Timers and time

### suspenders_timer_create()

```c
suspenders_timer_t* suspenders_timer_create(int ms, bool repeat,
                                            void (*cb)(void*), void *arg);
```

Create a timer that fires `cb(arg)` after `ms` milliseconds. If `repeat` is
true, it re-arms automatically. The callback runs in coroutine context on the
scheduler — you can call any suspenders API from it.

You must call `suspenders_timer_cancel()` to free the timer, whether or not
it has fired. A repeating timer keeps `suspenders_run` alive.

**Returns** the timer, or `NULL` on failure.

### suspenders_timer_cancel()

```c
void suspenders_timer_cancel(suspenders_timer_t *t);
```

Cancel and free a timer. Safe to call at any time.

### suspenders_now_ns()

```c
uint64_t suspenders_now_ns(void);
```

Return the current time in nanoseconds from the monotonic clock
(`CLOCK_MONOTONIC`). Use this for deadline arithmetic.

---

## Sleep

### suspenders_sleep_ns()

```c
int suspenders_sleep_ns(uint64_t ns);
```

Suspend the current coroutine for `ns` nanoseconds. Other coroutines run
while this one sleeps. Coroutine context only.

**Returns** `SUSPENDERS_OK`, or `SUSPENDERS_CANCELED` / `SUSPENDERS_TIMEDOUT`
if the sleep is interrupted by cancellation or a `suspenders_deadline`.

### suspenders_sleep_dl()

```c
int suspenders_sleep_dl(uint64_t deadline_ns);
```

Sleep until the absolute deadline `deadline_ns`. Same return codes.

---

## Channels

Channels are typed, blocking queues for passing data between coroutines.
They come in two flavors:

- **Rendezvous** (`buf_sz == 0`): every send blocks until a receiver is
  ready. The data moves directly — no copy, no buffer. This is the fastest
  synchronization primitive in the library.

- **Buffered** (`buf_sz > 0`): sends succeed immediately until the buffer is
  full; receives succeed immediately if the buffer has data. The buffer is a
  ring allocated with the channel.

Channels follow Go semantics for closing: receivers drain any buffered data,
then get `SUSPENDERS_CLOSED`. Senders fail immediately on a closed channel.

### suspenders_chan_create()

```c
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
```

Create a channel carrying elements of `elem_sz` bytes with a buffer of
`buf_sz` elements. Pass 0 for `buf_sz` to get a rendezvous channel.

**Returns** the channel, or `NULL` on failure.

### suspenders_chan_make()

```c
suspenders_chan_t* suspenders_chan_make(size_t elem_sz, size_t buf_sz);
```

Alias for `suspenders_chan_create`. If you're coming from Go, this name
may feel more natural.

### suspenders_chan_destroy()

```c
void suspenders_chan_destroy(suspenders_chan_t *ch);
```

Free the channel. The caller is responsible for ensuring no coroutine is
still blocked on it.

### suspenders_chan_send()

```c
int suspenders_chan_send(suspenders_chan_t *ch, void *val);
```

Send `elem_sz` bytes from `val` into the channel. Blocks until a receiver is
ready (rendezvous) or buffer space is available (buffered).

**Returns** `SUSPENDERS_OK`, or `SUSPENDERS_CLOSED` / `SUSPENDERS_CANCELED` /
`SUSPENDERS_PERM` (outside a coroutine).

### suspenders_chan_send_dl()

```c
int suspenders_chan_send_dl(suspenders_chan_t *ch, void *val, uint64_t deadline_ns);
```

Like `send`, with a deadline. Returns `SUSPENDERS_TIMEDOUT` if the deadline
passes before a receiver appears.

### suspenders_chan_try_send()

```c
int suspenders_chan_try_send(suspenders_chan_t *ch, void *val);
```

Non-blocking send. Returns `SUSPENDERS_OK` if the send completed immediately,
`SUSPENDERS_FULL` if it would block, or `SUSPENDERS_CLOSED`.

### suspenders_chan_recv()

```c
int suspenders_chan_recv(suspenders_chan_t *ch, void *out);
```

Receive `elem_sz` bytes into `out`. Blocks until a sender is ready or
buffered data is available.

**Returns** `SUSPENDERS_OK`, or `SUSPENDERS_CLOSED` (after the buffer drains) /
`SUSPENDERS_CANCELED` / `SUSPENDERS_PERM`.

### suspenders_chan_recv_dl()

```c
int suspenders_chan_recv_dl(suspenders_chan_t *ch, void *out, uint64_t deadline_ns);
```

Like `recv`, with a deadline.

### suspenders_chan_try_recv()

```c
int suspenders_chan_try_recv(suspenders_chan_t *ch, void *out);
```

Non-blocking receive. Returns `SUSPENDERS_OK` or `SUSPENDERS_EMPTY`.

### suspenders_chan_close()

```c
int suspenders_chan_close(suspenders_chan_t *ch);
```

Close the channel. Blocked receivers drain any remaining buffer, then get
`SUSPENDERS_CLOSED`. Blocked senders wake with `SUSPENDERS_CLOSED`
immediately. Closing an already-closed channel is a no-op.

**Example** — three producers, one consumer, rendezvous channel:

```c
static suspenders_chan_t *ch;

void producer(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 1000; i++) {
        int val = id * 1000000 + i;
        suspenders_chan_send(ch, &val);
    }
}

void consumer(void *arg) {
    (void)arg;
    long sum = 0;
    for (int i = 0; i < 3000; i++) {
        int val;
        suspenders_chan_recv(ch, &val);
        sum += val;
    }
    printf("sum = %ld\n", sum);
}

int main(void) {
    suspenders_init(0, 0);
    ch = suspenders_chan_create(sizeof(int), 0);

    suspenders_go(consumer, NULL);
    for (int i = 0; i < 3; i++)
        suspenders_go(producer, (void*)(intptr_t)i);

    suspenders_run();
    suspenders_chan_destroy(ch);
    suspenders_shutdown();
}
```

---

## Select

### suspenders_select()

```c
int suspenders_select(suspenders_chan_op_t *ops, int n);
```

Wait on up to `SUSPENDERS_SELECT_MAX` (64) channel operations simultaneously.
Each element of `ops` describes either a send or a receive:

```c
typedef struct {
    suspenders_chan_t *ch;
    void *val;       /* data to send, or buffer to receive into */
    bool  is_send;   /* true = send, false = receive */
} suspenders_chan_op_t;
```

When multiple cases are ready, one is chosen at random (fairness). Returns
the index of the winning case, with `suspenders_errno` set to
`SUSPENDERS_OK` or `SUSPENDERS_CLOSED` (if that channel was closed).

Returns a negative error code on failure (`SUSPENDERS_CANCELED`,
`SUSPENDERS_INVAL`, `SUSPENDERS_PERM`).

### suspenders_select_dl()

```c
int suspenders_select_dl(suspenders_chan_op_t *ops, int n, uint64_t deadline_ns);
```

Like `select`, with a deadline. Returns `SUSPENDERS_TIMEDOUT` if no case
becomes ready in time.

**Example** — multiplexing two channels:

```c
int val_a, val_b;
suspenders_chan_op_t ops[] = {
    { ch_a, &val_a, false },  /* recv from ch_a */
    { ch_b, &val_b, false },  /* recv from ch_b */
};

int idx = suspenders_select(ops, 2);
if (idx == 0) {
    printf("got %d from A\n", val_a);
} else if (idx == 1) {
    printf("got %d from B\n", val_b);
}
```

---

## Mutex

Coroutine-aware mutex with FIFO handoff and single-level priority inheritance.
Value type — declare it on the stack or embed it in a struct, then
`suspenders_mutex_init` before use.

### suspenders_mutex_init()

```c
int suspenders_mutex_init(suspenders_mutex_t *m);
```

### suspenders_mutex_lock()

```c
int suspenders_mutex_lock(suspenders_mutex_t *m);
```

Acquire the mutex. If it's held, the calling coroutine parks until the
holder unlocks. Other coroutines run while this one waits — no spinning.

**Returns** `SUSPENDERS_OK` / `SUSPENDERS_CANCELED` / `SUSPENDERS_TIMEDOUT`.

### suspenders_mutex_lock_dl()

```c
int suspenders_mutex_lock_dl(suspenders_mutex_t *m, uint64_t deadline_ns);
```

### suspenders_mutex_trylock()

```c
int suspenders_mutex_trylock(suspenders_mutex_t *m);
```

Try to acquire without blocking. Returns `SUSPENDERS_OK` or
`SUSPENDERS_BUSY`.

### suspenders_mutex_unlock()

```c
int suspenders_mutex_unlock(suspenders_mutex_t *m);
```

Release the mutex and hand it to the next waiter (FIFO).

---

## Read-write lock

Coroutine-aware rwlock with FIFO ordering and reader batching. A waiting
writer blocks later readers, preventing writer starvation.

### suspenders_rwlock_init()

```c
int suspenders_rwlock_init(suspenders_rwlock_t *rw);
```

### suspenders_rwlock_rdlock()

```c
int suspenders_rwlock_rdlock(suspenders_rwlock_t *rw);
```

Acquire a read lock. Multiple readers can hold the lock simultaneously.

### suspenders_rwlock_rdlock_dl()

```c
int suspenders_rwlock_rdlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns);
```

### suspenders_rwlock_tryrdlock()

```c
int suspenders_rwlock_tryrdlock(suspenders_rwlock_t *rw);
```

### suspenders_rwlock_wrlock()

```c
int suspenders_rwlock_wrlock(suspenders_rwlock_t *rw);
```

Acquire a write lock. Exclusive — no other readers or writers.

### suspenders_rwlock_wrlock_dl()

```c
int suspenders_rwlock_wrlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns);
```

### suspenders_rwlock_trywrlock()

```c
int suspenders_rwlock_trywrlock(suspenders_rwlock_t *rw);
```

### suspenders_rwlock_unlock()

```c
int suspenders_rwlock_unlock(suspenders_rwlock_t *rw);
```

Release either a read or write lock.

---

## Condition variable

### suspenders_cond_init()

```c
int suspenders_cond_init(suspenders_cond_t *c);
```

### suspenders_cond_wait()

```c
int suspenders_cond_wait(suspenders_cond_t *c, suspenders_mutex_t *m);
```

Atomically release `m` and park until signaled, then re-acquire `m`. Same
semantics as `pthread_cond_wait` — always use it in a loop that re-checks
your predicate.

### suspenders_cond_wait_dl()

```c
int suspenders_cond_wait_dl(suspenders_cond_t *c, suspenders_mutex_t *m, uint64_t deadline_ns);
```

### suspenders_cond_signal()

```c
int suspenders_cond_signal(suspenders_cond_t *c);
```

Wake one waiting coroutine.

### suspenders_cond_broadcast()

```c
int suspenders_cond_broadcast(suspenders_cond_t *c);
```

Wake all waiting coroutines.

---

## Wait group

A counter that lets coroutines wait for a batch of work to complete.

### suspenders_waitgroup_init()

```c
int suspenders_waitgroup_init(suspenders_waitgroup_t *wg);
```

### suspenders_waitgroup_add()

```c
int suspenders_waitgroup_add(suspenders_waitgroup_t *wg, int delta);
```

Add `delta` to the counter. Call before spawning work.

### suspenders_waitgroup_done()

```c
int suspenders_waitgroup_done(suspenders_waitgroup_t *wg);
```

Decrement the counter by 1. When it reaches zero, all waiters wake.

### suspenders_waitgroup_wait()

```c
int suspenders_waitgroup_wait(suspenders_waitgroup_t *wg);
```

Block until the counter reaches zero.

### suspenders_waitgroup_wait_dl()

```c
int suspenders_waitgroup_wait_dl(suspenders_waitgroup_t *wg, uint64_t deadline_ns);
```

---

## Task queues

Task queues are libdispatch-style work submission. You create a queue with a
label, a QoS level, and a concurrency count. Submitted tasks run as daemon
coroutines — they keep `suspenders_run` alive only while tasks are pending or
running, not while the queue is idle.

A queue with `concurrency == 1` is serial: tasks run in strict submission
order. Concurrent queues run up to `concurrency` tasks at once.

### suspenders_queue_create()

```c
suspenders_queue_t* suspenders_queue_create(const char *label,
                                            suspenders_qos_t qos,
                                            unsigned concurrency);
```

Create a task queue. `label` is a human-readable name (for debugging).
`concurrency` is the number of drainer coroutines — 1 for serial, more for
concurrent.

**Returns** the queue, or `NULL` on failure.

### suspenders_get_global_queue()

```c
suspenders_queue_t* suspenders_get_global_queue(suspenders_qos_t qos);
```

Return the shared global concurrent queue for the given QoS level. Global
queues are created at `init` and freed at `shutdown` — don't destroy them.

### suspenders_queue_async()

```c
int suspenders_queue_async(suspenders_queue_t *q, void (*fn)(void*), void *arg);
```

Submit `fn(arg)` for asynchronous execution. Returns immediately. Inside a
coroutine, blocks if the queue's internal channel is full. Outside a
coroutine, returns `SUSPENDERS_FULL` if the queue can't accept work.

### suspenders_queue_sync()

```c
int suspenders_queue_sync(suspenders_queue_t *q, void (*fn)(void*), void *arg);
```

Submit `fn(arg)` and block the calling coroutine until it completes.
Coroutine context only.

### suspenders_queue_after()

```c
int suspenders_queue_after(suspenders_queue_t *q, uint64_t delay_ns,
                           void (*fn)(void*), void *arg);
```

Submit `fn(arg)` to run after `delay_ns` nanoseconds. The task enters the
queue when the timer fires; it doesn't jump ahead of earlier submissions.

### suspenders_queue_barrier_async()

```c
int suspenders_queue_barrier_async(suspenders_queue_t *q,
                                   void (*fn)(void*), void *arg);
```

Submit a barrier task. It waits for every earlier task to finish, runs alone,
then allows later tasks to proceed. On a serial queue this is equivalent to
`async` (everything is already serialized).

### suspenders_queue_destroy()

```c
void suspenders_queue_destroy(suspenders_queue_t *q);
```

Close the queue. Inside a coroutine, this blocks until pending tasks drain.
Outside a coroutine, the queue is freed during `suspenders_shutdown` after
drainers exit.

### suspenders_queue_label()

```c
const char* suspenders_queue_label(const suspenders_queue_t *q);
```

Return the queue's label.

**Example** — serial queue with barrier and sync:

```c
void coordinator(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("work", SUSPENDERS_QOS_NORMAL, 1);

    for (int i = 0; i < 5; i++)
        suspenders_queue_async(q, inc, NULL);

    suspenders_queue_barrier_async(q, checkpoint, NULL);

    int result = -1;
    suspenders_queue_sync(q, read_counter, &result);
    printf("counter is now %d\n", result);

    suspenders_queue_destroy(q);  /* waits for drain */
}
```

---

## Coroutine pool

A pool is a thin wrapper around a concurrent task queue. It gives you N
worker coroutines pulling from a shared channel.

### suspenders_pool_create()

```c
suspenders_pool_t* suspenders_pool_create(unsigned nworkers, suspenders_qos_t qos);
```

Create a pool with `nworkers` coroutines at priority `qos`.

### suspenders_pool_submit()

```c
void suspenders_pool_submit(suspenders_pool_t *pool, void (*fn)(void*), void *arg);
```

Submit work to the pool. If all workers are busy, the task queues until one
becomes available.

### suspenders_pool_destroy()

```c
void suspenders_pool_destroy(suspenders_pool_t *pool);
```

Shut down the pool. Pending tasks run to completion.

---

## Hoses

A hose is Suspenders' async I/O primitive. It wraps a file descriptor with a
transport-agnostic interface — `dial` a URI, `listen` on one, `accept`
connections, then `read` and `write`. Every blocking operation suspends the
calling coroutine and resumes it when the I/O completes. On Linux, that
means io_uring under the hood. You never see a completion callback or a
poll loop.

The URI scheme selects the transport: `tcp://`, `udp://`, `unix://`,
`tty://`. You can register your own.

### suspenders_hose_init()

```c
void suspenders_hose_init(suspenders_hose_t *d, struct buf *b);
```

Initialize a hose. `b` is an optional dynamic buffer (pass `NULL` if you
don't need one). A hose is a value type — declare it on the stack.

### suspenders_hose_dial()

```c
bool suspenders_hose_dial(suspenders_hose_t *d, const char *uri);
```

Connect to `uri`. Suspends the calling coroutine until the connection is
established (or fails).

**Returns** `true` on success, `false` on failure (`suspenders_errno` is set).

### suspenders_hose_dial_dl()

```c
bool suspenders_hose_dial_dl(suspenders_hose_t *d, const char *uri, uint64_t deadline_ns);
```

### suspenders_hose_listen()

```c
bool suspenders_hose_listen(suspenders_hose_t *d, const char *uri);
```

Bind and listen on `uri`. The hose becomes a listener — call `accept` to
get client connections.

### suspenders_hose_accept()

```c
bool suspenders_hose_accept(suspenders_hose_t *d, suspenders_hose_t *client);
```

Accept a connection from a listening hose. Suspends until a client connects.
On success, `client` is initialized and ready for I/O.

### suspenders_hose_accept_dl()

```c
bool suspenders_hose_accept_dl(suspenders_hose_t *d, suspenders_hose_t *client, uint64_t deadline_ns);
```

### suspenders_hose_read()

```c
ssize_t suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len);
```

Read up to `len` bytes. Suspends until data arrives.

**Returns** bytes read, 0 on EOF, or -1 on error.

### suspenders_hose_read_dl()

```c
ssize_t suspenders_hose_read_dl(suspenders_hose_t *d, void *dest, size_t len, uint64_t deadline_ns);
```

### suspenders_hose_write()

```c
ssize_t suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len);
```

Write `len` bytes. Suspends until the write completes.

**Returns** bytes written, or -1 on error.

### suspenders_hose_write_dl()

```c
ssize_t suspenders_hose_write_dl(suspenders_hose_t *d, const void *src, size_t len, uint64_t deadline_ns);
```

### suspenders_hose_readv()

```c
ssize_t suspenders_hose_readv(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
```

Scatter read into multiple buffers.

### suspenders_hose_writev()

```c
ssize_t suspenders_hose_writev(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
```

Gather write from multiple buffers.

### suspenders_hose_recvfrom()

```c
ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len,
                                 struct sockaddr *addr, socklen_t *addrlen);
```

Receive a datagram and store the sender's address. For UDP hoses.

### suspenders_hose_recvfrom_dl()

```c
ssize_t suspenders_hose_recvfrom_dl(suspenders_hose_t *d, void *dest, size_t len,
                                    struct sockaddr *addr, socklen_t *addrlen,
                                    uint64_t deadline_ns);
```

### suspenders_hose_sendto()

```c
ssize_t suspenders_hose_sendto(suspenders_hose_t *d, const void *src, size_t len,
                               const struct sockaddr *addr, socklen_t addrlen);
```

Send a datagram to a specific address.

### suspenders_hose_sendto_dl()

```c
ssize_t suspenders_hose_sendto_dl(suspenders_hose_t *d, const void *src, size_t len,
                                  const struct sockaddr *addr, socklen_t addrlen,
                                  uint64_t deadline_ns);
```

### suspenders_hose_shutdown()

```c
int suspenders_hose_shutdown(suspenders_hose_t *d, int how);
```

Shut down one or both directions of the connection.

| `how` | Constant | Effect |
|---|---|---|
| 0 | `SUSPENDERS_SHUT_RD` | No more reads |
| 1 | `SUSPENDERS_SHUT_WR` | No more writes |
| 2 | `SUSPENDERS_SHUT_RDWR` | Shut down both |

### suspenders_hose_set_option()

```c
int suspenders_hose_set_option(suspenders_hose_t *d, int level, int optname,
                               const void *optval, socklen_t optlen);
```

Set a socket option (wraps `setsockopt`).

### suspenders_hose_peername()

```c
int suspenders_hose_peername(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen);
```

Get the remote address of a connected hose.

### suspenders_hose_sockname()

```c
int suspenders_hose_sockname(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen);
```

Get the local address of a bound hose.

### suspenders_hose_close()

```c
void suspenders_hose_close(suspenders_hose_t *d);
```

Close the hose and release the file descriptor.

**Example** — TCP echo server:

```c
void echo_handler(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buf[4096];
    ssize_t n;
    while ((n = suspenders_hose_read(client, buf, sizeof(buf))) > 0) {
        suspenders_hose_write(client, buf, (size_t)n);
    }
    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
}

void server(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    suspenders_hose_listen(&listener, "tcp://0.0.0.0:12345");
    printf("listening on :12345\n");

    for (;;) {
        suspenders_hose_t *client = memento_thread_heap_alloc(
            memento_thread_heap_get(), sizeof(*client));
        if (suspenders_hose_accept(&listener, client)) {
            suspenders_go(echo_handler, client);
        } else {
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
        }
    }
}

int main(void) {
    suspenders_init(4, 256);
    suspenders_spawn(server, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_run();
    suspenders_shutdown();
}
```

Notice that the echo handler looks exactly like blocking, synchronous code.
There's no callback, no state machine, no future to await. The coroutine
suspends inside `suspenders_hose_read`, the scheduler runs other coroutines,
and when io_uring delivers a completion the coroutine picks up right where
it left off. Thousands of connections, straight-line code.

---

## Transports

Suspenders ships with built-in transports for `tcp://`, `udp://`, `unix://`,
and `tty://`. You can add your own by implementing the transport ops vtable
and registering it.

### suspenders_transport_register()

```c
bool suspenders_transport_register(const suspenders_transport_ops_t *ops);
```

Register a custom transport. `ops->scheme` is the URI scheme (e.g. `"quic"`).
Once registered, `suspenders_hose_dial("quic://...")` uses your transport.

### suspenders_transport_find()

```c
const suspenders_transport_ops_t* suspenders_transport_find(const char *scheme);
```

Look up a transport by scheme. Returns `NULL` if not found.

The vtable:

```c
typedef struct suspenders_transport_ops {
    const char *scheme;
    bool (*dial)(suspenders_hose_t *h, const char *host, int port);
    bool (*listen)(suspenders_hose_t *h, const char *host, int port);
    bool (*accept)(suspenders_hose_t *listener, suspenders_hose_t *client);
    ssize_t (*read)(suspenders_hose_t *h, void *dest, size_t len);
    ssize_t (*write)(suspenders_hose_t *h, const void *src, size_t len);
    ssize_t (*readv)(suspenders_hose_t *h, const struct iovec *iov, int iovcnt);
    ssize_t (*writev)(suspenders_hose_t *h, const struct iovec *iov, int iovcnt);
    ssize_t (*recvfrom)(suspenders_hose_t *h, void *dest, size_t len,
                        struct sockaddr *addr, socklen_t *addrlen);
    ssize_t (*sendto)(suspenders_hose_t *h, const void *src, size_t len,
                      const struct sockaddr *addr, socklen_t addrlen);
    void (*close)(suspenders_hose_t *h);
} suspenders_transport_ops_t;
```

Implement the methods your transport needs; set the rest to `NULL`. The
hose layer handles `NULL` methods gracefully (returns `SUSPENDERS_NOTFOUND`
or -1).

---

## Ticket lock

A strict FIFO spinlock used internally by channels and sync primitives. It's
exposed in case you need a lightweight lock for very short critical sections
that don't warrant the overhead of parking a coroutine.

### suspenders_ticket_init()

```c
static inline void suspenders_ticket_init(suspenders_ticket_lock_t *l);
```

### suspenders_ticket_lock()

```c
static inline void suspenders_ticket_lock(suspenders_ticket_lock_t *l);
```

Spin until acquired. FIFO ordering — no starvation.

### suspenders_ticket_unlock()

```c
static inline void suspenders_ticket_unlock(suspenders_ticket_lock_t *l);
```

---

## Thread-local state

```c
extern SUSPENDERS_TLS int suspenders_errno;
```

The error code of the most recent failing call on this thread. Set by every
function that can fail.

### suspenders_strerror()

```c
const char* suspenders_strerror(int err);
```

Return a human-readable string for an error code. The returned pointer is
to a static string — don't free it.

---

## Types at a glance

| Type | What it is |
|---|---|
| `suspenders_cr_t` | Coroutine control block (cache-line aligned) |
| `suspenders_ctx_t` | Architecture-specific register context |
| `suspenders_chan_t` | Channel (rendezvous or buffered ring) |
| `suspenders_chan_op_t` | One case of a `select` |
| `suspenders_hose_t` | Async I/O handle |
| `suspenders_mutex_t` | Coroutine mutex (value type) |
| `suspenders_rwlock_t` | Coroutine read-write lock (value type) |
| `suspenders_cond_t` | Coroutine condition variable (value type) |
| `suspenders_waitgroup_t` | Coroutine wait group (value type) |
| `suspenders_timer_t` | Timer (one-shot or repeating) |
| `suspenders_cleanup_t` | Cleanup handler node (stack-allocated) |
| `suspenders_ticket_lock_t` | FIFO spinlock (value type) |
| `suspenders_queue_t` | Task queue (opaque) |
| `suspenders_pool_t` | Coroutine pool (opaque) |
| `suspenders_transport_ops_t` | Transport vtable |
| `suspenders_qos_t` | QoS priority level |
| `suspenders_state_t` | Coroutine state (READY/RUNNING/SUSPENDED/DONE) |
