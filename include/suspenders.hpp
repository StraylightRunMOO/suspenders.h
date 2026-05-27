#pragma once

/*
 * suspenders.hpp - Modern C++17 wrapper for Suspenders
 * 
 * Features:
 *   - RAII runtime context, hoses, and buffers
 *   - Type-safe Channel<T> with zero-copy semantics (T must be trivially copyable)
 *   - Lambda/functor coroutine spawning (no manual void* casting)
 *   - std::string_view integration for URIs and buffers
 *   - Movable but non-copyable handles (unique ownership)
 *   - Optional<T> returns for error handling
 * 
 * Usage:
 *   #define SUSPENDERS_IMPLEMENTATION  // in one .cpp file, includes C impl
 *   #include "suspenders.hpp"
 *   
 *   suspenders::Context ctx;
 *   suspenders::Channel<int> ch;
 *   
 *   auto task = suspenders::spawn([&ch]{
 *       int val = 42;
 *       ch.send(val);
 *   });
 *   
 *   ctx.run();
 */

#include "suspenders.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <utility>
#include <functional>
#include <optional>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <iterator>
#include <chrono>

namespace suspenders {

// ============================================================================
// Type Safety
// ============================================================================

enum class QoS {
    Realtime = SUSPENDERS_QOS_REALTIME,
    High     = SUSPENDERS_QOS_HIGH,
    Normal   = SUSPENDERS_QOS_NORMAL,
    Low      = SUSPENDERS_QOS_LOW
};

enum class State {
    Ready     = SUSPENDERS_STATE_READY,
    Running   = SUSPENDERS_STATE_RUNNING,
    Suspended = SUSPENDERS_STATE_SUSPENDED,
    Done      = SUSPENDERS_STATE_DONE
};

// ============================================================================
// Runtime Context (RAII)
// ============================================================================

class Context {
public:
    explicit Context(unsigned num_workers = 0, unsigned queue_hint = 256) {
        suspenders_init(num_workers, queue_hint);
    }
    
    ~Context() {
        suspenders_shutdown();
    }
    
    // Non-copyable, non-movable (global state per thread)
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    
    void run() {
        suspenders_run();
    }
};

// ============================================================================
// Task Handle (non-owning reference to coroutine)
// ============================================================================

class Task {
    suspenders_cr_t* cr_ = nullptr;
    
public:
    Task() = default;
    explicit Task(suspenders_cr_t* cr) : cr_(cr) {}
    
    void resume() {
        if (cr_) suspenders_resume(cr_);
    }
    
    void boost(QoS new_qos) {
        if (cr_) suspenders_boost(cr_, static_cast<suspenders_qos_t>(new_qos));
    }
    
    void cancel() {
        if (cr_) suspenders_cancel(cr_);
    }
    
    [[nodiscard]] State state() const {
        if (!cr_) return State::Done;
        return static_cast<State>(cr_->state);
    }
    
    [[nodiscard]] bool done() const {
        return state() == State::Done;
    }
    
    [[nodiscard]] bool valid() const { return cr_ != nullptr; }
    explicit operator bool() const { return valid(); }
    
    [[nodiscard]] suspenders_cr_t* native() const { return cr_; }
};

// ============================================================================
// Timer (RAII wrapper around suspenders_timer_t)
// ============================================================================

class Timer {
    suspenders_timer_t* t_ = nullptr;
    void* arg_ = nullptr;
    
    static void trampoline(void* arg) {
        auto* cb = static_cast<std::function<void()>*>(arg);
        (*cb)();
    }
    
public:
    Timer() = default;
    
    Timer(int ms, bool repeat, std::function<void()> cb) {
        auto* ptr = new std::function<void()>(std::move(cb));
        t_ = suspenders_timer_create(ms, repeat, trampoline, ptr);
        if (!t_) {
            delete ptr;
            throw std::runtime_error("suspenders::Timer: failed to create timer");
        }
        arg_ = ptr;
    }
    
    ~Timer() {
        cancel();
    }
    
    Timer(Timer&& other) noexcept 
        : t_(other.t_), arg_(other.arg_) 
    {
        other.t_ = nullptr;
        other.arg_ = nullptr;
    }
    
    Timer& operator=(Timer&& other) noexcept {
        if (this != &other) {
            cancel();
            t_ = other.t_;
            arg_ = other.arg_;
            other.t_ = nullptr;
            other.arg_ = nullptr;
        }
        return *this;
    }
    
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    
    void cancel() {
        if (t_) {
            suspenders_timer_cancel(t_);
            t_ = nullptr;
        }
        if (arg_) {
            delete static_cast<std::function<void()>*>(arg_);
            arg_ = nullptr;
        }
    }
    
    [[nodiscard]] bool valid() const { return t_ != nullptr; }
    explicit operator bool() const { return valid(); }
};

// ============================================================================
// Ticket Lock (RAII lock guard)
// ============================================================================

class TicketLock {
    suspenders_ticket_lock_t lock_;
    
public:
    TicketLock() {
        suspenders_ticket_init(&lock_);
    }
    
    void lock() {
        suspenders_ticket_lock(&lock_);
    }
    
    void unlock() {
        suspenders_ticket_unlock(&lock_);
    }
    
    class Guard {
        TicketLock* lock_;
    public:
        explicit Guard(TicketLock& lock) : lock_(&lock) { lock_->lock(); }
        ~Guard() { if (lock_) lock_->unlock(); }
        Guard(Guard&& other) noexcept : lock_(other.lock_) { other.lock_ = nullptr; }
        Guard& operator=(Guard&& other) noexcept {
            if (this != &other) {
                if (lock_) lock_->unlock();
                lock_ = other.lock_;
                other.lock_ = nullptr;
            }
            return *this;
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    };
};

// ============================================================================
// Type-Safe Channel (zero-copy rendezvous)
// ============================================================================

template<typename T>
class Channel {
    static_assert(std::is_trivially_copyable_v<T>, 
                  "Channel<T> requires trivially copyable types for zero-copy safety");
    suspenders_chan_t* ch_ = nullptr;
    
public:
    explicit Channel(size_t buffer_size = 0) 
        : ch_(suspenders_chan_create(sizeof(T), buffer_size)) 
    {
        if (!ch_) throw std::runtime_error("suspenders::Channel: failed to create channel");
    }
    
    ~Channel() {
        if (ch_) {
            memento_thread_heap_free(memento_thread_heap_get(), ch_, sizeof(suspenders_chan_t));
        }
    }
    
    // Non-copyable, movable
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    
    Channel(Channel&& other) noexcept : ch_(other.ch_) {
        other.ch_ = nullptr;
    }
    
    Channel& operator=(Channel&& other) noexcept {
        if (this != &other) {
            if (ch_) {
                memento_thread_heap_free(memento_thread_heap_get(), ch_, sizeof(suspenders_chan_t));
            }
            ch_ = other.ch_;
            other.ch_ = nullptr;
        }
        return *this;
    }
    
    [[nodiscard]] bool send(const T& value) {
        if (!ch_) return false;
        return suspenders_chan_send(ch_, const_cast<T*>(&value)) == SUSPENDERS_OK;
    }
    
    [[nodiscard]] bool recv(T& out) {
        if (!ch_) return false;
        return suspenders_chan_recv(ch_, &out) == SUSPENDERS_OK;
    }
    
    // Blocking recv with optional
    [[nodiscard]] std::optional<T> recv() {
        T val{};
        if (recv(val)) return val;
        return std::nullopt;
    }
    
    [[nodiscard]] size_t element_size() const { return sizeof(T); }
    [[nodiscard]] suspenders_chan_t* native() const { return ch_; }
};

// ============================================================================
// Async Hose (transport-agnostic stream abstraction)
// ============================================================================

class Hose {
    suspenders_hose_t suspenders_hose_{};
    bool valid_ = false;
    
public:
    Hose() {
        suspenders_hose_init(&suspenders_hose_, nullptr);
        valid_ = true;
    }
    
    explicit Hose(struct buf* buffer) : Hose() {
        suspenders_hose_.buffer = buffer;
    }
    
    ~Hose() {
        if (valid_) {
            suspenders_hose_close(&suspenders_hose_);
        }
    }
    
    // Disable copy, enable move
    Hose(const Hose&) = delete;
    Hose& operator=(const Hose&) = delete;
    
    Hose(Hose&& other) noexcept 
        : suspenders_hose_(other.suspenders_hose_), valid_(other.valid_) 
    {
        other.valid_ = false;
        other.suspenders_hose_.fd = SUSPENDERS_INVALID_SOCK;
    }
    
    Hose& operator=(Hose&& other) noexcept {
        if (this != &other) {
            if (valid_) suspenders_hose_close(&suspenders_hose_);
            suspenders_hose_ = other.suspenders_hose_;
            valid_ = other.valid_;
            other.valid_ = false;
            other.suspenders_hose_.fd = SUSPENDERS_INVALID_SOCK;
        }
        return *this;
    }
    
    // Connection setup
    [[nodiscard]] bool dial(std::string_view uri) {
        if (!valid_) return false;
        return suspenders_hose_dial(&suspenders_hose_, std::string(uri).c_str());
    }
    
    [[nodiscard]] bool listen(std::string_view uri) {
        if (!valid_) return false;
        return suspenders_hose_listen(&suspenders_hose_, std::string(uri).c_str());
    }
    
    [[nodiscard]] bool accept(Hose& client) {
        if (!valid_) return false;
        suspenders_hose_t client_hose;
        if (suspenders_hose_accept(&suspenders_hose_, &client_hose)) {
            client.close();
            client.suspenders_hose_ = client_hose;
            client.valid_ = true;
            return true;
        }
        return false;
    }
    
    // I/O operations
    [[nodiscard]] ssize_t read(void* dest, size_t len) {
        if (!valid_) return -1;
        return suspenders_hose_read(&suspenders_hose_, dest, len);
    }
    
    [[nodiscard]] ssize_t write(const void* src, size_t len) {
        if (!valid_) return -1;
        return suspenders_hose_write(&suspenders_hose_, src, len);
    }
    
    [[nodiscard]] ssize_t recvfrom(void* dest, size_t len, struct sockaddr* addr, socklen_t* addrlen) {
        if (!valid_) return -1;
        return suspenders_hose_recvfrom(&suspenders_hose_, dest, len, addr, addrlen);
    }
    
    [[nodiscard]] ssize_t sendto(const void* src, size_t len, const struct sockaddr* addr, socklen_t addrlen) {
        if (!valid_) return -1;
        return suspenders_hose_sendto(&suspenders_hose_, src, len, addr, addrlen);
    }
    
    template<typename T>
    [[nodiscard]] ssize_t write(const T& obj) {
        static_assert(std::is_trivially_copyable_v<T>);
        return write(&obj, sizeof(T));
    }
    
    [[nodiscard]] ssize_t write(std::string_view sv) {
        return write(sv.data(), sv.size());
    }
    
    void close() {
        if (valid_) {
            suspenders_hose_close(&suspenders_hose_);
            valid_ = false;
        }
    }
    
    [[nodiscard]] suspenders_sock_t fd() const { return suspenders_hose_.fd; }
    [[nodiscard]] bool valid() const { return valid_ && suspenders_hose_.fd != SUSPENDERS_INVALID_SOCK; }
    [[nodiscard]] suspenders_hose_t* native() { return &suspenders_hose_; }
};

// ============================================================================
// Dynamic Buffer (RAII wrapper for struct buf)
// ============================================================================

class Buffer {
    struct buf buf_{};
    
public:
    Buffer() = default;
    
    explicit Buffer(std::string_view initial) : Buffer() {
        (void)append(initial);
    }
    
    explicit Buffer(const void* data, size_t len) : Buffer() {
        (void)append(data, len);
    }
    
    ~Buffer() {
        buf_clear(&buf_);
    }
    
    // Disable copy, enable move
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    Buffer(Buffer&& other) noexcept : buf_(other.buf_) {
        other.buf_.data = nullptr;
        other.buf_.len = 0;
        other.buf_.cap = 0;
    }
    
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            buf_clear(&buf_);
            buf_ = other.buf_;
            other.buf_.data = nullptr;
            other.buf_.len = 0;
            other.buf_.cap = 0;
        }
        return *this;
    }
    
    // Append operations
    [[nodiscard]] bool append(std::string_view sv) {
        return buf_append(&buf_, sv.data(), static_cast<ssize_t>(sv.size()));
    }
    
    [[nodiscard]] bool append(const void* data, size_t len) {
        return buf_append(&buf_, static_cast<const char*>(data), static_cast<ssize_t>(len));
    }
    
    [[nodiscard]] bool append(const std::vector<uint8_t>& vec) {
        return append(vec.data(), vec.size());
    }
    
    [[nodiscard]] bool push_back(char c) {
        return buf_append_byte(&buf_, c);
    }
    
    template<typename Iter>
    [[nodiscard]] bool append(Iter first, Iter last) {
        if constexpr (std::is_same_v<typename std::iterator_traits<Iter>::value_type, char>) {
            std::string_view sv(&*first, std::distance(first, last));
            return append(sv);
        } else {
            for (; first != last; ++first) {
                if (!push_back(static_cast<char>(*first))) return false;
            }
            return true;
        }
    }
    
    void clear() {
        buf_.len = 0;
        if (buf_.data) buf_.data[0] = '\0';
    }
    
    void shrink_to_fit() {
        // Not supported by underlying buf
    }
    
    // Accessors
    [[nodiscard]] char* data() { return buf_.data; }
    [[nodiscard]] const char* data() const { return buf_.data; }
    [[nodiscard]] size_t size() const { return buf_.len; }
    [[nodiscard]] size_t capacity() const { return buf_.cap; }
    [[nodiscard]] bool empty() const { return buf_.len == 0; }
    [[nodiscard]] char* begin() { return buf_.data; }
    [[nodiscard]] char* end() { return buf_.data + buf_.len; }
    [[nodiscard]] const char* begin() const { return buf_.data; }
    [[nodiscard]] const char* end() const { return buf_.data + buf_.len; }
    
    [[nodiscard]] std::string_view view() const {
        return std::string_view(buf_.data, buf_.len);
    }
    
    [[nodiscard]] std::string str() const {
        return std::string(view());
    }
    
    [[nodiscard]] std::vector<uint8_t> bytes() const {
        return std::vector<uint8_t>(begin(), end());
    }
    
    [[nodiscard]] struct buf* native() { return &buf_; }
    [[nodiscard]] const struct buf* native() const { return &buf_; }
};

// ============================================================================
// Dispatch Queue (serial task queue)
// ============================================================================

class DispatchQueue {
    suspenders_dispatch_queue_t* q_ = nullptr;
    
    static void trampoline(void* arg) {
        auto* cb = static_cast<std::function<void()>*>(arg);
        (*cb)();
        delete cb;
    }
    
public:
    DispatchQueue() = default;
    
    explicit DispatchQueue(const char* label, QoS qos = QoS::Normal) {
        q_ = suspenders_dispatch_queue_create(label, static_cast<suspenders_qos_t>(qos));
        if (!q_) throw std::runtime_error("suspenders::DispatchQueue: failed to create queue");
    }
    
    ~DispatchQueue() {
        if (q_) suspenders_dispatch_queue_destroy(q_);
    }
    
    DispatchQueue(DispatchQueue&& other) noexcept : q_(other.q_) {
        other.q_ = nullptr;
    }
    
    DispatchQueue& operator=(DispatchQueue&& other) noexcept {
        if (this != &other) {
            if (q_) suspenders_dispatch_queue_destroy(q_);
            q_ = other.q_;
            other.q_ = nullptr;
        }
        return *this;
    }
    
    DispatchQueue(const DispatchQueue&) = delete;
    DispatchQueue& operator=(const DispatchQueue&) = delete;
    
    template<typename F>
    void async(F&& f) {
        if (!q_) return;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        suspenders_dispatch_async(q_, trampoline, ptr);
    }
    
    template<typename F>
    void barrier_async(F&& f) {
        if (!q_) return;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        suspenders_dispatch_barrier_async(q_, trampoline, ptr);
    }
    
    [[nodiscard]] bool valid() const { return q_ != nullptr; }
    explicit operator bool() const { return valid(); }
};

// ============================================================================
// Coroutine Pool (N workers consuming a shared task channel)
// ============================================================================

class Pool {
    suspenders_pool_t* pool_ = nullptr;
    
    static void trampoline(void* arg) {
        auto* cb = static_cast<std::function<void()>*>(arg);
        (*cb)();
        delete cb;
    }
    
public:
    Pool() = default;
    
    explicit Pool(unsigned nworkers, QoS qos = QoS::Normal) {
        pool_ = suspenders_pool_create(nworkers, static_cast<suspenders_qos_t>(qos));
        if (!pool_) throw std::runtime_error("suspenders::Pool: failed to create pool");
    }
    
    ~Pool() {
        if (pool_) suspenders_pool_destroy(pool_);
    }
    
    Pool(Pool&& other) noexcept : pool_(other.pool_) {
        other.pool_ = nullptr;
    }
    
    Pool& operator=(Pool&& other) noexcept {
        if (this != &other) {
            if (pool_) suspenders_pool_destroy(pool_);
            pool_ = other.pool_;
            other.pool_ = nullptr;
        }
        return *this;
    }
    
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    
    template<typename F>
    void submit(F&& f) {
        if (!pool_) return;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        suspenders_pool_submit(pool_, trampoline, ptr);
    }
    
    [[nodiscard]] bool valid() const { return pool_ != nullptr; }
    explicit operator bool() const { return valid(); }
};

// ============================================================================
// Coroutine Spawning (Lambda Support)
// ============================================================================

namespace detail {

template<typename F>
void coroutine_trampoline(void* arg) {
    auto* func = static_cast<std::decay_t<F>*>(arg);
    (*func)();
    delete func;
}

} // namespace detail

template<typename F>
Task spawn(F&& f, QoS qos = QoS::Normal) {
    using Func = std::decay_t<F>;
    auto* ptr = new Func(std::forward<F>(f));
    
    suspenders_cr_t* cr = suspenders_spawn(detail::coroutine_trampoline<Func>, ptr, 
                                 static_cast<suspenders_qos_t>(qos));
    if (!cr) {
        delete ptr;
        throw std::runtime_error("suspenders::spawn: failed to create coroutine");
    }
    return Task(cr);
}

// Convenience for void(*)(void*) functions (no allocation overhead)
inline Task spawn(void (*func)(void*), void* arg = nullptr, 
                                QoS qos = QoS::Normal) {
    suspenders_cr_t* cr = suspenders_spawn(func, arg, static_cast<suspenders_qos_t>(qos));
    if (!cr) throw std::runtime_error("suspenders::spawn: failed to create coroutine");
    return Task(cr);
}

// ============================================================================
// Fiber Utilities (within current coroutine)
// ============================================================================

[[nodiscard]] inline Task current_task() {
    return Task(suspenders_running);
}

inline void yield() {
    suspenders_yield();
}

inline void suspend() {
    suspenders_suspend();
}

inline void sleep_ms(int ms) {
    if (ms > 0) suspenders_sleep_ns(static_cast<uint64_t>(ms) * 1000000ULL);
}

template<typename Rep, typename Period>
inline void sleep_for(std::chrono::duration<Rep, Period> duration) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    if (ns > 0) suspenders_sleep_ns(static_cast<uint64_t>(ns));
}

inline uint64_t now_ns() {
    return suspenders_now_ns();
}

// ============================================================================
// Async I/O Helpers
// ============================================================================

#if SUSPENDERS_BACKEND_IOURING
inline void writev_async(int fd, struct iovec* iovs, int count) {
    suspenders_writev_async(fd, iovs, count);
}
#endif

} // namespace suspenders
