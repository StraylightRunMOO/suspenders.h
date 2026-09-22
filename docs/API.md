# Suspenders C API

Reference for `suspenders.h`, version 1.2.0. The C++ wrappers are in
[API_CPP.md](API_CPP.md). They call this. When the two disagree, this is
the one the machine runs.

A coroutine is a function with its own stack. A blocking call parks that
stack and the worker runs someone else. The call returns into the same
frame. That is the whole trick. Everything else is scheduling, a place to
put a value while you wait, or a socket that knows how to wait.

## Getting started

One translation unit defines the implementation. Memento's header comes
first, because Suspenders allocates from it and does not apologize.

```c
#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
```

Every other file includes `suspenders.h` and defines nothing. On Linux the
implementation unit links liburing 2.0 or later and pthread. Callers of
the header do not.

```c
#include <stdio.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

static void hello(void *arg) {
    (void)arg;
    printf("Hello from a coroutine.\n");
}

int main(void) {
    suspenders_init(0, 0);
    suspenders_go(hello, NULL);
    suspenders_run();
    suspenders_shutdown();
    return 0;
}
```

`init` builds the scheduler. `go` creates a coroutine at normal priority
and does not run it. `run` runs it, and anything else that appears, until
nothing is left. `shutdown` joins the workers and resets the runtime,
including Memento. The interesting code lives between `init` and
`shutdown`. Code that lives outside them gets `SUSPENDERS_PERM` or
`SUSPENDERS_NOTINIT`, which is the library's way of saying you are early.

## Contracts

### Errors

Zero is success. Negative is not. The library does not abort. You get a
code, and the thread-local `suspenders_errno` holds the same code from the
most recent failure on this thread. `suspenders_strerror` turns it into a
static string. Do not free the string. It was never yours.

| Constant | Value | Meaning |
|---|---|---|
| `SUSPENDERS_OK` | 0 | Done |
| `SUSPENDERS_ERROR` | -1 | The system call failed. `errno` has the rest |
| `SUSPENDERS_INVAL` | -2 | Bad argument |
| `SUSPENDERS_NOMEM` | -3 | Memento said no |
| `SUSPENDERS_TIMEDOUT` | -4 | Deadline passed |
| `SUSPENDERS_CANCELED` | -5 | This coroutine was canceled |
| `SUSPENDERS_CLOSED` | -6 | Channel is closed |
| `SUSPENDERS_EMPTY` | -7 | `try_recv` on an empty channel |
| `SUSPENDERS_FULL` | -8 | `try_send` on a full channel |
| `SUSPENDERS_BUSY` | -9 | `trylock` and someone else has it |
| `SUSPENDERS_NOTINIT` | -10 | `init` has not stuck |
| `SUSPENDERS_NOTFOUND` | -11 | No transport for that scheme |
| `SUSPENDERS_PERM` | -12 | This call is not legal from here |

### Deadlines

A `_dl` function takes an absolute time in nanoseconds, the same clock as
`suspenders_now_ns`. Pass 0 to wait with no deadline from this argument.
`suspenders_deadline` arms one deadline for every later blocking call on
this coroutine. The two races are settled the usual way: whoever expires
first wins, and you get `SUSPENDERS_TIMEDOUT`.

```c
uint64_t deadline = suspenders_now_ns() + 500 * 1000000ULL; /* 500 ms */
int rc = suspenders_chan_recv_dl(ch, &val, deadline);
if (rc == SUSPENDERS_TIMEDOUT) {
    /* the value was not that interested */
}
```

On io_uring a hose deadline is a linked timeout. The kernel cancels the
operation. The coroutine does not return while the read keeps running in
the background, which is the sort of bug that passes a unit test and fails
a Tuesday.

### Priority

Four lists per worker, drained from the top:

| Level | What belongs there |
|---|---|
| `SUSPENDERS_QOS_REALTIME` | Accept loops, timer callbacks |
| `SUSPENDERS_QOS_HIGH` | Listeners, request handlers |
| `SUSPENDERS_QOS_NORMAL` | Everything you did not think about |
| `SUSPENDERS_QOS_LOW` | Work you hope finishes eventually |

Set it at spawn. `suspenders_boost` raises effective priority and drops it
when that coroutine next finishes a wait. A mutex does this for you: the
holder is boosted to the waiter's level. Without that, four lists are a
way to wait politely behind the wrong coroutine.

### Cancellation

`suspenders_cancel(cr)` from any thread. If `cr` is blocked, it wakes with
`SUSPENDERS_CANCELED`. If it is running, the next blocking call fails with
that code. One request, one delivery, then it is consumed.
`suspenders_canceled` peeks and does not consume. Cleanup handlers pushed
with `suspenders_cleanup_push` run LIFO on the way out, including this way
out.

An in-flight io_uring operation is canceled with `IORING_OP_ASYNC_CANCEL`.
The completion still arrives. Exactly one of them wakes the coroutine.

### Memory

Hot paths do not call `malloc`. Stacks come from a Memento arena. Control
blocks live there too. Free returns the block to the thread that allocated
it, at the size you asked for. There is no header on the block, so the
size you pass to free is the size you passed to alloc. A free from the
wrong thread is pushed onto the owner's queue and reclaimed when that
owner is idle. `shutdown` calls `memento_shutdown`. If something else is
still using Memento, that something else is now using freed memory, which
is a brisk way to end a process.

## Runtime

Three calls. The rest of the API is what you do while they are in effect.

### suspenders_init()

```c
int suspenders_init(unsigned num_workers, unsigned queue_hint);
```

`num_workers == 0` is one worker. The calling thread does the work inside
`run`. A positive count is that many workers; the caller becomes worker 0
when `run` starts. `queue_hint` sizes the io_uring submission queue. 0
means 256.

Call it once, from one thread, before you spawn. It returns `SUSPENDERS_OK`
or an error. It also brings Memento up.

**Why not a worker per core chosen for you.** You know whether this
process is alone on the machine. The library does not.

### suspenders_run()

```c
void suspenders_run(void);
```

Enter the loop. Returns when every coroutine has finished and no queue
task or timer is still pending. Spawn more and call it again. The runtime
stays up until `shutdown`.

### suspenders_shutdown()

```c
void suspenders_shutdown(void);
```

Stop and join the other workers, reap what is still parked, reset. Same
thread as `init`, after `run` has returned. Calling it without `run`
abandons the work, which is legal and worth a comment at the call site.
`init` may be called again afterwards.

## Coroutines

The function runs until it yields, suspends, or blocks. You can have a
great many of them. The limit is memory for the stacks, 1 MB each unless
you change it, not a thread table in the kernel.

A spawn from this thread or another is legal after `init`. A foreign spawn
goes through the global injector. The coroutine is pinned to the worker
that first runs it. Later wakes from other workers arrive on that worker's
inbox. `run` and `shutdown` stay on the init thread.

### suspenders_spawn()

```c
suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos);
```

Create a coroutine that will call `func(arg)` at `qos`. It does not run
yet. Returns the control block, or `NULL` with `SUSPENDERS_NOTINIT` or
`SUSPENDERS_NOMEM`.

### suspenders_go()

```c
suspenders_cr_t* suspenders_go(void (*func)(void*), void *arg);
```

`spawn` at `SUSPENDERS_QOS_NORMAL`.

### suspenders_yield()

```c
void suspenders_yield(void);
```

Go to the back of your priority list. Coroutine context only. Other work
at the same or higher priority runs first. Work at a lower priority does
not, which is the point of having levels.

### suspenders_suspend()

```c
void suspenders_suspend(void);
```

Park until someone calls `suspenders_resume` on you. Channels and locks
are this, with a reason attached.

### suspenders_resume()

```c
void suspenders_resume(suspenders_cr_t *cr);
```

Wake a parked coroutine. Legal from any worker. If `cr` lives elsewhere,
the wake crosses the inbox. A no-op if `cr` is not parked. Resuming a
running coroutine does not make it run twice. The laws of scheduling are
dull, and they hold.

### suspenders_boost()

```c
void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos);
```

Raise `target`'s effective QoS until it next completes a wait. Use it when
a high-priority coroutine is stuck behind a low-priority one and the mutex
has not already done it for you.

```c
static suspenders_cr_t *workers[4];

void worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 5; i++) {
        printf("worker %d, step %d\n", id, i + 1);
        suspenders_suspend();
    }
}

void controller(void *arg) {
    (void)arg;
    for (int i = 0; i < 4; i++)
        workers[i] = suspenders_spawn(worker, (void *)(intptr_t)i,
                                      SUSPENDERS_QOS_NORMAL);
    suspenders_yield();
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 4; i++)
            suspenders_resume(workers[i]);
        suspenders_yield();
    }
}
```

## Identity

Outside a coroutine these return null, zero, or an empty string. They do
not invent a coroutine for you.

### suspenders_self()

```c
suspenders_cr_t* suspenders_self(void);
```

### suspenders_getid()

```c
uint64_t suspenders_getid(void);
```

Monotonic from 1. Stable for the life of the coroutine. Not reused, so a
stale id is a stale id and not a surprise reincarnation.

### suspenders_setname()

```c
int suspenders_setname(const char *name);
```

Up to 31 characters. For logs. The scheduler does not read it.

### suspenders_getname()

```c
const char* suspenders_getname(void);
```

The name, or `""`.

### suspenders_stack_size()

```c
size_t suspenders_stack_size(void);
```

Bytes. Default is 1 MB.

## Cancellation and cleanup

### suspenders_cancel()

```c
int suspenders_cancel(suspenders_cr_t *cr);
```

Returns `SUSPENDERS_OK`, or `SUSPENDERS_INVAL` if `cr` is null. Safe from
any thread.

### suspenders_canceled()

```c
bool suspenders_canceled(void);
```

Peek. Does not consume.

### suspenders_deadline()

```c
int suspenders_deadline(uint64_t deadline_ns);
```

Arm or, with 0, disarm the blanket deadline.

### suspenders_cleanup_push()

```c
void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void*), void *arg);
```

`node` is yours, and it must outlive the handler. Handlers run LIFO.
Same shape as `pthread_cleanup_push`, without the macro that eats a brace.

### suspenders_cleanup_pop()

```c
void suspenders_cleanup_pop(int execute);
```

Pop the latest handler. Non-zero `execute` runs it now. Zero discards it.
Discarding a handler that closes a socket is a decision you will remember.

### suspenders_exit()

```c
void suspenders_exit(void);
```

Leave the coroutine. Remaining handlers run. No-op outside one.

## Time

### suspenders_timer_create()

```c
suspenders_timer_t* suspenders_timer_create(int ms, bool repeat,
                                            void (*cb)(void*), void *arg);
```

Fire `cb(arg)` after `ms` milliseconds, on a coroutine, so the callback
may call the library. `repeat` re-arms. A repeating timer keeps `run`
alive, which is correct and also how a program forgets to exit. Cancel
the timer. Returns null on failure.

### suspenders_timer_cancel()

```c
void suspenders_timer_cancel(suspenders_timer_t *t);
```

Cancel and free. Safe if it already fired. Safe if it did not. Call it
once.

### suspenders_now_ns()

```c
uint64_t suspenders_now_ns(void);
```

Monotonic nanoseconds. `CLOCK_MONOTONIC` on POSIX, the performance counter
on Windows. Deadlines are differences of these values. Wall-clock time is
a different problem, and it is not this one.

### suspenders_sleep_ns()

```c
int suspenders_sleep_ns(uint64_t ns);
```

Park for `ns` nanoseconds. Other coroutines run. Returns `SUSPENDERS_OK`,
or `CANCELED` or `TIMEDOUT` if those hit first. Coroutine context only.

### suspenders_sleep_dl()

```c
int suspenders_sleep_dl(uint64_t deadline_ns);
```

Sleep until an absolute deadline. Same codes.

## Channels

`buf_sz == 0` is a rendezvous. Send and recv both wait, and the bytes move
once, from the sender's buffer to the receiver's. `buf_sz > 0` is a ring
of that many slots under a ticket lock, so waiters proceed in arrival
order. Send returns at once while a slot is free. Recv returns at once
while a slot is full.

Close lets receivers take what is left, then `SUSPENDERS_CLOSED`. A send
on a closed channel fails immediately. Closing twice is a no-op. Destroy
only when nobody is blocked on it. The channel will not check for you.

### suspenders_chan_create()

```c
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
```

`elem_sz == 0` fails. Returns null on failure.

### suspenders_chan_make()

```c
suspenders_chan_t* suspenders_chan_make(size_t elem_sz, size_t buf_sz);
```

The same function. Two names, one allocation.

### suspenders_chan_destroy()

```c
void suspenders_chan_destroy(suspenders_chan_t *ch);
```

### suspenders_chan_send()

```c
int suspenders_chan_send(suspenders_chan_t *ch, void *val);
```

Copy `elem_sz` bytes from `val`. Blocks as the flavor requires. Returns
`OK`, `CLOSED`, `CANCELED`, or `PERM`.

### suspenders_chan_send_dl()

```c
int suspenders_chan_send_dl(suspenders_chan_t *ch, void *val, uint64_t deadline_ns);
```

`TIMEDOUT` if the deadline wins.

### suspenders_chan_try_send()

```c
int suspenders_chan_try_send(suspenders_chan_t *ch, void *val);
```

`OK`, `FULL`, or `CLOSED`. Does not park.

### suspenders_chan_recv()

```c
int suspenders_chan_recv(suspenders_chan_t *ch, void *out);
```

`OK`, `CLOSED` once the buffer has drained, `CANCELED`, or `PERM`.

### suspenders_chan_recv_dl()

```c
int suspenders_chan_recv_dl(suspenders_chan_t *ch, void *out, uint64_t deadline_ns);
```

### suspenders_chan_try_recv()

```c
int suspenders_chan_try_recv(suspenders_chan_t *ch, void *out);
```

`OK` or `EMPTY`.

### suspenders_chan_close()

```c
int suspenders_chan_close(suspenders_chan_t *ch);
```

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
        suspenders_go(producer, (void *)(intptr_t)i);
    suspenders_run();
    suspenders_chan_destroy(ch);
    suspenders_shutdown();
}
```

Three producers, one consumer, no buffer. Each send waits for its recv.
The sum is the checksum. If it is wrong, the bug is not the channel.

## Select

Up to `SUSPENDERS_SELECT_MAX` (64) operations. One ready case is chosen
uniformly at random. That is the fairness property. It is also why a test
that depends on which case wins is testing the random-number generator.

```c
typedef struct {
    suspenders_chan_t *ch;
    void *val;       /* bytes to send, or where to store a recv */
    bool  is_send;
} suspenders_chan_op_t;
```

### suspenders_select()

```c
int suspenders_select(suspenders_chan_op_t *ops, int n);
```

Returns the index of the case that ran. `suspenders_errno` is `OK` or
`CLOSED` for that case. A negative return is `CANCELED`, `INVAL`, or
`PERM`.

### suspenders_select_dl()

```c
int suspenders_select_dl(suspenders_chan_op_t *ops, int n, uint64_t deadline_ns);
```

`TIMEDOUT` if nothing became ready.

```c
int val_a, val_b;
suspenders_chan_op_t ops[] = {
    { ch_a, &val_a, false },
    { ch_b, &val_b, false },
};
int idx = suspenders_select(ops, 2);
if (idx == 0) printf("from A: %d\n", val_a);
else if (idx == 1) printf("from B: %d\n", val_b);
```

## Mutex

A value. Put it on the stack or in a struct. Initialize it before use.
Waiters are FIFO. The holder is boosted to the waiter's QoS while you
wait, and the boost drops when the wait ends. The OS thread does not spin
and does not block.

### suspenders_mutex_init()

```c
int suspenders_mutex_init(suspenders_mutex_t *m);
```

### suspenders_mutex_lock()

```c
int suspenders_mutex_lock(suspenders_mutex_t *m);
```

`OK`, `CANCELED`, or `TIMEDOUT`.

### suspenders_mutex_lock_dl()

```c
int suspenders_mutex_lock_dl(suspenders_mutex_t *m, uint64_t deadline_ns);
```

### suspenders_mutex_trylock()

```c
int suspenders_mutex_trylock(suspenders_mutex_t *m);
```

`OK` or `BUSY`.

### suspenders_mutex_unlock()

```c
int suspenders_mutex_unlock(suspenders_mutex_t *m);
```

Hands the mutex to the oldest waiter.

## Read-write lock

FIFO. A waiting writer blocks new readers, so writers are not starved by a
stream of readers who each looked harmless alone.

### suspenders_rwlock_init()

```c
int suspenders_rwlock_init(suspenders_rwlock_t *rw);
```

### suspenders_rwlock_rdlock()

```c
int suspenders_rwlock_rdlock(suspenders_rwlock_t *rw);
```

Shared. Many readers, no writer.

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

Exclusive.

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

Releases whichever you hold. The lock believes you. If you are wrong, the
next waiter will be wrong in an interesting way.

## Condition variable

Same contract as `pthread_cond_wait`: the wait releases the mutex, parks,
and re-acquires it. Recheck the predicate. Signals are not a promise that
the predicate is true. They are a suggestion that you look.

### suspenders_cond_init()

```c
int suspenders_cond_init(suspenders_cond_t *c);
```

### suspenders_cond_wait()

```c
int suspenders_cond_wait(suspenders_cond_t *c, suspenders_mutex_t *m);
```

### suspenders_cond_wait_dl()

```c
int suspenders_cond_wait_dl(suspenders_cond_t *c, suspenders_mutex_t *m,
                            uint64_t deadline_ns);
```

### suspenders_cond_signal()

```c
int suspenders_cond_signal(suspenders_cond_t *c);
```

One waiter.

### suspenders_cond_broadcast()

```c
int suspenders_cond_broadcast(suspenders_cond_t *c);
```

All of them. They will then serialize on the mutex, which is fine, and
which is also why broadcast is not free.

## Wait group

A counter. Add before the work exists. `done` decrements. `wait` parks
until the counter is zero.

### suspenders_waitgroup_init()

```c
int suspenders_waitgroup_init(suspenders_waitgroup_t *wg);
```

### suspenders_waitgroup_add()

```c
int suspenders_waitgroup_add(suspenders_waitgroup_t *wg, int delta);
```

### suspenders_waitgroup_done()

```c
int suspenders_waitgroup_done(suspenders_waitgroup_t *wg);
```

### suspenders_waitgroup_wait()

```c
int suspenders_waitgroup_wait(suspenders_waitgroup_t *wg);
```

### suspenders_waitgroup_wait_dl()

```c
int suspenders_waitgroup_wait_dl(suspenders_waitgroup_t *wg, uint64_t deadline_ns);
```

## Task queues

A queue runs functions, not stacks. Use one when the work does not need
the caller's locals. Use a coroutine when it does.

`concurrency == 1` is a FIFO. A larger value runs that many tasks at once.
Drainers are daemon coroutines: a queue with nothing queued does not keep
`run` alive, and a queue with work does.

Inside a coroutine, `async` parks if the queue's internal channel is full.
Outside a coroutine, that case returns `SUSPENDERS_FULL` instead of
parking, because there is no coroutine to park.

### suspenders_queue_create()

```c
suspenders_queue_t* suspenders_queue_create(const char *label,
                                            suspenders_qos_t qos,
                                            unsigned concurrency);
```

`label` is for you. The scheduler does not branch on it. Null on failure.

### suspenders_get_global_queue()

```c
suspenders_queue_t* suspenders_get_global_queue(suspenders_qos_t qos);
```

The process-wide queue at that QoS. Created in `init`, freed in
`shutdown`. Do not destroy it yourself. It will not take the hint well.

### suspenders_queue_async()

```c
int suspenders_queue_async(suspenders_queue_t *q, void (*fn)(void*), void *arg);
```

Submit and return.

### suspenders_queue_sync()

```c
int suspenders_queue_sync(suspenders_queue_t *q, void (*fn)(void*), void *arg);
```

Submit and wait until `fn` returns. Coroutine context only.

### suspenders_queue_after()

```c
int suspenders_queue_after(suspenders_queue_t *q, uint64_t delay_ns,
                           void (*fn)(void*), void *arg);
```

The task joins the queue when the delay has passed. It does not jump the
tasks already there.

### suspenders_queue_barrier_async()

```c
int suspenders_queue_barrier_async(suspenders_queue_t *q,
                                   void (*fn)(void*), void *arg);
```

Wait until earlier tasks finish, run alone, then let later tasks start.
On a serial queue this is `async`. Everything was already alone.

### suspenders_queue_destroy()

```c
void suspenders_queue_destroy(suspenders_queue_t *q);
```

Inside a coroutine, wait until pending tasks finish, then free. Outside
one, the queue is reaped at `shutdown`.

### suspenders_queue_label()

```c
const char* suspenders_queue_label(const suspenders_queue_t *q);
```

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
    suspenders_queue_destroy(q);
}
```

## Pool

A pool is a concurrent queue with a fixed number of drainers and a shorter
spelling.

### suspenders_pool_create()

```c
suspenders_pool_t* suspenders_pool_create(unsigned nworkers, suspenders_qos_t qos);
```

### suspenders_pool_submit()

```c
void suspenders_pool_submit(suspenders_pool_t *pool, void (*fn)(void*), void *arg);
```

If every worker is busy the task waits. It is not dropped. Dropping work
silently is a policy, and it is not this one.

### suspenders_pool_destroy()

```c
void suspenders_pool_destroy(suspenders_pool_t *pool);
```

Pending tasks run, then the pool goes away.

## Hoses

A hose is a connection, or a listening socket, addressed by a URI.
`read` and `write` look like the POSIX calls. They park the coroutine.
On Linux with kernel 5.19 or newer the worker submits to io_uring.
Otherwise it is `poll`, kqueue, or WSAPoll, depending on the host.

Schemes: `tcp://`, `udp://`, `quic://`, `unix://`, `tty://`. `quic://` is
compiled in when OpenSSL 3 is present. It is QUIC v1, one bidirectional
stream, TLS 1.3. CertificateVerify is checked against the certificate's
key. The name is not checked, and there is no trust store. You wanted a
byte stream. You got a byte stream that had a handshake. See the README
for why it is not a general QUIC endpoint.

### suspenders_hose_init()

```c
void suspenders_hose_init(suspenders_hose_t *d, struct buf *b);
```

A hose is a value. `b` is an optional buffer, or null.

### suspenders_hose_dial()

```c
bool suspenders_hose_dial(suspenders_hose_t *d, const char *uri);
```

Connect. Parks until the connection exists or fails. False sets
`suspenders_errno`.

### suspenders_hose_dial_dl()

```c
bool suspenders_hose_dial_dl(suspenders_hose_t *d, const char *uri,
                             uint64_t deadline_ns);
```

### suspenders_hose_listen()

```c
bool suspenders_hose_listen(suspenders_hose_t *d, const char *uri);
```

Bind and listen. `accept` is how clients appear.

### suspenders_hose_accept()

```c
bool suspenders_hose_accept(suspenders_hose_t *d, suspenders_hose_t *client);
```

Parks until a client connects. On success `client` is ready for I/O.

### suspenders_hose_accept_dl()

```c
bool suspenders_hose_accept_dl(suspenders_hose_t *d, suspenders_hose_t *client,
                               uint64_t deadline_ns);
```

### suspenders_hose_read()

```c
ssize_t suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len);
```

Bytes read, 0 on EOF, -1 on error.

### suspenders_hose_read_dl()

```c
ssize_t suspenders_hose_read_dl(suspenders_hose_t *d, void *dest, size_t len,
                                uint64_t deadline_ns);
```

### suspenders_hose_write()

```c
ssize_t suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len);
```

Bytes written, or -1.

### suspenders_hose_write_dl()

```c
ssize_t suspenders_hose_write_dl(suspenders_hose_t *d, const void *src, size_t len,
                                 uint64_t deadline_ns);
```

### suspenders_hose_readv()

```c
ssize_t suspenders_hose_readv(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
```

### suspenders_hose_writev()

```c
ssize_t suspenders_hose_writev(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
```

### suspenders_hose_recvfrom()

```c
ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len,
                                 struct sockaddr *addr, socklen_t *addrlen);
```

Datagram receive. The peer address is written through `addr` when you pass
one. UDP.

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

| `how` | Constant | Effect |
|---|---|---|
| 0 | `SUSPENDERS_SHUT_RD` | No further reads |
| 1 | `SUSPENDERS_SHUT_WR` | No further writes |
| 2 | `SUSPENDERS_SHUT_RDWR` | Both |

### suspenders_hose_set_option()

```c
int suspenders_hose_set_option(suspenders_hose_t *d, int level, int optname,
                               const void *optval, socklen_t optlen);
```

`setsockopt`, with the coroutine still in the room.

### suspenders_hose_peername()

```c
int suspenders_hose_peername(suspenders_hose_t *d, struct sockaddr *addr,
                             socklen_t *addrlen);
```

### suspenders_hose_sockname()

```c
int suspenders_hose_sockname(suspenders_hose_t *d, struct sockaddr *addr,
                             socklen_t *addrlen);
```

### suspenders_hose_close()

```c
void suspenders_hose_close(suspenders_hose_t *d);
```

Closes the fd. On a QUIC connection that does not own the listening
socket, this ends the connection and leaves the listener's fd alone.

```c
void echo_handler(void *arg) {
    suspenders_hose_t *client = arg;
    char buf[4096];
    ssize_t n;
    while ((n = suspenders_hose_read(client, buf, sizeof(buf))) > 0)
        suspenders_hose_write(client, buf, (size_t)n);
    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
}

void server(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);
    suspenders_hose_listen(&listener, "tcp://0.0.0.0:12345");
    for (;;) {
        suspenders_hose_t *client = memento_thread_heap_alloc(
            memento_thread_heap_get(), sizeof(*client));
        if (suspenders_hose_accept(&listener, client))
            suspenders_go(echo_handler, client);
        else
            memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(*client));
    }
}
```

The handler is straight-line code. The suspend is inside `read`. When the
bytes arrive, the handler continues on the next line, with `buf` still
`buf`. That is what the stack was for.

## Transports

The built-in schemes are registered in `init`. Registering the same scheme
again is a no-op, so `init` can be called more than once in a process
without filling the table. The table holds 16. That is enough, and it is
also a number you can hit if you register new ones in a loop, which you
should not.

The scheme string includes the `://`. `tcp://`, not `tcp`. The parser is
not in the mood to guess.

### suspenders_transport_register()

```c
bool suspenders_transport_register(const suspenders_transport_ops_t *ops);
```

### suspenders_transport_find()

```c
const suspenders_transport_ops_t* suspenders_transport_find(const char *scheme);
```

Null if nobody registered it.

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

Null function pointers are legal. The hose layer turns them into
`SUSPENDERS_NOTFOUND` or -1. Implement what the scheme can actually do.

## Ticket lock

A FIFO spinlock. Channels use it for the short critical section around the
ring. You may use it for the same kind of section. If the section is long
enough to read this paragraph while you hold the lock, use a mutex. The
mutex parks. The ticket lock does not, and the worker is the one spinning.

### suspenders_ticket_init()

```c
static inline void suspenders_ticket_init(suspenders_ticket_lock_t *l);
```

### suspenders_ticket_lock()

```c
static inline void suspenders_ticket_lock(suspenders_ticket_lock_t *l);
```

### suspenders_ticket_unlock()

```c
static inline void suspenders_ticket_unlock(suspenders_ticket_lock_t *l);
```

## Errors, again

```c
extern SUSPENDERS_TLS int suspenders_errno;
const char* suspenders_strerror(int err);
```

## Types

| Type | What it is |
|---|---|
| `suspenders_cr_t` | Coroutine control block, cache-line aligned |
| `suspenders_ctx_t` | Registers the switch saves |
| `suspenders_chan_t` | Rendezvous or ring |
| `suspenders_chan_op_t` | One arm of a select |
| `suspenders_hose_t` | Connection or listener |
| `suspenders_mutex_t` | Mutex, a value |
| `suspenders_rwlock_t` | Read-write lock, a value |
| `suspenders_cond_t` | Condition variable, a value |
| `suspenders_waitgroup_t` | Counter, a value |
| `suspenders_timer_t` | One-shot or repeating |
| `suspenders_cleanup_t` | Cleanup node, you allocate it |
| `suspenders_ticket_lock_t` | FIFO spinlock, a value |
| `suspenders_queue_t` | Task queue |
| `suspenders_pool_t` | Fixed set of drainers |
| `suspenders_transport_ops_t` | Scheme vtable |
| `suspenders_qos_t` | One of the four levels |
| `suspenders_state_t` | Ready, running, suspended, done |
