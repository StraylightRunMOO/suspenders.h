# Suspenders (libsuspenders)

A high-performance C library providing stackful coroutines (fibers) with extremely fast context switching (8-12ns target) and Linux io_uring integration for asynchronous I/O.

## Overview

Suspenders delivers lightweight, cooperative multitasking for Linux applications. It uses hand-rolled assembly for context switches on x86_64 and aarch64, achieving nanosecond-level context switch latency while integrating seamlessly with Linux's io_uring for high-performance async I/O operations.

**Key Features:**

- **Ultra-fast context switching**: Hand-rolled assembly for 8-12ns context switches
- **Stackful coroutines**: Full stack per coroutine (1MB default) enables natural blocking-style code
- **io_uring integration**: Native async I/O with automatic coroutine suspension
- **Zero-copy channels**: Rendezvous-style communication between coroutines
- **QoS scheduling**: Four priority levels for workload management
- **Zero-allocation hot paths**: Thread-local memory pools via Memento allocator
- **Single-header library**: Easy integration with header-only pattern

## Requirements

- Linux x86_64 or aarch64
- GCC or Clang with GNU C11 support
- liburing 2.0 or later
- CMake 3.16+ (for building tests and benchmarks)

## Building

### Quick Build

```bash
./build.sh
```

### Manual Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build Outputs

| Target | Binary | Description |
|--------|--------|-------------|
| `suspenders-demo` | `./build/suspenders-demo` | Simple coroutine demonstration |
| `suspenders-tests` | `./build/suspenders-tests` | Unit test suite |
| `suspenders-benchmark` | `./build/suspenders-benchmark` | Context switch performance benchmark |

### Running Tests

```bash
cd build
make test
# or
./suspenders-tests
```

## Quick Start

Suspenders uses a header-only pattern with a two-step inclusion:

```c
#define MEMENTO_IMPLEMENTATION
#include "third_party/memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "include/suspenders.h"

void worker(void *arg) {
    int *val = (int*)arg;
    (*val)++;
    suspenders_yield();
    (*val)++;
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);  // 0 workers (single-threaded), 256 io_uring entries
    
    int counter = 0;
    suspenders_spawn(worker, &counter, SUSPENDERS_QOS_NORMAL);
    
    suspenders_run();  // Blocks until all coroutines complete
    suspenders_shutdown();
    
    return counter == 2 ? 0 : 1;
}
```

## API Reference

### Initialization and Lifecycle

```c
void suspenders_init(unsigned num_workers, unsigned io_uring_entries);
void suspenders_run(void);
void suspenders_shutdown(void);
```

- `suspenders_init()`: Initialize the scheduler with specified worker threads and io_uring queue depth
- `suspenders_run()`: Start the scheduler event loop (blocks until all coroutines complete)
- `suspenders_shutdown()`: Clean up resources and shut down the scheduler

### Coroutine Management

```c
suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos);
void suspenders_yield(void);
void suspenders_suspend(void);
void suspenders_resume(suspenders_cr_t *cr);
void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos);
```

**QoS Levels:**

- `SUSPENDERS_QOS_REALTIME` (0) - Highest priority, reserved for critical work
- `SUSPENDERS_QOS_HIGH` (1) - Above normal priority
- `SUSPENDERS_QOS_NORMAL` (2) - Default priority
- `SUSPENDERS_QOS_LOW` (3) - Background work

### Channels (Zero-Copy Communication)

```c
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
bool suspenders_chan_send(suspenders_chan_t *ch, void *val);
bool suspenders_chan_recv(suspenders_chan_t *ch, void *out);
```

Channels provide rendezvous-style communication between coroutines. The sender and receiver must meet for the transfer to complete.

### Async I/O (Hoses)

```c
void suspenders_hose_init(suspenders_hose_t *d, struct io_uring *ring, struct buf *b);
bool suspenders_hose_dial(suspenders_hose_t *d, const char *uri);      // tcp://host:port
bool suspenders_hose_listen(suspenders_hose_t *d, const char *uri);    // tcp://0.0.0.0:port
bool suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len);
bool suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len);
void suspenders_hose_close(suspenders_hose_t *d);
```

Hose operations automatically suspend the calling coroutine until the I/O completes, allowing other coroutines to run.

### Buffer Management

```c
struct buf { char *data; size_t len; size_t cap; };
bool buf_append(struct buf *buf, const char *data, ssize_t len);
bool buf_append_byte(struct buf *buf, char ch);
void buf_clear(struct buf *buf);
```

Buffers are backed by Memento arenas for zero-allocation growth in hot paths.

## Examples

See the `examples/` directory for complete working examples:

- `examples/tcp_echo.c` - Simple TCP echo server demonstrating async I/O
- `examples/tcp_pingpong.c` - TCP client/server PING/PONG demonstration

### Basic Coroutine Spawn

```c
#include <stdio.h>
#define MEMENTO_IMPLEMENTATION
#include "third_party/memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "include/suspenders.h"

void greeter(void *arg) {
    const char *name = (const char*)arg;
    printf("Hello, %s!\n", name);
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    
    suspenders_spawn(greeter, "World", SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(greeter, "Suspenders", SUSPENDERS_QOS_NORMAL);
    
    suspenders_run();
    suspenders_shutdown();
    return 0;
}
```

### Channel Communication

```c
void producer(void *arg) {
    suspenders_chan_t *ch = (suspenders_chan_t*)arg;
    int values[] = {1, 2, 3, 4, 5};
    
    for (int i = 0; i < 5; i++) {
        suspenders_chan_send(ch, &values[i]);
    }
}

void consumer(void *arg) {
    suspenders_chan_t *ch = (suspenders_chan_t*)arg;
    int value;
    
    while (suspenders_chan_recv(ch, &value)) {
        printf("Received: %d\n", value);
    }
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    
    suspenders_chan_t *ch = suspenders_chan_create(sizeof(int), 0);
    
    suspenders_spawn(producer, ch, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(consumer, ch, SUSPENDERS_QOS_NORMAL);
    
    suspenders_run();
    suspenders_shutdown();
    return 0;
}
```

### TCP Echo Server

```c
void handle_client(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buffer[1024];
    
    while (suspenders_hose_read(client, buffer, sizeof(buffer))) {
        suspenders_hose_write(client, buffer, strlen(buffer));
    }
    
    suspenders_hose_close(client);
}

void server(void *arg) {
    suspenders_hose_t listener, client;
    struct buf b;
    
    buf_clear(&b);
    suspenders_hose_init(&listener, suspenders_ring(), &b);
    
    if (!suspenders_hose_listen(&listener, "tcp://0.0.0.0:8080")) {
        fprintf(stderr, "Failed to bind\n");
        return;
    }
    
    while (suspenders_hose_accept(&listener, &client)) {
        suspenders_spawn(handle_client, &client, SUSPENDERS_QOS_NORMAL);
    }
}

int main(void) {
    memento_init();
    suspenders_init(4, 4096);  // 4 worker threads
    
    suspenders_spawn(server, NULL, SUSPENDERS_QOS_HIGH);
    
    suspenders_run();
    suspenders_shutdown();
    return 0;
}
```

## Benchmarking

Measure context switch performance:

```bash
./build/suspenders-benchmark [iterations]
```

Default is 1,000,000 iterations. The benchmark creates two coroutines that ping-pong yield between each other, measuring the time per context switch.

Example output:
```
Context switch benchmark
========================
Iterations: 1000000
Total time: 0.015s
Time per switch: 15ns
```

## Architecture

### Context Switching

Context switches are implemented in hand-rolled assembly:

- **x86_64**: Saves/restores `rsp`, `rbx`, `rbp`, `r12-r15` (56 bytes)
- **aarch64**: Saves/restores `x19-x28`, `fp`, `lr`, `sp` (104 bytes)

The assembly routines use `__attribute__((naked))` to have full control over the function prologue/epilogue, eliminating any compiler-generated overhead.

### Memory Management

Suspenders integrates with Memento, a thread-local arena allocator:

- Coroutine stacks allocated from per-thread arenas (1MB default)
- No locks on allocation/deallocation in hot paths
- Bump allocation for small temporary objects

### Scheduling

The scheduler uses Linux io_uring for event notification:

- Coroutines suspended on I/O are registered with io_uring
- Completion events resume suspended coroutines
- Work-stealing queue for multi-threaded operation

### Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Supported | Production ready |
| Linux aarch64 | Supported | Production ready |
| macOS | Not supported | No io_uring support |
| Windows | Not supported | No io_uring support |
| BSD | Not supported | No io_uring support |

## C++ Wrapper

Suspenders includes a modern C++17 wrapper (`include/suspenders.hpp`) that turns Suspenders into a type-safe, RAII-friendly API. It adds move semantics, `std::string_view` support, templated channels, and lambda-ready coroutine spawning while staying header-only and zero-overhead.

### Quick Start (C++)

```cpp
#include "suspenders.hpp"

int main() {
    suspenders::Context ctx;  // RAII initialization
    suspenders::Channel<int> ch;  // Type-safe channel
    
    auto task = suspenders::spawn([&ch] {
        ch.send(42);
    });
    
    auto val = ch.recv();  // Returns std::optional<int>
    ctx.run();
    return 0;
}
```

### C++ API Overview

```cpp
// RAII Context - manages init/shutdown automatically
suspenders::Context ctx(num_workers, io_uring_entries);
ctx.run();

// Type-safe Channel<T> - zero-copy for trivially copyable types
suspenders::Channel<T> ch(buffer_size);
bool sent = ch.send(value);
std::optional<T> val = ch.recv();

// Lambda/functor spawning
auto task = suspenders::spawn([]{ /* coroutine code */ }, suspenders::QoS::High);
task.resume();
task.boost(suspenders::QoS::Realtime);

// RAII Hose with move semantics
suspenders::Hose hose(ctx.ring());
hose.listen("tcp://0.0.0.0:8080");
suspenders::Hose client;
hose.accept(client);
ssize_t n = client.read(buffer, len);
ssize_t n = client.write(data, len);

// RAII Buffer with STL-like interface
suspenders::Buffer buf;
buf.append("data");
buf.append(data, len);
std::string_view sv = buf.view();
```

### C++ Wrapper Features

- **RAII Everything**: Context, Hose, Buffer, and Channel<T> automatically clean up resources. Channel destruction works around the missing C `suspenders_chan_destroy` by calling `memento_thread_heap_free` directly.

- **Lambda Spawning**: `suspenders::spawn([]{ ... })` accepts any callable (lambdas with captures, `std::function`, functors) and manages heap allocation automatically. No manual `void*` casting required.

- **Type-Safe Channels**: `Channel<int>`, `Channel<MyStruct>`, etc. with `static_assert` ensuring trivial copyability at compile time (matching C zero-copy semantics). Provides `std::optional<T>` receive interface.

- **Modern String Handling**: `std::string_view` everywhere for URIs (`pipe.dial("tcp://host:port")`) and buffer appends, zero-copy where possible.

- **Move Semantics**: All handles are movable but non-copyable, enforcing unique ownership of file descriptors and channel handles.

- **STL Integration**: Buffer provides `begin()`/`end()` iterators, works with range-based for loops, and converts to `std::string` or `std::vector<uint8_t>`.

- **Error Safety**: Uses `std::optional` for receives and exceptions for constructor failures (channel creation).

### C++ Example: Producer-Consumer with TCP Server

```cpp
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include <iostream>

int main() {
    suspenders::Context ctx;
    suspenders::Channel<int> ch;

    // Spawn producer
    auto producer = suspenders::spawn([&ch]{
        for (int i = 0; i < 10; ++i) {
            ch.send(i);
            suspenders::yield();
        }
    }, suspenders::QoS::High);

    // Spawn consumer
    auto consumer = suspenders::spawn([&ch]{
        while (auto val = ch.recv()) {
            std::cout << "Received: " << *val << "\n";
        }
    });

    // TCP server example
    suspenders::spawn([]{
        suspenders::Hose listener;
        if (listener.listen("tcp://0.0.0.0:8080")) {
            std::cout << "Listening on 8080\n";
            while (true) {
                suspenders::Hose client;
                if (listener.accept(client)) {
                    suspenders::spawn([client = std::move(client)]() mutable {
                        char buf[256];
                        ssize_t n = client.read(buf, sizeof(buf));
                        if (n > 0) {
                            client.write("HTTP/1.1 200 OK\r\n\r\n");
                        }
                    });
                }
            }
        }
    });

    ctx.run();
    return 0;
}
```

### Including in C++ Projects

```cpp
// In one .cpp file:
#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"  // Includes suspenders.h automatically
```

## Integration

### CMake

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing>=2.0)

add_executable(myapp main.c)
target_include_directories(myapp PRIVATE include third_party ${LIBURING_INCLUDE_DIRS})
target_link_libraries(myapp PRIVATE ${LIBURING_LIBRARIES})
target_compile_options(myapp PRIVATE ${LIBURING_CFLAGS_OTHER})
```

### pkg-config

```bash
gcc -o myapp main.c $(pkg-config --cflags --libs liburing) -I./include -I./third_party
```

## Common Pitfalls

1. **Missing implementation defines**: Ensure `MEMENTO_IMPLEMENTATION` and `SUSPENDERS_IMPLEMENTATION` are defined in exactly one `.c` file before the includes.

2. **Include order**: Memento must be included before suspenders.h when using the implementation defines.

3. **Forgetting `suspenders_run()`**: Coroutines do not execute until the scheduler is started with `suspenders_run()`.

4. **Stack overflow**: Default stack is 1MB. Avoid deep recursion in coroutine functions.

5. **Platform support**: Only Linux x86_64 and aarch64 are supported. The build will fail explicitly on other platforms.

## License

See source files for license information.

## Contributing

Contributions are welcome. Please ensure:

- Code follows the existing style (4-space indentation, K&R braces)
- Tests pass on both x86_64 and aarch64
- Benchmarks show no regression in context switch latency
- Assembly changes include comments explaining register usage
