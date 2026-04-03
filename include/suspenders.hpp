#pragma once

/*
 * suspenders.hpp - Modern C++17 wrapper for Suspenders
 * 
 * Features:
 *   - RAII runtime context, pipes, and buffers
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

extern "C" {
#include "suspenders.h"
}

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
    explicit Context(unsigned num_workers = 0, unsigned io_uring_entries = 256) {
        suspenders_init(num_workers, io_uring_entries);
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
    
    [[nodiscard]] static struct io_uring* ring() {
        return suspenders_ring();
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
        // Note: C API lacks explicit destroy, so we manually free via memento
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
        // C API does memcpy internally, so const_cast is safe here
        return suspenders_chan_send(ch_, const_cast<T*>(&value));
    }
    
    [[nodiscard]] bool recv(T& out) {
        if (!ch_) return false;
        return suspenders_chan_recv(ch_, &out);
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
// Async Hose (TCP/Unix domain socket abstraction)
// ============================================================================

class Hose {
    hose_t hose_{};
    bool valid_ = false;
    
public:
    Hose() {
        memset(&hose_, 0, sizeof(hose_));
        hose_.fd = -1;
    }
    
    explicit Hose(struct io_uring* ring, struct buf* buffer = nullptr) : Hose() {
        hose_init(&hose_, ring ? ring : suspenders_ring(), buffer);
        valid_ = true;
    }
    
    ~Hose() {
        if (valid_) {
            hose_close(&hose_);
        }
    }
    
    // Disable copy, enable move
    Hose(const Hose&) = delete;
    Hose& operator=(const Hose&) = delete;
    
    Hose(Hose&& other) noexcept 
        : hose_(other.hose_), valid_(other.valid_) 
    {
        other.valid_ = false;
        other.hose_.fd = -1;
    }
    
    Hose& operator=(Hose&& other) noexcept {
        if (this != &other) {
            if (valid_) hose_close(&hose_);
            hose_ = other.hose_;
            valid_ = other.valid_;
            other.valid_ = false;
            other.hose_.fd = -1;
        }
        return *this;
    }
    
    // Connection setup
    [[nodiscard]] bool dial(std::string_view uri) {
        if (!valid_) return false;
        return hose_dial(&hose_, std::string(uri).c_str());
    }
    
    [[nodiscard]] bool listen(std::string_view uri) {
        if (!valid_) return false;
        return hose_listen(&hose_, std::string(uri).c_str());
    }
    
    [[nodiscard]] bool accept(Hose& client) {
        if (!valid_) return false;
        hose_t client_hose;
        if (hose_accept(&hose_, &client_hose)) {
            client = Hose();  // Reset client
            client.hose_ = client_hose;
            client.valid_ = true;
            return true;
        }
        return false;
    }
    
    // I/O operations
    [[nodiscard]] ssize_t read(void* dest, size_t len) {
        if (!valid_) return -1;
        return hose_read(&hose_, dest, len);
    }
    
    [[nodiscard]] ssize_t write(const void* src, size_t len) {
        if (!valid_) return -1;
        return hose_write(&hose_, src, len);
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
            hose_close(&hose_);
            valid_ = false;
        }
    }
    
    [[nodiscard]] int fd() const { return hose_.fd; }
    [[nodiscard]] bool valid() const { return valid_ && hose_.fd >= 0; }
    [[nodiscard]] hose_t* native() { return &hose_; }
};

// ============================================================================
// Dynamic Buffer (RAII wrapper for struct buf)
// ============================================================================

class Buffer {
    struct buf buf_{};
    
public:
    Buffer() = default;
    
    explicit Buffer(std::string_view initial) : Buffer() {
        append(initial);
    }
    
    explicit Buffer(const void* data, size_t len) : Buffer() {
        append(data, len);
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
        // Optimized for iterators
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
        // Not supported by underlying buf, but we could realloc
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
// Coroutine Spawning (Lambda Support)
// ============================================================================

namespace detail {

template<typename F>
void coroutine_trampoline(void* arg) {
    auto* func = static_cast<std::decay_t<F>*>(arg);
    (*func)();
    delete func;  // Clean up the allocated lambda
}

} // namespace detail

template<typename F>
[[nodiscard]] Task spawn(F&& f, QoS qos = QoS::Normal) {
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
[[nodiscard]] inline Task spawn(void (*func)(void*), void* arg = nullptr, 
                                QoS qos = QoS::Normal) {
    suspenders_cr_t* cr = suspenders_spawn(func, arg, static_cast<suspenders_qos_t>(qos));
    if (!cr) throw std::runtime_error("suspenders::spawn: failed to create coroutine");
    return Task(cr);
}

// ============================================================================
// Fiber Utilities (within current coroutine)
// ============================================================================

[[nodiscard]] inline Task current_task() {
    // suspenders_running is thread_local in C lib
    return Task(suspenders_running);
}

inline void yield() {
    suspenders_yield();
}

inline void suspend() {
    suspenders_suspend();
}

inline void sleep_ms(int ms) {
    // Could implement via io_uring timeout, for now just yield
    (void)ms;
    yield();
}

// ============================================================================
// Async I/O Helpers
// ============================================================================

#if defined(__linux__)
inline void writev_async(int fd, struct iovec* iovs, int count) {
    suspenders_writev_async(fd, iovs, count);
}
#endif

} // namespace suspenders
