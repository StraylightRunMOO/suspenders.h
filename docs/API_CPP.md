# Suspenders C++ API

Reference for `suspenders.hpp`, version 1.2.0. The C calls are in
[API.md](API.md). This header wraps them. Every type has `native()` for
the moment you need the C object back. That moment arrives. It is not a
failure of character.

The wrappers are move-only. Copying a hose would be two owners of one fd,
and the destructor would then close it twice, which the kernel notices.

## Getting started

One `.cpp` file defines both implementation macros. Memento first.
`suspenders.hpp` includes `suspenders.h`.

```cpp
#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
```

```cpp
#include <cstdio>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

int main() {
    suspenders::Context ctx;
    suspenders::spawn([] {
        std::printf("Hello from a coroutine.\n");
    });
    ctx.run();
}
```

`Context` calls `init` in the constructor and `shutdown` in the destructor.
`spawn` returns a `Task`. `run` does not return until the work is gone.
Everything is in namespace `suspenders`.

## What the wrapper adds

Error codes, deadlines, priority, cancellation, and allocation are the C
rules. Read those notes once. The C++ layer adds four habits:

- **RAII.** `Context`, `Hose`, `Channel<T>`, `Timer`, `Queue`, `Pool` clean
  up in their destructors. You can still call `close` or `cancel`. You do
  not have to, unless the lifetime you want is shorter than the scope.
- **Callables.** `spawn`, `Queue::async`, `Pool::submit`, and `Timer` take
  any callable. The wrapper copies it onto the heap and deletes it after
  the call. A raw function pointer still exists, and it allocates nothing.
- **`std::optional` for the receive you do not want to spell.**
  `Channel<T>::recv()` with no argument is empty on failure.
- **Ints for the failures that are normal.** Closed, timeout, and cancel
  do not throw. Methods named `*_status`, and methods that return `int`,
  give you the `SUSPENDERS_*` code. Constructors throw `std::runtime_error`
  when the coroutine or the object cannot be created. That is the failure
  you cannot report with a return code, because you do not have the object.

## Context

One runtime. Not copyable, not movable. There is one scheduler. A second
`Context` is a second `init`, and `init` on a live runtime is not a plan.

```cpp
class Context {
public:
    explicit Context(unsigned num_workers = 0, unsigned queue_hint = 256);
    ~Context();   // suspenders_shutdown()
    void run();   // suspenders_run()
};
```

`num_workers == 0` is one worker. The zero is historical. It is also the
default, so most programs never say the number.

```cpp
int main() {
    suspenders::Context ctx;
    suspenders::spawn([] { /* ... */ });
    ctx.run();
}
```

## Spawning

### suspenders::spawn()

```cpp
template<typename F>
Task spawn(F&& f, QoS qos = QoS::Normal);

Task spawn(void (*func)(void*), void* arg = nullptr, QoS qos = QoS::Normal);
```

The first copies `f` onto the heap and deletes it when the coroutine
finishes. The second does not. Throws `std::runtime_error` if the
coroutine cannot be created.

```cpp
auto task = suspenders::spawn([&] { do_work(); });
suspenders::spawn([] { latency_critical_work(); }, suspenders::QoS::Realtime);
suspenders::spawn(my_func, my_arg);
```

Capturing by reference is legal. The referent has to outlive the
coroutine. The type system will not check. The sanitizer will, later, in
a tone of voice you will not enjoy.

### QoS

```cpp
enum class QoS {
    Realtime,  // SUSPENDERS_QOS_REALTIME
    High,
    Normal,
    Low
};
```

## Task

A pointer with manners. It does not own the coroutine. Destroying a
`Task` does not cancel it. If you wanted that, you wanted `cancel()`.

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

enum class State { Ready, Running, Suspended, Done };
```

```cpp
auto worker = suspenders::spawn([] {
    for (int i = 0; i < 10; i++) {
        std::printf("step %d\n", i);
        suspenders::suspend();
    }
});

while (!worker.done()) {
    worker.resume();
    suspenders::yield();
}
```

## The running coroutine

### suspenders::current_task()

```cpp
[[nodiscard]] Task current_task();
```

### suspenders::yield()

```cpp
void yield();
```

Back of your priority list.

### suspenders::suspend()

```cpp
void suspend();
```

Park until some other `Task::resume`.

### suspenders::sleep_ms()

```cpp
void sleep_ms(int ms);
```

### suspenders::sleep_for()

```cpp
template<typename Rep, typename Period>
void sleep_for(std::chrono::duration<Rep, Period> duration);
```

```cpp
using namespace std::chrono_literals;
suspenders::sleep_for(250ms);
suspenders::sleep_for(2s);
```

### suspenders::now_ns()

```cpp
uint64_t now_ns();
```

Monotonic nanoseconds. Subtract two of them. Do not compare one of them
to a wall clock. They are not having the same day.

## Channel

`T` is trivially copyable. The channel `memcpy`s. Constructors and
destructors of `T` do not run in transit. If that sentence surprises you,
`T` is the wrong type.

`buffer_size == 0` is a rendezvous. A positive size is a ring.

```cpp
template<typename T>
class Channel {
public:
    explicit Channel(size_t buffer_size = 0);
    ~Channel();

    Channel(Channel&& other) noexcept;
    Channel& operator=(Channel&& other) noexcept;

    [[nodiscard]] bool send(const T& value);
    [[nodiscard]] bool recv(T& out);
    [[nodiscard]] std::optional<T> recv();

    [[nodiscard]] int send_status(const T& value);
    [[nodiscard]] int recv_status(T& out);

    [[nodiscard]] int try_send(const T& value);
    [[nodiscard]] int try_recv(T& out);

    [[nodiscard]] int send_dl(const T& value, uint64_t deadline_ns);
    [[nodiscard]] int recv_dl(T& out, uint64_t deadline_ns);

    int close();

    [[nodiscard]] suspenders_chan_op_t recv_op(T& out);
    [[nodiscard]] suspenders_chan_op_t send_op(T& value);
    [[nodiscard]] suspenders_chan_t* native() const;
};
```

`send` and `recv` are `true` on success, `false` on closed or canceled.
The `optional` receive is empty on failure. Use `*_status` when you need
to know which failure. `try_*` returns `OK`, `FULL` or `EMPTY`, or
`CLOSED`. `recv_op` and `send_op` are how you build a `select`.

```cpp
suspenders::Channel<uint64_t> ch;
suspenders::spawn([&ch] { ch.send(42); });
suspenders::spawn([&ch] {
    if (auto val = ch.recv())
        std::printf("got %llu\n", (unsigned long long)*val);
});
```

## Select

```cpp
int select(std::initializer_list<suspenders_chan_op_t> cases,
           uint64_t deadline_ns = 0);
```

Index of the case that ran, or a negative code (`TIMEDOUT`, `CANCELED`).
`suspenders_errno` on the winning case is `OK` or `CLOSED`. Among several
ready cases, one is chosen at random. Do not write a test that requires a
particular winner. You will learn about the generator, not about your bug.

```cpp
suspenders::Channel<int> ch_a, ch_b;
int val_a, val_b;
int idx = suspenders::select({
    ch_a.recv_op(val_a),
    ch_b.recv_op(val_b),
});
if (idx == 0) std::printf("A %d\n", val_a);
else if (idx == 1) std::printf("B %d\n", val_b);
```

## Hose

Move-only. The destructor closes. Move it into the coroutine that will
read it. Do not copy it. There is no copy.

```cpp
class Hose {
public:
    Hose();
    explicit Hose(struct buf* buffer);
    ~Hose();

    Hose(Hose&& other) noexcept;
    Hose& operator=(Hose&& other) noexcept;

    [[nodiscard]] bool dial(std::string_view uri);
    [[nodiscard]] bool listen(std::string_view uri);
    [[nodiscard]] bool accept(Hose& client);
    [[nodiscard]] bool accept_dl(Hose& client, uint64_t deadline_ns);

    [[nodiscard]] ssize_t read(void* dest, size_t len);
    [[nodiscard]] ssize_t write(const void* src, size_t len);
    [[nodiscard]] ssize_t read_dl(void* dest, size_t len, uint64_t deadline_ns);
    [[nodiscard]] ssize_t write_dl(const void* src, size_t len, uint64_t deadline_ns);
    [[nodiscard]] ssize_t readv(const struct iovec* iov, int iovcnt);
    [[nodiscard]] ssize_t writev(const struct iovec* iov, int iovcnt);

    template<typename T>
    [[nodiscard]] ssize_t write(const T& obj);   // trivially copyable
    [[nodiscard]] ssize_t write(std::string_view sv);

    [[nodiscard]] ssize_t recvfrom(void* dest, size_t len,
                                   struct sockaddr* addr, socklen_t* addrlen);
    [[nodiscard]] ssize_t sendto(const void* src, size_t len,
                                 const struct sockaddr* addr, socklen_t addrlen);

    void close();
    int shutdown(int how);   // SUSPENDERS_SHUT_RD / WR / RDWR
    int set_option(int level, int optname, const void* optval, socklen_t optlen);
    int peername(struct sockaddr* addr, socklen_t* addrlen);
    int sockname(struct sockaddr* addr, socklen_t* addrlen);

    [[nodiscard]] suspenders_sock_t fd() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] suspenders_hose_t* native();
};
```

`dial`, `listen`, and `accept` park the caller. Read returns a byte count,
0 on EOF, -1 on error. Schemes are the C schemes: `tcp://`, `udp://`,
`quic://`, `unix://`, `tty://`.

```cpp
static void handle_client(suspenders::Hose client) {
    char buf[4096];
    ssize_t n;
    while ((n = client.read(buf, sizeof(buf))) > 0) {
        if (client.write(buf, static_cast<size_t>(n)) < 0) break;
    }
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

The move into the lambda is the ownership transfer. When the handler
returns, the hose closes. There is no `delete`. There is also no second
chance if you moved from `client` and then used it. Moved-from is empty.
Treat it that way.

## Timer

Destructor cancels. A repeating timer keeps `run` alive until you cancel
it or destroy it. Forgetting is how `main` waits forever for a clock.

```cpp
class Timer {
public:
    Timer();
    Timer(int ms, bool repeat, std::function<void()> cb);
    ~Timer();

    Timer(Timer&& other) noexcept;
    Timer& operator=(Timer&& other) noexcept;

    void cancel();
    [[nodiscard]] bool valid() const;
    explicit operator bool() const;
};
```

```cpp
suspenders::Timer oneshot(500, false, [] { std::printf("fired\n"); });
suspenders::Timer ticker(250, true, [] { std::printf("tick\n"); });
ticker.cancel();
```

The callback runs as a coroutine. It may call the library. Keep the
resulting nest shallow. The stack is 1 MB, not a philosophy.

## Queue

A list of functions, drained by coroutines. `concurrency == 1` is a FIFO.
The destructor waits for the drain if you destroy it from a coroutine.

`DispatchQueue` is an old name for `Queue`. It still compiles. New code
can say `Queue`.

```cpp
class Queue {
public:
    Queue();
    explicit Queue(const char* label, QoS qos = QoS::Normal,
                   unsigned concurrency = 1);
    ~Queue();

    static Queue global(QoS qos = QoS::Normal);  // does not own the queue

    Queue(Queue&& other) noexcept;
    Queue& operator=(Queue&& other) noexcept;

    template<typename F> int async(F&& f);
    template<typename F> int sync(F&& f);    // coroutine context only
    template<typename F> int after(uint64_t delay_ns, F&& f);
    template<typename F> int barrier_async(F&& f);

    [[nodiscard]] const char* label() const;
    [[nodiscard]] bool valid() const;
    explicit operator bool() const;
    [[nodiscard]] suspenders_queue_t* native() const;
};
```

`async` returns. `sync` waits until the callable returns. `after` joins
the queue when the delay has passed. `barrier_async` runs with the queue
to itself. `global` is the process-wide queue. Do not destroy it. You do
not own it, and `shutdown` does.

```cpp
suspenders::spawn([&] {
    suspenders::Queue queue("work", suspenders::QoS::Normal);
    for (int i = 0; i < 5; i++)
        queue.async([i] { std::printf("task %d\n", i); });
    queue.barrier_async([] { std::printf("barrier\n"); });
    suspenders::sleep_ms(100);
});
```

## Pool

A fixed number of drainers and a queue in front of them.

```cpp
class Pool {
public:
    Pool();
    explicit Pool(unsigned nworkers, QoS qos = QoS::Normal);
    ~Pool();

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
for (int i = 0; i < 100; i++)
    pool.submit([i] { process(i); });
```

Destruction drains. Pending work runs. It is not discarded. If you needed
discard, you needed a different object, and you should say so in the type.

## Mutex

FIFO. The waiter parks. The holder is boosted to the waiter's QoS for the
duration of the wait.

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

## LockGuard

Locks on construction, unlocks on destruction. The only RAII type in this
header that is boring, which is the compliment.

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
    shared_data++;
}
```

## RWLock

Readers share. A waiting writer blocks new readers, so a writer is not
stuck behind an infinite polite queue of readers.

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

## Cond

`wait` releases the mutex, parks, and re-acquires it. Loop on the
predicate. A signal means "look again", not "it is true now".

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

mtx.lock();
while (!ready)
    cond.wait(mtx);
mtx.unlock();

mtx.lock();
ready = true;
cond.signal();
mtx.unlock();
```

## WaitGroup

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

`add` before the work exists. `done` from the work. `wait` until the
counter is zero. Adding after you have started waiting is how the counter
and the waiters disagree about the plot.

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
```

## CleanupGuard

Runs the callable when the scope ends, and also if the coroutine is
canceled while the guard is alive. `release` disarms it. A disarmed guard
is a resource you have promised to free yourself. The promise is not
checked.

```cpp
class CleanupGuard {
public:
    template<typename F>
    explicit CleanupGuard(F&& f);
    ~CleanupGuard();
    void release();
};
```

```cpp
{
    auto* resource = acquire();
    suspenders::CleanupGuard guard([resource] { release(resource); });
    do_work_with(resource);
}
```

## Buffer

The C `struct buf`, with a destructor. Memento-backed. Move-only.
`append` returns false when the allocator says no. Check it. A buffer
that silently stopped growing is a short message with a long future.

```cpp
class Buffer {
public:
    Buffer();
    explicit Buffer(std::string_view initial);
    explicit Buffer(const void* data, size_t len);
    ~Buffer();

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    [[nodiscard]] bool append(std::string_view sv);
    [[nodiscard]] bool append(const void* data, size_t len);
    [[nodiscard]] bool append(const std::vector<uint8_t>& vec);
    [[nodiscard]] bool push_back(char c);
    void clear();

    [[nodiscard]] char* data();
    [[nodiscard]] const char* data() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::string_view view() const;
    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::vector<uint8_t> bytes() const;

    [[nodiscard]] char* begin();
    [[nodiscard]] char* end();
    [[nodiscard]] const char* begin() const;
    [[nodiscard]] const char* end() const;

    [[nodiscard]] struct buf* native();
};
```

## TicketLock

FIFO spinlock, plus a `Guard`. The critical section is a few
instructions. A coroutine mutex parks. This does not. If you can do
useful work while holding it, you are holding the wrong lock.

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
    };
};
```

## Dropping to C

```cpp
suspenders::Channel<int> ch;
suspenders_chan_t* raw = ch.native();

suspenders::Hose hose;
suspenders_hose_t* raw_hose = hose.native();
```

`suspenders.hpp` includes `suspenders.h`. The C API is in the same
translation unit. `native()` does not extend the lifetime of the C++
object. When the C++ object dies, the C pointer dies with it, whether or
not you kept a copy. You kept a copy. That was the bug.
