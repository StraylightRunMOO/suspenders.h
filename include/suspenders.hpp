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
        // suspenders_atomic_int is std::atomic<int> in C++ mode: load first
        return static_cast<State>(static_cast<int>(cr_->state));
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
// Synchronization Primitives (value types over the C API)
// ============================================================================

class Mutex {
    suspenders_mutex_t m_;
public:
    Mutex() { suspenders_mutex_init(&m_); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    int lock() { return suspenders_mutex_lock(&m_); }
    int lock_dl(uint64_t deadline_ns) { return suspenders_mutex_lock_dl(&m_, deadline_ns); }
    [[nodiscard]] int try_lock() { return suspenders_mutex_trylock(&m_); }
    int unlock() { return suspenders_mutex_unlock(&m_); }
    [[nodiscard]] suspenders_mutex_t* native() { return &m_; }
};

// Scoped lock over suspenders::Mutex.
class LockGuard {
    Mutex& m_;
public:
    explicit LockGuard(Mutex& m) : m_(m) { (void)m_.lock(); }
    ~LockGuard() { (void)m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

class RWLock {
    suspenders_rwlock_t rw_;
public:
    RWLock() { suspenders_rwlock_init(&rw_); }
    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;

    int rdlock() { return suspenders_rwlock_rdlock(&rw_); }
    int rdlock_dl(uint64_t deadline_ns) { return suspenders_rwlock_rdlock_dl(&rw_, deadline_ns); }
    [[nodiscard]] int try_rdlock() { return suspenders_rwlock_tryrdlock(&rw_); }
    int wrlock() { return suspenders_rwlock_wrlock(&rw_); }
    int wrlock_dl(uint64_t deadline_ns) { return suspenders_rwlock_wrlock_dl(&rw_, deadline_ns); }
    [[nodiscard]] int try_wrlock() { return suspenders_rwlock_trywrlock(&rw_); }
    int unlock() { return suspenders_rwlock_unlock(&rw_); }
    [[nodiscard]] suspenders_rwlock_t* native() { return &rw_; }
};

class Cond {
    suspenders_cond_t c_;
public:
    Cond() { suspenders_cond_init(&c_); }
    Cond(const Cond&) = delete;
    Cond& operator=(const Cond&) = delete;

    int wait(Mutex& m) { return suspenders_cond_wait(&c_, m.native()); }
    int wait_dl(Mutex& m, uint64_t deadline_ns) {
        return suspenders_cond_wait_dl(&c_, m.native(), deadline_ns);
    }
    int signal() { return suspenders_cond_signal(&c_); }
    int broadcast() { return suspenders_cond_broadcast(&c_); }
    [[nodiscard]] suspenders_cond_t* native() { return &c_; }
};

class WaitGroup {
    suspenders_waitgroup_t wg_;
public:
    WaitGroup() { suspenders_waitgroup_init(&wg_); }
    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;

    int add(int delta) { return suspenders_waitgroup_add(&wg_, delta); }
    int done() { return suspenders_waitgroup_done(&wg_); }
    int wait() { return suspenders_waitgroup_wait(&wg_); }
    int wait_dl(uint64_t deadline_ns) { return suspenders_waitgroup_wait_dl(&wg_, deadline_ns); }
    [[nodiscard]] suspenders_waitgroup_t* native() { return &wg_; }
};

// RAII cleanup handler: runs at scope exit, or at coroutine exit if the
// coroutine is canceled/exits mid-scope (via the C cleanup stack).
class CleanupGuard {
    suspenders_cleanup_t node_{};
    std::function<void()> fn_;
    static void trampoline(void* arg) {
        auto* self = static_cast<CleanupGuard*>(arg);
        if (self->fn_) self->fn_();
    }
public:
    template<typename F>
    explicit CleanupGuard(F&& f) : fn_(std::forward<F>(f)) {
        suspenders_cleanup_push(&node_, trampoline, this);
    }
    ~CleanupGuard() {
        suspenders_cleanup_pop(0);   // unregister; run from the destructor
        if (fn_) fn_();
    }
    CleanupGuard(const CleanupGuard&) = delete;
    CleanupGuard& operator=(const CleanupGuard&) = delete;
    void release() { fn_ = nullptr; }   // disarm: never runs
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
        if (ch_) suspenders_chan_destroy(ch_);
    }
    
    // Non-copyable, movable
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    
    Channel(Channel&& other) noexcept : ch_(other.ch_) {
        other.ch_ = nullptr;
    }
    
    Channel& operator=(Channel&& other) noexcept {
        if (this != &other) {
            if (ch_) suspenders_chan_destroy(ch_);
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
    
    // Status-returning variants (SUSPENDERS_OK / CLOSED / FULL / EMPTY / ...)
    [[nodiscard]] int send_status(const T& value) {
        return ch_ ? suspenders_chan_send(ch_, const_cast<T*>(&value)) : SUSPENDERS_INVAL;
    }
    [[nodiscard]] int recv_status(T& out) {
        return ch_ ? suspenders_chan_recv(ch_, &out) : SUSPENDERS_INVAL;
    }
    [[nodiscard]] int try_send(const T& value) {
        return ch_ ? suspenders_chan_try_send(ch_, const_cast<T*>(&value)) : SUSPENDERS_INVAL;
    }
    [[nodiscard]] int try_recv(T& out) {
        return ch_ ? suspenders_chan_try_recv(ch_, &out) : SUSPENDERS_INVAL;
    }
    [[nodiscard]] int send_dl(const T& value, uint64_t deadline_ns) {
        return ch_ ? suspenders_chan_send_dl(ch_, const_cast<T*>(&value), deadline_ns)
                   : SUSPENDERS_INVAL;
    }
    [[nodiscard]] int recv_dl(T& out, uint64_t deadline_ns) {
        return ch_ ? suspenders_chan_recv_dl(ch_, &out, deadline_ns) : SUSPENDERS_INVAL;
    }
    int close() {
        return ch_ ? suspenders_chan_close(ch_) : SUSPENDERS_INVAL;
    }

    // Select case builders: suspenders::select({ch.recv_op(v), ch2.send_op(x)})
    [[nodiscard]] suspenders_chan_op_t recv_op(T& out) {
        return suspenders_chan_op_t{ ch_, &out, false };
    }
    [[nodiscard]] suspenders_chan_op_t send_op(T& value) {
        return suspenders_chan_op_t{ ch_, &value, true };
    }

    [[nodiscard]] size_t element_size() const { return sizeof(T); }
    [[nodiscard]] suspenders_chan_t* native() const { return ch_; }
};

// Multi-channel select over case builders. Returns the winning case index
// (suspenders_errno holds OK/CLOSED) or a negative status (TIMEDOUT/...).
inline int select(std::initializer_list<suspenders_chan_op_t> cases,
                  uint64_t deadline_ns = 0) {
    suspenders_chan_op_t ops[SUSPENDERS_SELECT_MAX];
    int n = 0;
    for (const auto& op : cases) {
        if (n >= SUSPENDERS_SELECT_MAX) break;
        ops[n++] = op;
    }
    return suspenders_select_dl(ops, n, deadline_ns);
}

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

    [[nodiscard]] bool accept_dl(Hose& client, uint64_t deadline_ns) {
        if (!valid_) return false;
        suspenders_hose_t client_hose;
        if (suspenders_hose_accept_dl(&suspenders_hose_, &client_hose, deadline_ns)) {
            client.close();
            client.suspenders_hose_ = client_hose;
            client.valid_ = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] ssize_t read_dl(void* dest, size_t len, uint64_t deadline_ns) {
        if (!valid_) return -1;
        return suspenders_hose_read_dl(&suspenders_hose_, dest, len, deadline_ns);
    }

    [[nodiscard]] ssize_t write_dl(const void* src, size_t len, uint64_t deadline_ns) {
        if (!valid_) return -1;
        return suspenders_hose_write_dl(&suspenders_hose_, src, len, deadline_ns);
    }

    [[nodiscard]] ssize_t readv(const struct iovec* iov, int iovcnt) {
        if (!valid_) return -1;
        return suspenders_hose_readv(&suspenders_hose_, iov, iovcnt);
    }

    [[nodiscard]] ssize_t writev(const struct iovec* iov, int iovcnt) {
        if (!valid_) return -1;
        return suspenders_hose_writev(&suspenders_hose_, iov, iovcnt);
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
    
    int shutdown(int how) {
        return valid_ ? suspenders_hose_shutdown(&suspenders_hose_, how) : SUSPENDERS_INVAL;
    }
    int set_option(int level, int optname, const void* optval, socklen_t optlen) {
        return valid_ ? suspenders_hose_set_option(&suspenders_hose_, level, optname, optval, optlen)
                      : SUSPENDERS_INVAL;
    }
    int peername(struct sockaddr* addr, socklen_t* addrlen) {
        return valid_ ? suspenders_hose_peername(&suspenders_hose_, addr, addrlen) : SUSPENDERS_INVAL;
    }
    int sockname(struct sockaddr* addr, socklen_t* addrlen) {
        return valid_ ? suspenders_hose_sockname(&suspenders_hose_, addr, addrlen) : SUSPENDERS_INVAL;
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
// Task Queue (libdispatch-style; concurrency 1 = serial)
// ============================================================================

class Queue {
    suspenders_queue_t* q_ = nullptr;
    bool owned_ = false;

    static void trampoline(void* arg) {
        auto* cb = static_cast<std::function<void()>*>(arg);
        (*cb)();
        delete cb;
    }

    explicit Queue(suspenders_queue_t* q, bool owned) : q_(q), owned_(owned) {}

public:
    Queue() = default;

    explicit Queue(const char* label, QoS qos = QoS::Normal, unsigned concurrency = 1)
        : q_(suspenders_queue_create(label, static_cast<suspenders_qos_t>(qos), concurrency)),
          owned_(true)
    {
        if (!q_) throw std::runtime_error("suspenders::Queue: failed to create queue");
    }

    // Non-owning handle to a shared global queue (freed at shutdown).
    static Queue global(QoS qos = QoS::Normal) {
        return Queue(suspenders_get_global_queue(static_cast<suspenders_qos_t>(qos)), false);
    }

    ~Queue() {
        if (q_ && owned_) suspenders_queue_destroy(q_);
    }

    Queue(Queue&& other) noexcept : q_(other.q_), owned_(other.owned_) {
        other.q_ = nullptr;
        other.owned_ = false;
    }

    Queue& operator=(Queue&& other) noexcept {
        if (this != &other) {
            if (q_ && owned_) suspenders_queue_destroy(q_);
            q_ = other.q_;
            owned_ = other.owned_;
            other.q_ = nullptr;
            other.owned_ = false;
        }
        return *this;
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    template<typename F>
    int async(F&& f) {
        if (!q_) return SUSPENDERS_INVAL;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        int st = suspenders_queue_async(q_, trampoline, ptr);
        if (st != SUSPENDERS_OK) delete ptr;
        return st;
    }

    template<typename F>
    int sync(F&& f) {
        if (!q_) return SUSPENDERS_INVAL;
        std::function<void()> fn(std::forward<F>(f));
        return suspenders_queue_sync(q_, [](void* a) {
            (*static_cast<std::function<void()>*>(a))();
        }, &fn);
    }

    template<typename F>
    int after(uint64_t delay_ns, F&& f) {
        if (!q_) return SUSPENDERS_INVAL;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        int st = suspenders_queue_after(q_, delay_ns, trampoline, ptr);
        if (st != SUSPENDERS_OK) delete ptr;
        return st;
    }

    template<typename F>
    int barrier_async(F&& f) {
        if (!q_) return SUSPENDERS_INVAL;
        auto* ptr = new std::function<void()>(std::forward<F>(f));
        int st = suspenders_queue_barrier_async(q_, trampoline, ptr);
        if (st != SUSPENDERS_OK) delete ptr;
        return st;
    }

    [[nodiscard]] const char* label() const { return suspenders_queue_label(q_); }
    [[nodiscard]] bool valid() const { return q_ != nullptr; }
    explicit operator bool() const { return valid(); }
    [[nodiscard]] suspenders_queue_t* native() const { return q_; }
};

// Deprecated alias from the pre-1.0 dispatch API.
using DispatchQueue = Queue;

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

} // namespace suspenders
