# Suspenders C++ API Reference

*libsuspenders 1.0 — the C++17 wrapper: RAII, lambdas, type-safe channels.*

This is the reference for `suspenders.hpp`, the C++ facade. It wraps the
entire [C API](API.md) in move-only RAII types, replaces `void*` callbacks
with lambdas, and gives you `Channel<T>` instead of raw byte shuffling. The
C API is always available underneath — every wrapper exposes a `.native()`
method when you need to drop down.

## Getting started

Same deal as the C header: define `SUSPENDERS_IMPLEMENTATION` in exactly one
`.cpp` file, include `suspenders.hpp` (which pulls in `suspenders.h` for
you), and Memento comes first:

```cpp
#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
```

Here is the smallest useful program:

```cpp
#include <cstdio>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

int main() {
    suspenders::Context ctx;

    suspenders::spawn([] {
        std::printf("Hello from a coroutine!\n");
    });

    ctx.run();
}
```

`Context` calls `suspenders_init` in its constructor and `suspenders_shutdown`
in its destructor. `spawn` takes a lambda (or any callable) and returns a
`Task` handle. `ctx.run()` spins the scheduler.

Everything lives in the `suspenders` namespace. Types are movable but not
copyable — unique ownership, no surprise double-frees.

## Programming notes

Error codes, deadlines, QoS levels, cancellation, and memory management
all work identically to the C API. See the [C API programming notes](API.md#programming-notes)
for the full story.

The C++ wrapper adds a few conventions:

- **RAII everywhere.** `Context`, `Hose`, `Channel<T>`, `Timer`, `Queue`,
  `Pool` — they all clean up in their destructors. You rarely call a
  `destroy` or `close` method explicitly.

- **Lambdas instead of function pointers.** `spawn`, `Queue::async`,
  `Pool::submit`, and `Timer` all accept any callable. The wrapper
  heap-allocates a copy and deletes it after the call.

- **`std::optional` for fallible receives.** `Channel<T>::recv()` (no
  argument) returns `std::optional<T>` — empty on failure.

- **Status codes stay as ints.** The wrapper doesn't throw for routine
  errors (closed channels, timeouts). Methods named `_status` or returning
  `int` give you the raw `SUSPENDERS_*` code. Constructors throw
  `std::runtime_error` only when allocation fails — that's the one thing
  you can't reasonably recover from in-line.

---

## Table of contents

- [Context](#context)
- [Spawning coroutines](#spawning-coroutines)
- [Task](#task)
- [Fiber utilities](#fiber-utilities)
- [Channel\<T\>](#channel)
- [Select](#select)
- [Hose](#hose)
- [Timer](#timer)
- [Queue](#queue)
- [Pool](#pool)
- [Mutex](#mutex)
- [LockGuard](#lockguard)
- [RWLock](#rwlock)
- [Cond](#cond)
- [WaitGroup](#waitgroup)
- [CleanupGuard](#cleanupguard)
- [Buffer](#buffer)
- [TicketLock](#ticketlock)

---

## Context

RAII wrapper for the runtime lifecycle. Create one on the stack in `main`;
it inits the scheduler and shuts it down when it goes out of scope.

```cpp
class Context {
public:
    explicit Context(unsigned num_workers = 0, unsigned queue_hint = 256);
    ~Context();  // calls suspenders_shutdown()

    void run();  // calls suspenders_run()
};
```

Non-copyable, non-movable — there's only one runtime.

**Example:**

```cpp
int main() {
    suspenders::Context ctx;
    suspenders::spawn([] { /* ... */ });
    ctx.run();
    // shutdown happens here
}
```

---

## Spawning coroutines

### suspenders::spawn()

```cpp
template<typename F>
Task spawn(F&& f, QoS qos = QoS::Normal);

Task spawn(void (*func)(void*), void* arg = nullptr, QoS qos = QoS::Normal);
```

Spawn a coroutine. The first overload takes any callable — lambda, functor,
`std::function` — heap-allocates a copy, and deletes it when the coroutine
finishes. The second overload takes a raw function pointer with no allocation
overhead.

**Throws** `std::runtime_error` if the coroutine can't be created.

**Returns** a `Task` handle.

```cpp
// Lambda (the common case)
auto task = suspenders::spawn([&] {
    do_work();
});

// With priority
suspenders::spawn([] { latency_critical_work(); }, suspenders::QoS::Realtime);

// Raw function pointer (zero overhead)
suspenders::spawn(my_func, my_arg);
```

### QoS

```cpp
enum class QoS {
    Realtime,  // SUSPENDERS_QOS_REALTIME
    High,      // SUSPENDERS_QOS_HIGH
    Normal,    // SUSPENDERS_QOS_NORMAL
    Low        // SUSPENDERS_QOS_LOW
};
```

---

## Task

A non-owning handle to a coroutine. Lightweight — it's just a pointer.

```cpp
class Task {
public:
    Task();
    explicit Task(suspenders_cr_t* cr);

    void resume();
    void boost(QoS new_qos);
    void cancel();

    [[nodiscard]] State state() const;
    [[nodiscard]] bool done() const;
    [[nodiscard]] bool valid() const;
    explicit operator bool() const;

    [[nodiscard]] suspenders_cr_t* native() const;
};
```

```cpp
enum class State {
    Ready,
    Running,
    Suspended,
    Done
};
```

**Example:**

```cpp
auto worker = suspenders::spawn([] {
    for (int i = 0; i < 10; i++) {
        std::printf("step %d\n", i);
        suspenders::suspend();
    }
});

// Later, from another coroutine:
while (!worker.done()) {
    worker.resume();
    suspenders::yield();
}
```

---

## Fiber utilities

Free functions that operate on the currently running coroutine.

### suspenders::current_task()

```cpp
[[nodiscard]] Task current_task();
```

Return a `Task` handle to the current coroutine.

### suspenders::yield()

```cpp
void yield();
```

Reschedule. Other coroutines at the same or higher priority run first.

### suspenders::suspend()

```cpp
void suspend();
```

Park until another coroutine calls `resume()` on your `Task`.

### suspenders::sleep_ms()

```cpp
void sleep_ms(int ms);
```

Sleep for `ms` milliseconds. Other coroutines run in the meantime.

### suspenders::sleep_for()

```cpp
template<typename Rep, typename Period>
void sleep_for(std::chrono::duration<Rep, Period> duration);
```

Sleep for a `std::chrono::duration`. Use it with duration literals:

```cpp
using namespace std::chrono_literals;
suspenders::sleep_for(250ms);
suspenders::sleep_for(2s);
```

### suspenders::now_ns()

```cpp
uint64_t now_ns();
```

Monotonic clock in nanoseconds.

---

## Channel

Type-safe channel for passing data between coroutines. `T` must be trivially
copyable — the channel moves bytes with `memcpy`, so no constructors or
destructors run on the data in flight.

```cpp
template<typename T>
class Channel {
public:
    explicit Channel(size_t buffer_size = 0);
    ~Channel();

    // Move-only
    Channel(Channel&& other) noexcept;
    Channel& operator=(Channel&& other) noexcept;
```

### Blocking operations

```cpp
    [[nodiscard]] bool send(const T& value);
    [[nodiscard]] bool recv(T& out);
    [[nodiscard]] std::optional<T> recv();
```

`send` and `recv` return `true` on success, `false` on closed/canceled.
The no-argument `recv()` returns `std::optional<T>` — empty on failure.

### Status-returning operations

```cpp
    [[nodiscard]] int send_status(const T& value);
    [[nodiscard]] int recv_status(T& out);
```

Return the raw `SUSPENDERS_*` code when you need to distinguish closed from
canceled from timed out.

### Non-blocking operations

```cpp
    [[nodiscard]] int try_send(const T& value);
    [[nodiscard]] int try_recv(T& out);
```

Return `SUSPENDERS_OK`, `SUSPENDERS_FULL` / `SUSPENDERS_EMPTY`, or
`SUSPENDERS_CLOSED`.

### Deadline operations

```cpp
    [[nodiscard]] int send_dl(const T& value, uint64_t deadline_ns);
    [[nodiscard]] int recv_dl(T& out, uint64_t deadline_ns);
```

### Close and select helpers

```cpp
    int close();

    [[nodiscard]] suspenders_chan_op_t recv_op(T& out);
    [[nodiscard]] suspenders_chan_op_t send_op(T& value);

    [[nodiscard]] suspenders_chan_t* native() const;
};
```

`recv_op` and `send_op` build case descriptors for `suspenders::select()`.

**Example:**

```cpp
suspenders::Channel<uint64_t> ch;

suspenders::spawn([&ch] {
    ch.send(42);
});

suspenders::spawn([&ch] {
    if (auto val = ch.recv()) {
        std::printf("got %llu\n", (unsigned long long)*val);
    }
});
```

---

## Select

### suspenders::select()

```cpp
int select(std::initializer_list<suspenders_chan_op_t> cases,
           uint64_t deadline_ns = 0);
```

Wait on multiple channel operations at once. Uses the `recv_op` / `send_op`
helpers from `Channel<T>` to build cases.

Returns the index of the ready case, or a negative error code
(`SUSPENDERS_TIMEDOUT`, `SUSPENDERS_CANCELED`). Check `suspenders_errno` for
`SUSPENDERS_OK` vs. `SUSPENDERS_CLOSED` on the winning case.

```cpp
suspenders::Channel<int> ch_a, ch_b;
int val_a, val_b;

int idx = suspenders::select({
    ch_a.recv_op(val_a),
    ch_b.recv_op(val_b),
});

if (idx == 0)
    std::printf("got %d from A\n", val_a);
else if (idx == 1)
    std::printf("got %d from B\n", val_b);
```

---

## Hose

RAII wrapper for async I/O. Move-only. Closes automatically on destruction.

```cpp
class Hose {
public:
    Hose();
    explicit Hose(struct buf* buffer);
    ~Hose();  // closes if valid

    // Move-only
    Hose(Hose&& other) noexcept;
    Hose& operator=(Hose&& other) noexcept;
```

### Connection setup

```cpp
    [[nodiscard]] bool dial(std::string_view uri);
    [[nodiscard]] bool listen(std::string_view uri);
    [[nodiscard]] bool accept(Hose& client);
    [[nodiscard]] bool accept_dl(Hose& client, uint64_t deadline_ns);
```

`dial` connects, `listen` binds, `accept` waits for incoming connections.
All suspend the calling coroutine.

### I/O

```cpp
    [[nodiscard]] ssize_t read(void* dest, size_t len);
    [[nodiscard]] ssize_t write(const void* src, size_t len);
    [[nodiscard]] ssize_t read_dl(void* dest, size_t len, uint64_t deadline_ns);
    [[nodiscard]] ssize_t write_dl(const void* src, size_t len, uint64_t deadline_ns);
    [[nodiscard]] ssize_t readv(const struct iovec* iov, int iovcnt);
    [[nodiscard]] ssize_t writev(const struct iovec* iov, int iovcnt);
```

### Convenience write overloads

```cpp
    template<typename T>
    [[nodiscard]] ssize_t write(const T& obj);  // trivially copyable types

    [[nodiscard]] ssize_t write(std::string_view sv);
```

### Datagram I/O

```cpp
    [[nodiscard]] ssize_t recvfrom(void* dest, size_t len,
                                   struct sockaddr* addr, socklen_t* addrlen);
    [[nodiscard]] ssize_t sendto(const void* src, size_t len,
                                 const struct sockaddr* addr, socklen_t addrlen);
```

### Socket control

```cpp
    void close();
    int shutdown(int how);  // SUSPENDERS_SHUT_RD / WR / RDWR
    int set_option(int level, int optname, const void* optval, socklen_t optlen);
    int peername(struct sockaddr* addr, socklen_t* addrlen);
    int sockname(struct sockaddr* addr, socklen_t* addrlen);
```

### State

```cpp
    [[nodiscard]] suspenders_sock_t fd() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] suspenders_hose_t* native();
};
```

**Example** — TCP echo server:

```cpp
static void handle_client(suspenders::Hose client) {
    char buf[4096];
    ssize_t n;
    while ((n = client.read(buf, sizeof(buf))) > 0) {
        if (client.write(buf, static_cast<size_t>(n)) < 0) break;
    }
    // client closes automatically here
}

int main() {
    suspenders::Context ctx;

    suspenders::spawn([] {
        suspenders::Hose listener;
        listener.listen("tcp://0.0.0.0:12345");

        for (;;) {
            suspenders::Hose client;
            if (listener.accept(client)) {
                suspenders::spawn([c = std::move(client)]() mutable {
                    handle_client(std::move(c));
                });
            }
        }
    }, suspenders::QoS::High);

    ctx.run();
}
```

The move into the lambda is the interesting part. Each client connection
gets its own coroutine with its own `Hose`, which closes automatically when
the coroutine exits. No manual memory management, no `new`/`delete`.

---

## Timer

RAII timer. Cancels and frees automatically on destruction.

```cpp
class Timer {
public:
    Timer();
    Timer(int ms, bool repeat, std::function<void()> cb);
    ~Timer();  // cancels if active

    // Move-only
    Timer(Timer&& other) noexcept;
    Timer& operator=(Timer&& other) noexcept;

    void cancel();
    [[nodiscard]] bool valid() const;
    explicit operator bool() const;
};
```

```cpp
// Fire once after 500ms
suspenders::Timer oneshot(500, false, [] {
    std::printf("fired!\n");
});

// Tick every 250ms
suspenders::Timer ticker(250, true, [] {
    std::printf("tick\n");
});

// Cancel early
ticker.cancel();
```

---

## Queue

libdispatch-style task queue. Serial or concurrent, with async/sync/after/barrier
submission.

```cpp
class Queue {
public:
    Queue();
    explicit Queue(const char* label, QoS qos = QoS::Normal, unsigned concurrency = 1);
    ~Queue();  // waits for drain if inside a coroutine

    static Queue global(QoS qos = QoS::Normal);  // non-owning handle

    // Move-only
    Queue(Queue&& other) noexcept;
    Queue& operator=(Queue&& other) noexcept;
```

### Submission

```cpp
    template<typename F> int async(F&& f);
    template<typename F> int sync(F&& f);     // coroutine context only
    template<typename F> int after(uint64_t delay_ns, F&& f);
    template<typename F> int barrier_async(F&& f);
```

All take any callable. `async` is fire-and-forget. `sync` blocks the
calling coroutine until the task completes. `after` delays submission.
`barrier_async` runs alone — nothing before it is still running, nothing
after it starts until it finishes.

### Accessors

```cpp
    [[nodiscard]] const char* label() const;
    [[nodiscard]] bool valid() const;
    explicit operator bool() const;
    [[nodiscard]] suspenders_queue_t* native() const;
};
```

`DispatchQueue` is a deprecated alias for `Queue`.

**Example:**

```cpp
suspenders::spawn([&] {
    suspenders::Queue queue("work", suspenders::QoS::Normal);

    for (int i = 0; i < 5; i++) {
        queue.async([i] {
            std::printf("task %d\n", i);
        });
    }

    queue.barrier_async([] {
        std::printf("barrier reached\n");
    });

    suspenders::sleep_ms(100);
    // queue drains and destroys here
});
```

---

## Pool

A pool of N worker coroutines consuming from a shared queue. For when you
want a fixed-size workforce.

```cpp
class Pool {
public:
    Pool();
    explicit Pool(unsigned nworkers, QoS qos = QoS::Normal);
    ~Pool();

    // Move-only
    Pool(Pool&& other) noexcept;
    Pool& operator=(Pool&& other) noexcept;

    template<typename F>
    void submit(F&& f);

    [[nodiscard]] bool valid() const;
    explicit operator bool() const;
};
```

```cpp
suspenders::Pool pool(4, suspenders::QoS::Normal);

for (int i = 0; i < 100; i++) {
    pool.submit([i] {
        process(i);
    });
}
// pool drains on destruction
```

---

## Mutex

Coroutine-aware mutex with FIFO handoff.

```cpp
class Mutex {
public:
    Mutex();

    int lock();
    int lock_dl(uint64_t deadline_ns);
    [[nodiscard]] int try_lock();
    int unlock();

    [[nodiscard]] suspenders_mutex_t* native();
};
```

---

## LockGuard

Scoped lock for `suspenders::Mutex`. Acquires on construction, releases on
destruction.

```cpp
class LockGuard {
public:
    explicit LockGuard(Mutex& m);
    ~LockGuard();
};
```

```cpp
suspenders::Mutex mtx;

void safe_update() {
    suspenders::LockGuard guard(mtx);
    // mutex held until guard goes out of scope
    shared_data++;
}
```

---

## RWLock

Coroutine-aware read-write lock.

```cpp
class RWLock {
public:
    RWLock();

    int rdlock();
    int rdlock_dl(uint64_t deadline_ns);
    [[nodiscard]] int try_rdlock();

    int wrlock();
    int wrlock_dl(uint64_t deadline_ns);
    [[nodiscard]] int try_wrlock();

    int unlock();

    [[nodiscard]] suspenders_rwlock_t* native();
};
```

---

## Cond

Coroutine-aware condition variable.

```cpp
class Cond {
public:
    Cond();

    int wait(Mutex& m);
    int wait_dl(Mutex& m, uint64_t deadline_ns);
    int signal();
    int broadcast();

    [[nodiscard]] suspenders_cond_t* native();
};
```

```cpp
suspenders::Mutex mtx;
suspenders::Cond cond;
bool ready = false;

// Waiter
mtx.lock();
while (!ready)
    cond.wait(mtx);
mtx.unlock();

// Signaler
mtx.lock();
ready = true;
cond.signal();
mtx.unlock();
```

---

## WaitGroup

Wait for a batch of coroutines to finish.

```cpp
class WaitGroup {
public:
    WaitGroup();

    int add(int delta);
    int done();
    int wait();
    int wait_dl(uint64_t deadline_ns);

    [[nodiscard]] suspenders_waitgroup_t* native();
};
```

```cpp
suspenders::WaitGroup wg;
wg.add(10);

for (int i = 0; i < 10; i++) {
    suspenders::spawn([&wg, i] {
        process(i);
        wg.done();
    });
}

wg.wait();
std::printf("all done\n");
```

---

## CleanupGuard

RAII cleanup handler. Runs at scope exit, or if the coroutine is canceled
while the guard is alive. Call `release()` to disarm.

```cpp
class CleanupGuard {
public:
    template<typename F>
    explicit CleanupGuard(F&& f);
    ~CleanupGuard();

    void release();  // disarm — never runs
};
```

```cpp
{
    auto* resource = acquire();
    suspenders::CleanupGuard guard([resource] {
        release(resource);
    });

    // If the coroutine is canceled here, release() still runs.
    do_work_with(resource);
}
// release() runs here on normal exit
```

---

## Buffer

RAII wrapper for the internal dynamic buffer (`struct buf`). Memento-backed,
move-only.

```cpp
class Buffer {
public:
    Buffer();
    explicit Buffer(std::string_view initial);
    explicit Buffer(const void* data, size_t len);
    ~Buffer();

    // Move-only
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    // Append
    [[nodiscard]] bool append(std::string_view sv);
    [[nodiscard]] bool append(const void* data, size_t len);
    [[nodiscard]] bool append(const std::vector<uint8_t>& vec);
    [[nodiscard]] bool push_back(char c);

    void clear();

    // Accessors
    [[nodiscard]] char* data();
    [[nodiscard]] const char* data() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] std::string_view view() const;
    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::vector<uint8_t> bytes() const;

    // Iterators
    [[nodiscard]] char* begin();
    [[nodiscard]] char* end();
    [[nodiscard]] const char* begin() const;
    [[nodiscard]] const char* end() const;

    [[nodiscard]] struct buf* native();
};
```

---

## TicketLock

FIFO spinlock with a scoped `Guard`.

```cpp
class TicketLock {
public:
    TicketLock();
    void lock();
    void unlock();

    class Guard {
    public:
        explicit Guard(TicketLock& lock);
        ~Guard();
        // Move-only
    };
};
```

---

## Dropping to C

Every wrapper type has a `.native()` method that returns a pointer to the
underlying C type. Use it when you need the C API directly, or when passing
handles to C code:

```cpp
suspenders::Channel<int> ch;
suspenders_chan_t* raw = ch.native();

suspenders::Hose hose;
suspenders_hose_t* raw_hose = hose.native();
```

The C API is always available — `suspenders.hpp` includes `suspenders.h`.
