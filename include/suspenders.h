
#ifndef LIBSUSPENDERS_H
#define LIBSUSPENDERS_H

/* ============================================================================
 * Version
 * ============================================================================ */
#define SUSPENDERS_VERSION_MAJOR 1
#define SUSPENDERS_VERSION_MINOR 0
#define SUSPENDERS_VERSION_PATCH 0
#define SUSPENDERS_VERSION "1.0.0"
#define SUSPENDERS_VERSION_NUMBER \
    (SUSPENDERS_VERSION_MAJOR * 10000 + SUSPENDERS_VERSION_MINOR * 100 + SUSPENDERS_VERSION_PATCH)

/* ============================================================================
 * Language requirements - full C11 (or C++11) commitment
 * ============================================================================ */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L)
    #error "suspenders.h requires C11: compile with -std=c11 (or later)"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
    #define SUSPENDERS_TLS thread_local
#else
    #define SUSPENDERS_TLS _Thread_local
#endif

/* C11 alignment: applied to the first member of a struct so the whole
 * struct inherits the alignment ( _Alignas is not valid on typedefs). */
#ifdef __cplusplus
    #define SUSPENDERS_ALIGNAS(x) alignas(x)
    #define SUSPENDERS_RESTRICT __restrict
    #define SUSPENDERS_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
    #define SUSPENDERS_ALIGNAS(x) _Alignas(x)
    #define SUSPENDERS_RESTRICT restrict
    #define SUSPENDERS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
#define SUSPENDERS_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SUSPENDERS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SUSPENDERS_CACHELINE   64

/* ============================================================================
 * Atomic abstraction - C11 _Atomic in C, std::atomic in C++
 * ============================================================================ */
#ifdef __cplusplus
#include <atomic>
typedef std::atomic<int>           suspenders_atomic_int;
typedef std::atomic<uint32_t>      suspenders_atomic_u32;
typedef std::atomic<size_t>        suspenders_atomic_size_t;
typedef std::atomic<uintptr_t>     suspenders_atomic_uintptr_t;

#define SUSPENDERS_MEMORY_ORDER_RELAXED std::memory_order_relaxed
#define SUSPENDERS_MEMORY_ORDER_ACQUIRE std::memory_order_acquire
#define SUSPENDERS_MEMORY_ORDER_RELEASE std::memory_order_release
#define SUSPENDERS_MEMORY_ORDER_ACQ_REL std::memory_order_acq_rel
#define SUSPENDERS_MEMORY_ORDER_SEQ_CST std::memory_order_seq_cst

#define suspenders_atomic_init(x, val)                 ((x) = (val))
#define suspenders_atomic_load(x, order)               ((x).load(order))
#define suspenders_atomic_store(x, val, order)         ((x).store((val), (order)))
#define suspenders_atomic_fetch_add(x, val, order)     ((x).fetch_add((val), (order)))
#define suspenders_atomic_fetch_sub(x, val, order)     ((x).fetch_sub((val), (order)))
#define suspenders_atomic_exchange(x, val, order)      ((x).exchange((val), (order)))
#define suspenders_atomic_compare_exchange_weak(x, expected, desired, succ, fail) \
    ((x).compare_exchange_weak((expected), (desired), (succ), (fail)))
#define suspenders_atomic_compare_exchange_strong(x, expected, desired, succ, fail) \
    ((x).compare_exchange_strong((expected), (desired), (succ), (fail)))
#else
#include <stdatomic.h>
typedef _Atomic int                suspenders_atomic_int;
typedef _Atomic uint32_t           suspenders_atomic_u32;
typedef _Atomic size_t             suspenders_atomic_size_t;
typedef _Atomic uintptr_t          suspenders_atomic_uintptr_t;

#define SUSPENDERS_MEMORY_ORDER_RELAXED memory_order_relaxed
#define SUSPENDERS_MEMORY_ORDER_ACQUIRE memory_order_acquire
#define SUSPENDERS_MEMORY_ORDER_RELEASE memory_order_release
#define SUSPENDERS_MEMORY_ORDER_ACQ_REL memory_order_acq_rel
#define SUSPENDERS_MEMORY_ORDER_SEQ_CST memory_order_seq_cst

#define suspenders_atomic_init(x, val)                 atomic_init(&(x), (val))
#define suspenders_atomic_load(x, order)               atomic_load_explicit(&(x), (order))
#define suspenders_atomic_store(x, val, order)         atomic_store_explicit(&(x), (val), (order))
#define suspenders_atomic_fetch_add(x, val, order)     atomic_fetch_add_explicit(&(x), (val), (order))
#define suspenders_atomic_fetch_sub(x, val, order)     atomic_fetch_sub_explicit(&(x), (val), (order))
#define suspenders_atomic_exchange(x, val, order)      atomic_exchange_explicit(&(x), (val), (order))
#define suspenders_atomic_compare_exchange_weak(x, expected, desired, succ, fail) \
    atomic_compare_exchange_weak_explicit(&(x), &(expected), (desired), (succ), (fail))
#define suspenders_atomic_compare_exchange_strong(x, expected, desired, succ, fail) \
    atomic_compare_exchange_strong_explicit(&(x), &(expected), (desired), (succ), (fail))
#endif

/* ============================================================================
 * Platform Detection
 * ============================================================================ */
#if defined(__linux__)
    #define SUSPENDERS_PLATFORM_LINUX 1
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    #define SUSPENDERS_PLATFORM_BSD 1
#elif defined(_WIN32)
    #define SUSPENDERS_PLATFORM_WINDOWS 1
#else
    #define SUSPENDERS_PLATFORM_POSIX_GENERIC 1
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define SUSPENDERS_ARCH_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define SUSPENDERS_ARCH_AARCH64 1
#endif

/* Backend selection */
#if SUSPENDERS_PLATFORM_LINUX
    #define SUSPENDERS_BACKEND_IOURING 1
#elif SUSPENDERS_PLATFORM_BSD
    #define SUSPENDERS_BACKEND_KQUEUE 1
#elif SUSPENDERS_PLATFORM_WINDOWS
    #define SUSPENDERS_BACKEND_WSAPOLL 1
#else
    #define SUSPENDERS_BACKEND_POLL 1
#endif

/* ============================================================================
 * Platform Headers
 * ============================================================================ */
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <poll.h>
    #include <errno.h>
    #if SUSPENDERS_PLATFORM_BSD
        #include <sys/event.h>
        #include <sys/time.h>
    #endif
    #if SUSPENDERS_PLATFORM_LINUX
        #include <sys/syscall.h>
        #include <sys/uio.h>
    #endif
    #include <netinet/tcp.h>
    #include <sys/un.h>
#endif

/* NOTE: liburing.h is only needed by the implementation and is included
 * inside the SUSPENDERS_IMPLEMENTATION block. Consumers of this header do
 * not need liburing headers on their include path. */

/* ============================================================================
 * MEMENTO INTEGRATION
 * ============================================================================ */
#ifndef MEMENTO_IMPLEMENTATION
typedef struct memento_arena_s memento_arena_t;
typedef struct memento_thread_heap_s memento_thread_heap_t;
#endif

/* ============================================================================
 * BUFFER - Memento-backed dynamic buffer
 * ============================================================================ */
#ifndef BUF_MALLOC
#define BUF_MALLOC(size) ((char*)memento_thread_heap_alloc(memento_thread_heap_get(), size))
#endif
#ifndef BUF_FREE
#define BUF_FREE(ptr, size) memento_thread_heap_free(memento_thread_heap_get(), ptr, size)
#endif

struct buf {
    char  *data;
    size_t len;
    size_t cap;
};

bool buf_append(struct buf *SUSPENDERS_RESTRICT buf, const char *SUSPENDERS_RESTRICT data, ssize_t len);
bool buf_append_byte(struct buf *SUSPENDERS_RESTRICT buf, char ch);
void buf_clear(struct buf *SUSPENDERS_RESTRICT buf);

/* ============================================================================
 * SOCKET PORTABILITY
 * ============================================================================ */
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    typedef SOCKET suspenders_sock_t;
    #define SUSPENDERS_INVALID_SOCK INVALID_SOCKET
    #define SUSPENDERS_SOCK_ERRNO    WSAGetLastError()
    #define SUSPENDERS_EINPROGRESS   WSAEINPROGRESS
    #define SUSPENDERS_EWOULDBLOCK   WSAEWOULDBLOCK
    #define SUSPENDERS_EAGAIN        WSAEWOULDBLOCK
#else
    typedef int suspenders_sock_t;
    #define SUSPENDERS_INVALID_SOCK -1
    #define SUSPENDERS_SOCK_ERRNO    errno
    #define SUSPENDERS_EINPROGRESS   EINPROGRESS
    #define SUSPENDERS_EWOULDBLOCK   EWOULDBLOCK
    #define SUSPENDERS_EAGAIN        EAGAIN
#endif

/* ============================================================================
 * SUSPENDERS CORE - Ultra-fast stackful coroutines
 * ============================================================================ */
#define SUSPENDERS_STACK_SIZE  (1UL << 20)  /* 1MB default stack */

/* ============================================================================
 * ERROR CODES - returned by fallible APIs and mirrored in suspenders_errno
 * ============================================================================ */
enum {
    SUSPENDERS_OK        = 0,    /* Success */
    SUSPENDERS_ERROR     = -1,   /* System error (check errno) */
    SUSPENDERS_INVAL     = -2,   /* Invalid argument */
    SUSPENDERS_NOMEM     = -3,   /* Out of memory */
    SUSPENDERS_TIMEDOUT  = -4,   /* Deadline expired */
    SUSPENDERS_CANCELED  = -5,   /* Operation canceled */
    SUSPENDERS_CLOSED    = -6,   /* Channel closed */
    SUSPENDERS_EMPTY     = -7,   /* Channel empty (try_recv) */
    SUSPENDERS_FULL      = -8,   /* Channel full (try_send) */
    SUSPENDERS_BUSY      = -9,   /* Resource busy (trylock) */
    SUSPENDERS_NOTINIT   = -10,  /* Runtime not initialized */
    SUSPENDERS_NOTFOUND  = -11,  /* No such transport/resource */
    SUSPENDERS_PERM      = -12   /* Operation not permitted in this context */
};

typedef enum {
    SUSPENDERS_QOS_REALTIME = 0,
    SUSPENDERS_QOS_HIGH     = 1,
    SUSPENDERS_QOS_NORMAL   = 2,
    SUSPENDERS_QOS_LOW      = 3,
    SUSPENDERS_QOS_COUNT    = 4
} suspenders_qos_t;

typedef enum {
    SUSPENDERS_STATE_READY,
    SUSPENDERS_STATE_RUNNING,
    SUSPENDERS_STATE_SUSPENDED,
    SUSPENDERS_STATE_DONE
} suspenders_state_t;

/* Forward declarations */
struct suspenders_cr_s;
struct suspenders_backend_s;
struct suspenders_hose_s;
struct suspenders_wq_list_s;
struct suspenders_ticket_lock_s;

/* Context structure - packed for minimal cache footprint */
#if SUSPENDERS_PLATFORM_WINDOWS

typedef struct {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) void *fiber;
    void (*func)(void*);
    void *arg;
    struct suspenders_cr_s *cr;
} suspenders_ctx_t;

#elif defined(SUSPENDERS_ARCH_X86_64)

typedef struct {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) void *rip;
    void *rsp;
    void *rbp;
    void *rbx;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
} suspenders_ctx_t;

#elif defined(SUSPENDERS_ARCH_AARCH64)

typedef struct {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) void *x[12];   /* x19-x30 */
    void *sp;
    void *lr;
    void *d[8];    /* d8-d15 (NEON/VFP callee-saved) */
} suspenders_ctx_t;

#else
    #error "libsuspenders: only x86_64 and aarch64 supported"
#endif

/* Work queue node for blocking operations (channels, mutexes, conds, ...).
 * status holds SUSPENDERS_OK / SUSPENDERS_CANCELED / SUSPENDERS_TIMEDOUT
 * once the wait is decided; wait_list/wait_lock let cancellation and
 * deadline timers unlink a parked waiter from whatever queue it sits on. */
typedef struct suspenders_wq_node_s {
    struct suspenders_cr_s *cr;
    void       *data_ptr;
    struct suspenders_wq_node_s *next;
    int         status;
    struct suspenders_wq_list_s     *wait_list;
    struct suspenders_ticket_lock_s *wait_lock;
} suspenders_wq_node_t;

/* FIFO wait queue */
typedef struct suspenders_wq_list_s {
    suspenders_wq_node_t *head;
    suspenders_wq_node_t *tail;
} suspenders_wq_list_t;

/* Cleanup handler - caller-stack-allocated node, LIFO per coroutine.
 * Remaining handlers run automatically when the coroutine exits. */
typedef struct suspenders_cleanup_s {
    void (*fn)(void*);
    void  *arg;
    struct suspenders_cleanup_s *next;
} suspenders_cleanup_t;

/* Timer - min-heap-ordered one-shot or repeating callback. Public timers
 * are created with suspenders_timer_create and must be released with
 * suspenders_timer_cancel (whether or not they have fired). The struct is
 * exposed so deadline timers can be embedded without allocation. */
typedef struct suspenders_timer_s {
    uint64_t deadline_ns;
    int      period_ms;
    bool     repeat;
    bool     active;          /* currently armed in the timer heap */
    bool     heap_allocated;  /* created by suspenders_timer_create */
    void   (*cb)(void*);
    void    *arg;
} suspenders_timer_t;

/* Coroutine structure - cache line aligned (the asm context switch requires
 * ctx at offset 0; see static asserts below). The cr_t itself lives inside
 * its stack arena so the 64-byte alignment is honored regardless of the
 * heap's natural alignment. */
typedef struct suspenders_cr_s {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) suspenders_ctx_t ctx;
    suspenders_state_t    state;
    suspenders_atomic_int effective_qos;
    int              base_qos;
    memento_arena_t *arena;       /* Stack arena (also holds this struct) */
    suspenders_wq_node_t  wq_node;
    int32_t          io_result;   /* Result from last I/O op */
    struct suspenders_cr_s *next;      /* Ready queue linkage */
    /* Identity & introspection */
    uint64_t         id;
    char             name[32];
    size_t           stack_size;
    /* Cancellation, deadline, cleanup */
    suspenders_atomic_int cancel_requested;
    suspenders_cleanup_t *cleanup_head;
    suspenders_timer_t    deadline_timer;   /* armed by suspenders_deadline */
    bool             deadline_armed;
    /* Ownership (multi-worker scheduling lands in a later phase) */
    memento_thread_heap_t *owner_heap;
    void            *worker;
} suspenders_cr_t;

/* Ticket lock - strict FIFO ordering */
typedef struct suspenders_ticket_lock_s {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) suspenders_atomic_u32 next_ticket;
    suspenders_atomic_u32 now_serving;
} suspenders_ticket_lock_t;

static inline void suspenders_ticket_init(suspenders_ticket_lock_t *l) {
    suspenders_atomic_init(l->next_ticket, 0);
    suspenders_atomic_init(l->now_serving, 0);
}

static inline void suspenders_ticket_lock(suspenders_ticket_lock_t *l) {
    uint32_t my_ticket = suspenders_atomic_fetch_add(l->next_ticket, 1,
                                                    SUSPENDERS_MEMORY_ORDER_RELAXED);
    while (suspenders_atomic_load(l->now_serving, SUSPENDERS_MEMORY_ORDER_ACQUIRE) != my_ticket) {
#if defined(SUSPENDERS_ARCH_X86_64)
        __builtin_ia32_pause();
#elif defined(SUSPENDERS_ARCH_AARCH64)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
}

static inline void suspenders_ticket_unlock(suspenders_ticket_lock_t *l) {
    uint32_t current = suspenders_atomic_load(l->now_serving, SUSPENDERS_MEMORY_ORDER_RELAXED);
    suspenders_atomic_store(l->now_serving, current + 1, SUSPENDERS_MEMORY_ORDER_RELEASE);
}

/* Channel - zero-copy rendezvous communication (FIFO waiters) */
typedef struct {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) size_t elem_sz;
    size_t buf_sz;
    suspenders_atomic_size_t count;
    suspenders_wq_list_t senders;
    suspenders_wq_list_t receivers;
    suspenders_ticket_lock_t lock;
} suspenders_chan_t;

/* Coroutine mutex - FIFO handoff with single-level priority inheritance.
 * Value type: zero-init or suspenders_mutex_init before use. */
typedef struct {
    suspenders_ticket_lock_t lock;
    struct suspenders_cr_s *owner;
    suspenders_wq_list_t waiters;
} suspenders_mutex_t;

/* Coroutine read-write lock - FIFO with reader batching (a waiting writer
 * blocks later readers, preventing writer starvation). */
typedef struct {
    suspenders_ticket_lock_t lock;
    int  readers;             /* active readers */
    bool writer;              /* active writer */
    suspenders_wq_list_t waiters;  /* mixed FIFO; data_ptr tags writers */
} suspenders_rwlock_t;

/* Coroutine condition variable */
typedef struct {
    suspenders_ticket_lock_t lock;
    suspenders_wq_list_t waiters;
} suspenders_cond_t;

/* Coroutine wait group */
typedef struct {
    suspenders_ticket_lock_t lock;
    int count;
    suspenders_wq_list_t waiters;
} suspenders_waitgroup_t;

/* Layout invariants the hand-rolled context switch depends on */
SUSPENDERS_STATIC_ASSERT(offsetof(suspenders_cr_t, ctx) == 0,
                         "asm context switch requires ctx at offset 0 of suspenders_cr_t");
SUSPENDERS_STATIC_ASSERT(sizeof(suspenders_cr_t) % SUSPENDERS_CACHELINE == 0,
                         "suspenders_cr_t must be a whole number of cache lines");
SUSPENDERS_STATIC_ASSERT(sizeof(suspenders_ticket_lock_t) % SUSPENDERS_CACHELINE == 0,
                         "suspenders_ticket_lock_t must be a whole number of cache lines");
#if defined(SUSPENDERS_ARCH_X86_64) && !defined(SUSPENDERS_PLATFORM_WINDOWS)
SUSPENDERS_STATIC_ASSERT(offsetof(suspenders_ctx_t, rsp) == 8 &&
                         offsetof(suspenders_ctx_t, r15) == 56,
                         "x86_64 asm uses fixed 8-byte slot offsets into suspenders_ctx_t");
#elif defined(SUSPENDERS_ARCH_AARCH64) && !defined(SUSPENDERS_PLATFORM_WINDOWS)
SUSPENDERS_STATIC_ASSERT(offsetof(suspenders_ctx_t, sp) == 96 &&
                         offsetof(suspenders_ctx_t, lr) == 104 &&
                         offsetof(suspenders_ctx_t, d) == 112,
                         "aarch64 asm uses fixed stp/ldp pair offsets into suspenders_ctx_t");
#endif

/* ============================================================================
 * EVENT BACKEND
 * ============================================================================ */
typedef struct suspenders_backend_s suspenders_backend_t;

/* ============================================================================
 * TRANSPORT ABSTRACTION
 * ============================================================================ */
typedef struct suspenders_hose_s suspenders_hose_t;

typedef struct suspenders_transport_ops {
    const char *scheme;
    bool (*dial)(suspenders_hose_t *h, const char *host, int port);
    bool (*listen)(suspenders_hose_t *h, const char *host, int port);
    bool (*accept)(suspenders_hose_t *listener, suspenders_hose_t *client);
    ssize_t (*read)(suspenders_hose_t *h, void *dest, size_t len);
    ssize_t (*write)(suspenders_hose_t *h, const void *src, size_t len);
    ssize_t (*recvfrom)(suspenders_hose_t *h, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen);
    ssize_t (*sendto)(suspenders_hose_t *h, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen);
    void (*close)(suspenders_hose_t *h);
} suspenders_transport_ops_t;

#ifdef __cplusplus
extern "C" {
#endif

bool suspenders_transport_register(const suspenders_transport_ops_t *ops);
const suspenders_transport_ops_t* suspenders_transport_find(const char *scheme);

/* ============================================================================
 * HOSE - Async I/O abstraction
 * ============================================================================ */
#define SUSPENDERS_HOSE_ROLE_READER   (1 << 0)
#define SUSPENDERS_HOSE_ROLE_WRITER   (1 << 1)
#define SUSPENDERS_HOSE_ROLE_DIALER   (1 << 2)
#define SUSPENDERS_HOSE_ROLE_LISTENER (1 << 3)

typedef enum {
    SUSPENDERS_HOSE_PROTO_UNKNOWN,
    SUSPENDERS_HOSE_PROTO_FILE,
    SUSPENDERS_HOSE_PROTO_TCP,
    SUSPENDERS_HOSE_PROTO_UDP,
    SUSPENDERS_HOSE_PROTO_UNIX,
    SUSPENDERS_HOSE_PROTO_PIPE,
    SUSPENDERS_HOSE_PROTO_TTY,
} suspenders_hose_protocol_t;

struct suspenders_hose_s {
    uint8_t          roles;
    suspenders_hose_protocol_t  protocol;
    suspenders_sock_t fd;
    struct buf      *buffer;
    const suspenders_transport_ops_t *transport;
};

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/* Thread-local detailed error of the most recent failing call */
extern SUSPENDERS_TLS int suspenders_errno;
const char *suspenders_strerror(int err);

/* Returns SUSPENDERS_OK or a negative error code (never aborts). */
int  suspenders_init(unsigned num_workers, unsigned queue_hint);
void suspenders_run(void);
void suspenders_shutdown(void);

suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos);
suspenders_cr_t* suspenders_go(void (*func)(void*), void *arg);  /* spawn at NORMAL */
void suspenders_yield(void);
void suspenders_suspend(void);
void suspenders_resume(suspenders_cr_t *cr);
void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos);

/* Identity & introspection (return 0/NULL/"" outside a coroutine) */
suspenders_cr_t* suspenders_self(void);
uint64_t    suspenders_getid(void);
int         suspenders_setname(const char *name);
const char* suspenders_getname(void);
size_t      suspenders_stack_size(void);

/* Cancellation. suspenders_cancel requests cancellation: a blocked target
 * wakes with SUSPENDERS_CANCELED; otherwise its next blocking call fails.
 * Delivery consumes the request. suspenders_canceled peeks the flag. */
int  suspenders_cancel(suspenders_cr_t *cr);
bool suspenders_canceled(void);

/* Self-cancel deadline for the current coroutine (absolute, from
 * suspenders_now_ns). Blocking calls past the deadline fail with
 * SUSPENDERS_TIMEDOUT. deadline_ns == 0 disarms. */
int suspenders_deadline(uint64_t deadline_ns);

/* Cleanup handlers - LIFO, run when the coroutine exits. Nodes are
 * caller-stack-allocated, so they must live in stack frames that are still
 * alive at exit: balance push/pop within a scope, or terminate with
 * suspenders_exit() while the pushing frames are live (pthread rules). */
void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void*), void *arg);
void suspenders_cleanup_pop(int execute);

/* Terminate the current coroutine, running remaining cleanup handlers.
 * No-op outside a coroutine. */
void suspenders_exit(void);

/* Timers and monotonic clock. Timers created here must be released with
 * suspenders_timer_cancel, whether or not they have fired. */
suspenders_timer_t* suspenders_timer_create(int ms, bool repeat,
                                            void (*cb)(void*), void *arg);
void suspenders_timer_cancel(suspenders_timer_t *t);
uint64_t suspenders_now_ns(void);

/* Sleep - returns SUSPENDERS_OK, or SUSPENDERS_CANCELED / SUSPENDERS_TIMEDOUT
 * if canceled or past a suspenders_deadline. Coroutine context only. */
int suspenders_sleep_ns(uint64_t ns);
int suspenders_sleep_dl(uint64_t deadline_ns);

/* Coroutine synchronization primitives. All blocking calls are
 * cancel/deadline aware and return SUSPENDERS_OK or an error code.
 * _dl variants take an absolute deadline (0 = wait forever). */
int suspenders_mutex_init(suspenders_mutex_t *m);
int suspenders_mutex_lock(suspenders_mutex_t *m);
int suspenders_mutex_lock_dl(suspenders_mutex_t *m, uint64_t deadline_ns);
int suspenders_mutex_trylock(suspenders_mutex_t *m);
int suspenders_mutex_unlock(suspenders_mutex_t *m);

int suspenders_rwlock_init(suspenders_rwlock_t *rw);
int suspenders_rwlock_rdlock(suspenders_rwlock_t *rw);
int suspenders_rwlock_rdlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns);
int suspenders_rwlock_tryrdlock(suspenders_rwlock_t *rw);
int suspenders_rwlock_wrlock(suspenders_rwlock_t *rw);
int suspenders_rwlock_wrlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns);
int suspenders_rwlock_trywrlock(suspenders_rwlock_t *rw);
int suspenders_rwlock_unlock(suspenders_rwlock_t *rw);

int suspenders_cond_init(suspenders_cond_t *c);
int suspenders_cond_wait(suspenders_cond_t *c, suspenders_mutex_t *m);
int suspenders_cond_wait_dl(suspenders_cond_t *c, suspenders_mutex_t *m, uint64_t deadline_ns);
int suspenders_cond_signal(suspenders_cond_t *c);
int suspenders_cond_broadcast(suspenders_cond_t *c);

int suspenders_waitgroup_init(suspenders_waitgroup_t *wg);
int suspenders_waitgroup_add(suspenders_waitgroup_t *wg, int delta);
int suspenders_waitgroup_done(suspenders_waitgroup_t *wg);
int suspenders_waitgroup_wait(suspenders_waitgroup_t *wg);
int suspenders_waitgroup_wait_dl(suspenders_waitgroup_t *wg, uint64_t deadline_ns);

/* Dispatch queue and coroutine pool */
typedef struct suspenders_dispatch_queue_s suspenders_dispatch_queue_t;
typedef struct suspenders_pool_s suspenders_pool_t;

suspenders_dispatch_queue_t* suspenders_dispatch_queue_create(const char *label,
                                                               suspenders_qos_t qos);
void suspenders_dispatch_queue_destroy(suspenders_dispatch_queue_t *q);
void suspenders_dispatch_async(suspenders_dispatch_queue_t *q,
                               void (*fn)(void*), void *arg);
void suspenders_dispatch_barrier_async(suspenders_dispatch_queue_t *q,
                                       void (*fn)(void*), void *arg);

suspenders_pool_t* suspenders_pool_create(unsigned nworkers,
                                          suspenders_qos_t qos);
void suspenders_pool_destroy(suspenders_pool_t *pool);
void suspenders_pool_submit(suspenders_pool_t *pool,
                            void (*fn)(void*), void *arg);

suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
void suspenders_chan_destroy(suspenders_chan_t *ch);
bool suspenders_chan_send(suspenders_chan_t *ch, void *val);
bool suspenders_chan_recv(suspenders_chan_t *ch, void *out);

/* Hose API - transport agnostic */
void  suspenders_hose_init(suspenders_hose_t *d, struct buf *b);
bool  suspenders_hose_dial(suspenders_hose_t *d, const char *uri);
bool  suspenders_hose_listen(suspenders_hose_t *d, const char *uri);
bool  suspenders_hose_accept(suspenders_hose_t *d, suspenders_hose_t *client);
ssize_t suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len);
ssize_t suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len);
ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen);
ssize_t suspenders_hose_sendto(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen);
void  suspenders_hose_close(suspenders_hose_t *d);

#if SUSPENDERS_BACKEND_IOURING
void suspenders_writev_async(int fd, struct iovec *iovs, int count);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBSUSPENDERS_H */


/* ============================================================================
 * IMPLEMENTATION - Define SUSPENDERS_IMPLEMENTATION in ONE .c file
 * ============================================================================ */
#ifdef SUSPENDERS_IMPLEMENTATION

/* The implementation is C11 code. When compiled as C++ (single-TU
 * convenience), silence pedantic warnings about C idioms such as designated
 * initializers, {0} aggregate init, and flexible array members. */
#ifdef __cplusplus
  #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc99-extensions"
    #pragma clang diagnostic ignored "-Wc++20-designator"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
  #elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  #endif
#endif

#include <stdlib.h>
#include <stdio.h>

#if SUSPENDERS_BACKEND_IOURING
    #include <liburing.h>
#endif

/* Thread-local detailed error */
SUSPENDERS_TLS int suspenders_errno = SUSPENDERS_OK;

const char *suspenders_strerror(int err) {
    switch (err) {
    case SUSPENDERS_OK:       return "ok";
    case SUSPENDERS_ERROR:    return "system error";
    case SUSPENDERS_INVAL:    return "invalid argument";
    case SUSPENDERS_NOMEM:    return "out of memory";
    case SUSPENDERS_TIMEDOUT: return "deadline expired";
    case SUSPENDERS_CANCELED: return "operation canceled";
    case SUSPENDERS_CLOSED:   return "channel closed";
    case SUSPENDERS_EMPTY:    return "channel empty";
    case SUSPENDERS_FULL:     return "channel full";
    case SUSPENDERS_BUSY:     return "resource busy";
    case SUSPENDERS_NOTINIT:  return "runtime not initialized";
    case SUSPENDERS_NOTFOUND: return "not found";
    case SUSPENDERS_PERM:     return "operation not permitted";
    default:                  return "unknown error";
    }
}

/* Thread-local state. The main (scheduler) coroutine lives in static TLS
 * storage: naturally 64-byte aligned and immune to allocator lifetime. */
static SUSPENDERS_TLS suspenders_cr_t *suspenders_running = NULL;
static SUSPENDERS_TLS suspenders_cr_t suspenders_main_cr_storage;
static SUSPENDERS_TLS suspenders_cr_t *suspenders_main_cr = NULL;
static SUSPENDERS_TLS suspenders_cr_t *ready_queue_heads[SUSPENDERS_QOS_COUNT] = {0};
static SUSPENDERS_TLS suspenders_cr_t *ready_queue_tails[SUSPENDERS_QOS_COUNT] = {0};
static SUSPENDERS_TLS int suspenders_initialized = 0;
static suspenders_atomic_int active_coroutines = 0;
static suspenders_atomic_uintptr_t zombie_head = 0;
static suspenders_atomic_uintptr_t suspenders_next_id = 1;

/* wq_node.status while a waiter is parked; decided waits carry an error
 * code (SUSPENDERS_OK / _CANCELED / _TIMEDOUT), all <= 0. */
#define S_WQ_WAITING 1

/* Timer internals (definitions in the TIMERS section below) */
static bool s_timer_arm(suspenders_timer_t *t);
static void s_timer_disarm(suspenders_timer_t *t);

/* Backend instance */
static SUSPENDERS_TLS struct suspenders_backend_s *suspenders_backend = NULL;

/* Transport registry */
static const suspenders_transport_ops_t *suspenders_transport_registry[16];
static int suspenders_transport_count = 0;

#ifdef SUSPENDERS_PLATFORM_WINDOWS
static void *suspenders_main_fiber = NULL;
#endif

/* ============================================================================
 * BUFFER IMPLEMENTATION
 * ============================================================================ */
bool buf_append(struct buf *SUSPENDERS_RESTRICT buf, const char *SUSPENDERS_RESTRICT data, ssize_t len) {
    if (len < 0) len = (ssize_t)strlen(data);
    size_t needed = buf->len + (size_t)len;
    if (SUSPENDERS_UNLIKELY(needed >= buf->cap)) {
        size_t cap = buf->cap ? buf->cap : 64;
        while (cap <= needed) cap *= 2;
        char *ndata = BUF_MALLOC(cap + 1);
        if (SUSPENDERS_UNLIKELY(!ndata)) return false;
        if (buf->data) {
            memcpy(ndata, buf->data, buf->len);
            BUF_FREE(buf->data, buf->cap + 1);
        }
        buf->data = ndata;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, (size_t)len);
    buf->len = needed;
    buf->data[buf->len] = '\0';
    return true;
}

bool buf_append_byte(struct buf *SUSPENDERS_RESTRICT buf, char ch) {
    if (SUSPENDERS_LIKELY(buf->len < buf->cap)) {
        buf->data[buf->len++] = ch;
        buf->data[buf->len] = '\0';
        return true;
    }
    return buf_append(buf, &ch, 1);
}

void buf_clear(struct buf *SUSPENDERS_RESTRICT buf) {
    if (buf->data) BUF_FREE(buf->data, buf->cap + 1);
    memset(buf, 0, sizeof(*buf));
}

/* ============================================================================
 * SOCKET PORTABILITY IMPLEMENTATION
 * ============================================================================ */
static inline suspenders_sock_t suspenders_socket(int domain, int type, int protocol) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    return socket(domain, type, protocol);
#else
    return socket(domain, type, protocol);
#endif
}

static inline int suspenders_close_socket(suspenders_sock_t fd) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    return closesocket(fd);
#else
    return close(fd);
#endif
}

static inline bool suspenders_set_nonblocking(suspenders_sock_t fd) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

static inline ssize_t suspenders_sock_recv(suspenders_sock_t fd, void *buf, size_t len, int flags) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    return recv(fd, (char*)buf, (int)len, flags);
#else
    return recv(fd, buf, len, flags);
#endif
}

static inline ssize_t suspenders_sock_send(suspenders_sock_t fd, const void *buf, size_t len, int flags) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    return send(fd, (const char*)buf, (int)len, flags);
#else
    return send(fd, buf, len, flags);
#endif
}

static inline int suspenders_sock_errno(void) {
    return SUSPENDERS_SOCK_ERRNO;
}

/* ============================================================================
 * COROUTINE EXIT HANDLER
 * ============================================================================ */
#ifdef __cplusplus
extern "C"
#endif
void suspenders_cr_exit(void);

/* ============================================================================
 * CONTEXT SWITCH
 * ============================================================================ */

#if SUSPENDERS_PLATFORM_WINDOWS

static void WINAPI suspenders_fiber_trampoline(void *param) {
    suspenders_ctx_t *ctx = (suspenders_ctx_t*)param;
    suspenders_running = ctx->cr;
    ctx->func(ctx->arg);
    suspenders_cr_exit();
    __builtin_unreachable();
}

static inline void suspenders_ctx_switch(suspenders_ctx_t *old, suspenders_ctx_t *new_ctx) {
    (void)old;
    SwitchToFiber(new_ctx->fiber);
}

static inline void suspenders_make_context(suspenders_ctx_t *ctx, void *stack_base, size_t stack_size,
                                    void (*func)(void*), void *arg) {
    (void)stack_base;
    ctx->func = func;
    ctx->arg = arg;
    ctx->cr = NULL;
    ctx->fiber = CreateFiber(stack_size, suspenders_fiber_trampoline, ctx);
}

#elif defined(SUSPENDERS_ARCH_X86_64)

__asm__(
    ".text\n"
#ifdef __MACH__
    ".globl __suspenders_asm_entry\n"
    "__suspenders_asm_entry:\n"
#else
    ".globl _suspenders_asm_entry\n"
    ".type _suspenders_asm_entry @function\n"
    ".hidden _suspenders_asm_entry\n"
    "_suspenders_asm_entry:\n"
#endif
    "  movq %r13, %rdi\n"
    "  jmpq *%r12\n"
#ifndef __MACH__
    ".size _suspenders_asm_entry, .-_suspenders_asm_entry\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __MACH__
    ".globl __suspenders_asm_switch\n"
    "__suspenders_asm_switch:\n"
#else
    ".globl _suspenders_asm_switch\n"
    ".type _suspenders_asm_switch @function\n"
    ".hidden _suspenders_asm_switch\n"
    "_suspenders_asm_switch:\n"
#endif
    "  leaq .Lret(%rip), %rax\n"
    "  movq %rax, (%rdi)\n"
    "  movq %rsp, 8(%rdi)\n"
    "  movq %rbp, 16(%rdi)\n"
    "  movq %rbx, 24(%rdi)\n"
    "  movq %r12, 32(%rdi)\n"
    "  movq %r13, 40(%rdi)\n"
    "  movq %r14, 48(%rdi)\n"
    "  movq %r15, 56(%rdi)\n"
    "  movq 56(%rsi), %r15\n"
    "  movq 48(%rsi), %r14\n"
    "  movq 40(%rsi), %r13\n"
    "  movq 32(%rsi), %r12\n"
    "  movq 24(%rsi), %rbx\n"
    "  movq 16(%rsi), %rbp\n"
    "  movq 8(%rsi), %rsp\n"
    "  jmpq *(%rsi)\n"
    ".Lret:\n"
    "  ret\n"
#ifndef __MACH__
    ".size _suspenders_asm_switch, .-_suspenders_asm_switch\n"
#endif
);

#ifdef __cplusplus
extern "C"
#endif
void _suspenders_asm_switch(suspenders_ctx_t *old, suspenders_ctx_t *new_ctx);
#ifdef __cplusplus
extern "C"
#endif
void _suspenders_asm_entry(void);

static inline void suspenders_make_context(suspenders_ctx_t *ctx, void *stack_base, size_t stack_size,
                                    void (*func)(void*), void *arg) {
    stack_size = stack_size - 128; /* Reserve Red Zone */
    void **stack_high = (void**)((char*)stack_base + stack_size - sizeof(void*));
    stack_high[0] = (void*)suspenders_cr_exit;
    ctx->rip = (void*)_suspenders_asm_entry;
    ctx->rsp = stack_high;
    ctx->r12 = (void*)func;
    ctx->r13 = (void*)arg;
    ctx->rbx = ctx->rbp = ctx->r14 = ctx->r15 = 0;
}

static inline void suspenders_ctx_switch(suspenders_ctx_t *old, suspenders_ctx_t *new_ctx) {
    _suspenders_asm_switch(old, new_ctx);
}

#elif defined(SUSPENDERS_ARCH_AARCH64)

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __suspenders_asm_entry\n"
    "__suspenders_asm_entry:\n"
#else
    ".globl _suspenders_asm_entry\n"
    ".type _suspenders_asm_entry #function\n"
    ".hidden _suspenders_asm_entry\n"
    "_suspenders_asm_entry:\n"
#endif
    "  mov x0, x19\n"
    "  blr x20\n"
    "  b suspenders_cr_exit\n"
#ifndef __APPLE__
    ".size _suspenders_asm_entry, .-_suspenders_asm_entry\n"
#endif
);

__asm__(
    ".text\n"
#ifdef __APPLE__
    ".globl __suspenders_asm_switch\n"
    "__suspenders_asm_switch:\n"
#else
    ".globl _suspenders_asm_switch\n"
    ".type _suspenders_asm_switch #function\n"
    ".hidden _suspenders_asm_switch\n"
    "_suspenders_asm_switch:\n"
#endif
    "  mov x10, sp\n"
    "  mov x11, x30\n"
    "  stp x19, x20, [x0, #(0*16)]\n"
    "  stp x21, x22, [x0, #(1*16)]\n"
    "  stp d8, d9, [x0, #(7*16)]\n"
    "  stp x23, x24, [x0, #(2*16)]\n"
    "  stp d10, d11, [x0, #(8*16)]\n"
    "  stp x25, x26, [x0, #(3*16)]\n"
    "  stp d12, d13, [x0, #(9*16)]\n"
    "  stp x27, x28, [x0, #(4*16)]\n"
    "  stp d14, d15, [x0, #(10*16)]\n"
    "  stp x29, x30, [x0, #(5*16)]\n"
    "  stp x10, x11, [x0, #(6*16)]\n"
    "  ldp x19, x20, [x1, #(0*16)]\n"
    "  ldp x21, x22, [x1, #(1*16)]\n"
    "  ldp d8, d9, [x1, #(7*16)]\n"
    "  ldp x23, x24, [x1, #(2*16)]\n"
    "  ldp d10, d11, [x1, #(8*16)]\n"
    "  ldp x25, x26, [x1, #(3*16)]\n"
    "  ldp d12, d13, [x1, #(9*16)]\n"
    "  ldp x27, x28, [x1, #(4*16)]\n"
    "  ldp d14, d15, [x1, #(10*16)]\n"
    "  ldp x29, x30, [x1, #(5*16)]\n"
    "  ldp x10, x11, [x1, #(6*16)]\n"
    "  mov sp, x10\n"
    "  br x11\n"
#ifndef __APPLE__
    ".size _suspenders_asm_switch, .-_suspenders_asm_switch\n"
#endif
);

#ifdef __cplusplus
extern "C"
#endif
void _suspenders_asm_switch(suspenders_ctx_t *old, suspenders_ctx_t *new_ctx);
#ifdef __cplusplus
extern "C"
#endif
void _suspenders_asm_entry(void);

static inline void suspenders_make_context(suspenders_ctx_t *ctx, void *stack_base, size_t stack_size,
                                    void (*func)(void*), void *arg) {
    ctx->x[0] = arg;
    ctx->x[1] = (void*)func;
    ctx->x[2] = (void*)0xdeaddeaddeaddead;
    ctx->sp = (void*)((char*)stack_base + stack_size);
    ctx->lr = (void*)_suspenders_asm_entry;
    for (int i = 3; i < 12; i++) ctx->x[i] = NULL;
    for (int i = 0; i < 8; i++) ctx->d[i] = NULL;
}

static inline void suspenders_ctx_switch(suspenders_ctx_t *old, suspenders_ctx_t *new_ctx) {
    _suspenders_asm_switch(old, new_ctx);
}

#endif /* Architecture-specific context switch */

/* ============================================================================
 * COROUTINE EXIT HANDLER
 * ============================================================================ */
static inline void suspenders_zombie_enqueue(suspenders_cr_t *cr) {
    uintptr_t old_head;
    do {
        old_head = suspenders_atomic_load(zombie_head, SUSPENDERS_MEMORY_ORDER_RELAXED);
        cr->next = (suspenders_cr_t*)old_head;
    } while (!suspenders_atomic_compare_exchange_weak(zombie_head, old_head, (uintptr_t)cr,
                                                      SUSPENDERS_MEMORY_ORDER_RELEASE,
                                                      SUSPENDERS_MEMORY_ORDER_RELAXED));
}

/* Timer disarm is defined in the TIMERS section; forward-declared above. */
void suspenders_cr_exit(void) {
    suspenders_cr_t *cr = suspenders_running;
    if (cr) {
        /* Run remaining cleanup handlers (LIFO) while still on this stack. */
        while (cr->cleanup_head) {
            suspenders_cleanup_t *h = cr->cleanup_head;
            cr->cleanup_head = h->next;
            if (h->fn) h->fn(h->arg);
        }
        if (cr->deadline_armed) {
            s_timer_disarm(&cr->deadline_timer);
            cr->deadline_armed = false;
        }
        if (cr->state != SUSPENDERS_STATE_DONE) {
            cr->state = SUSPENDERS_STATE_DONE;
            suspenders_atomic_fetch_sub(active_coroutines, 1, SUSPENDERS_MEMORY_ORDER_RELAXED);
        }
        suspenders_zombie_enqueue(cr);
        if (suspenders_main_cr) {
            suspenders_ctx_switch(&cr->ctx, &suspenders_main_cr->ctx);
        }
    }
    __builtin_unreachable();
}

/* ============================================================================
 * SCHEDULER - Ready queue and context switching
 * ============================================================================ */
static inline int suspenders_clamp_qos(int qos) {
    if (qos < 0) return 0;
    if (qos >= SUSPENDERS_QOS_COUNT) return SUSPENDERS_QOS_COUNT - 1;
    return qos;
}

static void suspenders_ready_enqueue(suspenders_cr_t *cr) {
    cr->next = NULL;
    cr->state = SUSPENDERS_STATE_READY;
    int qos = suspenders_clamp_qos(cr->effective_qos);
    if (ready_queue_tails[qos]) {
        ready_queue_tails[qos]->next = cr;
    } else {
        ready_queue_heads[qos] = cr;
    }
    ready_queue_tails[qos] = cr;
}

static suspenders_cr_t* suspenders_ready_dequeue(void) {
    for (int qos = 0; qos < SUSPENDERS_QOS_COUNT; qos++) {
        suspenders_cr_t *cr = ready_queue_heads[qos];
        if (cr) {
            ready_queue_heads[qos] = cr->next;
            if (!ready_queue_heads[qos]) {
                ready_queue_tails[qos] = NULL;
            }
            cr->next = NULL;
            return cr;
        }
    }
    return NULL;
}

static bool suspenders_ready_queue_empty(void) {
    for (int qos = 0; qos < SUSPENDERS_QOS_COUNT; qos++) {
        if (ready_queue_heads[qos]) return false;
    }
    return true;
}

/* Removed suspenders_switch_to - preemption now yields to main scheduler */

static suspenders_cr_t* s_main_cr_get(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_main_cr)) {
        suspenders_cr_t *m = &suspenders_main_cr_storage;
        memset(m, 0, sizeof(*m));
        m->state = SUSPENDERS_STATE_RUNNING;
        suspenders_atomic_init(m->effective_qos, SUSPENDERS_QOS_NORMAL);
        m->base_qos = SUSPENDERS_QOS_NORMAL;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        if (!suspenders_main_fiber) {
            suspenders_main_fiber = ConvertThreadToFiber(NULL);
        }
        m->ctx.fiber = suspenders_main_fiber;
#endif
        suspenders_main_cr = m;
        suspenders_running = m;
    }
    return suspenders_main_cr;
}

/* ============================================================================
 * WAIT PROTOCOL - shared by channels, sync primitives, sleep, deadlines
 *
 * A blocked coroutine parks its embedded wq_node on a FIFO list with
 * status == S_WQ_WAITING. Whoever decides the wait (peer/unlock/signal,
 * cancellation, or a deadline timer) unlinks the node under the owning
 * structure's ticket lock, writes the final status, and resumes the
 * coroutine. The waiter loops on suspend until the status is decided, so
 * stray resumes are harmless.
 * ============================================================================ */
static inline void s_wq_push(suspenders_wq_list_t *l, suspenders_wq_node_t *n) {
    n->next = NULL;
    if (l->tail) l->tail->next = n;
    else l->head = n;
    l->tail = n;
}

static inline suspenders_wq_node_t* s_wq_pop(suspenders_wq_list_t *l) {
    suspenders_wq_node_t *n = l->head;
    if (n) {
        l->head = n->next;
        if (!l->head) l->tail = NULL;
        n->next = NULL;
    }
    return n;
}

static void s_wq_remove(suspenders_wq_list_t *l, suspenders_wq_node_t *n) {
    suspenders_wq_node_t **pp = &l->head, *prev = NULL;
    while (*pp) {
        if (*pp == n) {
            *pp = n->next;
            if (l->tail == n) l->tail = prev;
            n->next = NULL;
            return;
        }
        prev = *pp;
        pp = &(*pp)->next;
    }
}

/* Park the running coroutine's wq_node on a list (caller holds `lock`). */
static void s_wait_park(suspenders_wq_list_t *list, suspenders_ticket_lock_t *lock,
                        void *data) {
    suspenders_wq_node_t *node = &suspenders_running->wq_node;
    node->cr = suspenders_running;
    node->data_ptr = data;
    node->status = S_WQ_WAITING;
    node->wait_list = list;
    node->wait_lock = lock;
    s_wq_push(list, node);
}

/* Dequeue a waiter for a normal wake (caller holds the list's lock and
 * resumes n->cr after releasing it). */
static inline void s_wait_grant(suspenders_wq_node_t *n, int status) {
    n->wait_list = NULL;
    n->status = status;
}

/* Suspend until the wait is decided; returns the final status. Consumes a
 * pending cancel request when it delivered the wake. Call after releasing
 * the structure's lock. */
static int s_wait_block(void) {
    suspenders_wq_node_t *node = &suspenders_running->wq_node;
    while (node->status == S_WQ_WAITING) suspenders_suspend();
    node->wait_list = NULL;
    node->wait_lock = NULL;
    int st = node->status;
    if (st == SUSPENDERS_CANCELED) {
        suspenders_atomic_store(suspenders_running->cancel_requested, 0,
                                SUSPENDERS_MEMORY_ORDER_RELAXED);
    }
    if (st != SUSPENDERS_OK) suspenders_errno = st;
    return st;
}

/* Decide a parked wait from outside (cancel / deadline timer): unlink the
 * node under its structure's lock, write status, resume. Returns false if
 * the coroutine was not parked on a wq_node wait. */
static bool s_waiter_wake(suspenders_cr_t *cr, int status) {
    suspenders_wq_node_t *node = &cr->wq_node;
    if (node->status != S_WQ_WAITING) return false;
    suspenders_ticket_lock_t *lk = node->wait_lock;
    if (lk) suspenders_ticket_lock(lk);
    bool won = false;
    if (node->status == S_WQ_WAITING) {
        if (node->wait_list) {
            s_wq_remove(node->wait_list, node);
            node->wait_list = NULL;
        }
        node->status = status;
        won = true;
    }
    if (lk) suspenders_ticket_unlock(lk);
    if (won) suspenders_resume(cr);
    return won;
}

static void s_wait_deadline_cb(void *arg) {
    s_waiter_wake((suspenders_cr_t*)arg, SUSPENDERS_TIMEDOUT);
}

/* s_wait_block with an optional stack-resident deadline timer. */
static int s_wait_block_dl(uint64_t deadline_ns) {
    suspenders_timer_t t;
    bool armed = false;
    if (deadline_ns) {
        memset(&t, 0, sizeof(t));
        t.deadline_ns = deadline_ns;
        t.cb = s_wait_deadline_cb;
        t.arg = suspenders_running;
        armed = s_timer_arm(&t);
    }
    int st = s_wait_block();
    if (armed) s_timer_disarm(&t);
    return st;
}

/* Pre-block checks for a coroutine about to park: consume a pending cancel,
 * honor an expired suspenders_deadline. Returns 0 to proceed. */
static int s_pre_block(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(suspenders_atomic_exchange(cr->cancel_requested, 0,
                                SUSPENDERS_MEMORY_ORDER_RELAXED) != 0)) {
        suspenders_errno = SUSPENDERS_CANCELED;
        return SUSPENDERS_CANCELED;
    }
    if (SUSPENDERS_UNLIKELY(cr->deadline_armed) &&
        suspenders_now_ns() >= cr->deadline_timer.deadline_ns) {
        suspenders_errno = SUSPENDERS_TIMEDOUT;
        return SUSPENDERS_TIMEDOUT;
    }
    return 0;
}

/* Entry gate for blocking sync APIs: must be a real coroutine. */
static int s_block_begin(suspenders_cr_t **out) {
    suspenders_cr_t *cr = suspenders_running;
    if (SUSPENDERS_UNLIKELY(!cr || cr == suspenders_main_cr)) {
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }
    *out = cr;
    return s_pre_block(cr);
}

/* ============================================================================
 * COROUTINE LIFECYCLE
 * ============================================================================ */
void suspenders_yield(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_running || suspenders_running == suspenders_main_cr)) return;
    suspenders_ready_enqueue(suspenders_running);
    suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
}

void suspenders_suspend(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_running || suspenders_running == suspenders_main_cr)) return;
    if (suspenders_running->state == SUSPENDERS_STATE_RUNNING) {
        suspenders_running->state = SUSPENDERS_STATE_SUSPENDED;
    }
    suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
}

void suspenders_resume(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(!cr || cr->state == SUSPENDERS_STATE_RUNNING || cr->state == SUSPENDERS_STATE_READY)) return;
    suspenders_ready_enqueue(cr);
    if (suspenders_running && suspenders_running != suspenders_main_cr &&
        suspenders_running->effective_qos > cr->effective_qos) {
        suspenders_ready_enqueue(suspenders_running);
        suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
    }
}

void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos) {
    if (SUSPENDERS_UNLIKELY(!target)) return;
    int current = suspenders_atomic_load(target->effective_qos, SUSPENDERS_MEMORY_ORDER_RELAXED);
    while ((int)new_qos < current) {
        if (suspenders_atomic_compare_exchange_weak(target->effective_qos, current, (int)new_qos,
                                                    SUSPENDERS_MEMORY_ORDER_RELAXED,
                                                    SUSPENDERS_MEMORY_ORDER_RELAXED))
            break;
        current = suspenders_atomic_load(target->effective_qos, SUSPENDERS_MEMORY_ORDER_RELAXED);
    }
}

int suspenders_cancel(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(!cr)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_atomic_store(cr->cancel_requested, 1, SUSPENDERS_MEMORY_ORDER_RELEASE);
    if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
        if (!s_waiter_wake(cr, SUSPENDERS_CANCELED)) {
            /* Blocked on I/O or a bare suspend: deliver failure via io_result. */
            cr->io_result = -1;
            suspenders_resume(cr);
        }
    }
    return SUSPENDERS_OK;
}

bool suspenders_canceled(void) {
    suspenders_cr_t *cr = suspenders_running;
    if (!cr) return false;
    return suspenders_atomic_load(cr->cancel_requested,
                                  SUSPENDERS_MEMORY_ORDER_ACQUIRE) != 0;
}

/* ============================================================================
 * IDENTITY, CLEANUP HANDLERS, DEADLINE
 * ============================================================================ */
suspenders_cr_t* suspenders_self(void) {
    suspenders_cr_t *cr = suspenders_running;
    return (cr && cr != suspenders_main_cr) ? cr : NULL;
}

uint64_t suspenders_getid(void) {
    suspenders_cr_t *cr = suspenders_self();
    return cr ? cr->id : 0;
}

int suspenders_setname(const char *name) {
    suspenders_cr_t *cr = suspenders_self();
    if (SUSPENDERS_UNLIKELY(!cr || !name)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    strncpy(cr->name, name, sizeof(cr->name) - 1);
    cr->name[sizeof(cr->name) - 1] = '\0';
    return SUSPENDERS_OK;
}

const char* suspenders_getname(void) {
    suspenders_cr_t *cr = suspenders_self();
    return cr ? cr->name : "";
}

size_t suspenders_stack_size(void) {
    suspenders_cr_t *cr = suspenders_self();
    return cr ? cr->stack_size : 0;
}

void suspenders_cleanup_push(suspenders_cleanup_t *node, void (*fn)(void*), void *arg) {
    suspenders_cr_t *cr = suspenders_self();
    if (SUSPENDERS_UNLIKELY(!cr || !node)) return;
    node->fn = fn;
    node->arg = arg;
    node->next = cr->cleanup_head;
    cr->cleanup_head = node;
}

void suspenders_cleanup_pop(int execute) {
    suspenders_cr_t *cr = suspenders_self();
    if (SUSPENDERS_UNLIKELY(!cr || !cr->cleanup_head)) return;
    suspenders_cleanup_t *h = cr->cleanup_head;
    cr->cleanup_head = h->next;
    if (execute && h->fn) h->fn(h->arg);
}

void suspenders_exit(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_self())) return;
    suspenders_cr_exit();   /* runs cleanup handlers from this live frame */
}

static void s_deadline_cb(void *arg) {
    suspenders_cr_t *cr = (suspenders_cr_t*)arg;
    if (!s_waiter_wake(cr, SUSPENDERS_TIMEDOUT) &&
        cr->state == SUSPENDERS_STATE_SUSPENDED) {
        /* Blocked on I/O or a bare suspend: same delivery as cancel. */
        cr->io_result = -1;
        suspenders_resume(cr);
    }
    /* If the coroutine is running or ready, s_pre_block reports the expiry
     * on its next blocking call. */
}

int suspenders_deadline(uint64_t deadline_ns) {
    suspenders_cr_t *cr = suspenders_self();
    if (SUSPENDERS_UNLIKELY(!cr)) {
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }
    if (cr->deadline_armed) {
        s_timer_disarm(&cr->deadline_timer);
        cr->deadline_armed = false;
    }
    if (deadline_ns == 0) return SUSPENDERS_OK;
    suspenders_timer_t *t = &cr->deadline_timer;
    memset(t, 0, sizeof(*t));
    t->deadline_ns = deadline_ns;
    t->cb = s_deadline_cb;
    t->arg = cr;
    if (SUSPENDERS_UNLIKELY(!s_timer_arm(t))) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }
    cr->deadline_armed = true;
    return SUSPENDERS_OK;
}

suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) {
        suspenders_errno = SUSPENDERS_NOTINIT;
        return NULL;
    }
    if (SUSPENDERS_UNLIKELY(!func)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return NULL;
    }

    s_main_cr_get();

    /* The cr_t lives at the front of its own stack arena: one allocation,
     * cache-line alignment guaranteed by the arena, one destroy at reap. */
    memento_thread_heap_t *heap = memento_thread_heap_get();
    memento_arena_t *arena = memento_arena_create(
        sizeof(suspenders_cr_t) + 2 * SUSPENDERS_CACHELINE + SUSPENDERS_STACK_SIZE, heap);
    if (SUSPENDERS_UNLIKELY(!arena)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }

    suspenders_cr_t *cr = (suspenders_cr_t*)memento_arena_alloc(
        arena, sizeof(suspenders_cr_t), SUSPENDERS_CACHELINE);
    void *stack = cr ? memento_arena_alloc(arena, SUSPENDERS_STACK_SIZE, 16) : NULL;
    if (SUSPENDERS_UNLIKELY(!stack)) {
        memento_arena_destroy(arena);
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    memset(cr, 0, sizeof(suspenders_cr_t));

    cr->arena = arena;
    cr->owner_heap = heap;
    cr->stack_size = SUSPENDERS_STACK_SIZE;
    cr->id = (uint64_t)suspenders_atomic_fetch_add(suspenders_next_id, 1,
                                                   SUSPENDERS_MEMORY_ORDER_RELAXED);
    cr->state = SUSPENDERS_STATE_READY;
    cr->base_qos = (int)qos;
    suspenders_atomic_init(cr->effective_qos, (int)qos);

    suspenders_make_context(&cr->ctx, stack, SUSPENDERS_STACK_SIZE, func, arg);
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    cr->ctx.cr = cr;
#endif

    suspenders_ready_enqueue(cr);
    suspenders_atomic_fetch_add(active_coroutines, 1, SUSPENDERS_MEMORY_ORDER_RELAXED);

    return cr;
}

suspenders_cr_t* suspenders_go(void (*func)(void*), void *arg) {
    return suspenders_spawn(func, arg, SUSPENDERS_QOS_NORMAL);
}

/* ============================================================================
 * CHANNEL - Zero-copy rendezvous
 * ============================================================================ */
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz) {
    suspenders_chan_t *ch = (suspenders_chan_t*)memento_thread_heap_alloc(memento_thread_heap_get(),
                                                  sizeof(suspenders_chan_t));
    if (SUSPENDERS_UNLIKELY(!ch)) return NULL;
    memset(ch, 0, sizeof(*ch));
    ch->elem_sz = elem_sz;
    ch->buf_sz = buf_sz;
    suspenders_ticket_init(&ch->lock);
    return ch;
}

bool suspenders_chan_send(suspenders_chan_t *ch, void *val) {
    if (SUSPENDERS_UNLIKELY(!ch || !val)) return false;

    suspenders_cr_t *cr = suspenders_running;
    bool can_block = (cr && cr != suspenders_main_cr);
    if (can_block && SUSPENDERS_UNLIKELY(s_pre_block(cr) != 0)) return false;

    suspenders_ticket_lock(&ch->lock);

    if (ch->receivers.head) {
        suspenders_wq_node_t *rx = s_wq_pop(&ch->receivers);
        memcpy(rx->data_ptr, val, ch->elem_sz);
        s_wait_grant(rx, SUSPENDERS_OK);
        suspenders_ticket_unlock(&ch->lock);
        suspenders_resume(rx->cr);
        return true;
    }

    if (ch->buf_sz == 0 && can_block) {
        s_wait_park(&ch->senders, &ch->lock, val);
        suspenders_ticket_unlock(&ch->lock);
        return s_wait_block() == SUSPENDERS_OK;
    }

    suspenders_ticket_unlock(&ch->lock);
    return false;
}

bool suspenders_chan_recv(suspenders_chan_t *ch, void *out) {
    if (SUSPENDERS_UNLIKELY(!ch || !out)) return false;

    suspenders_cr_t *cr = suspenders_running;
    bool can_block = (cr && cr != suspenders_main_cr);
    if (can_block && SUSPENDERS_UNLIKELY(s_pre_block(cr) != 0)) return false;

    suspenders_ticket_lock(&ch->lock);

    if (ch->senders.head) {
        suspenders_wq_node_t *tx = s_wq_pop(&ch->senders);
        memcpy(out, tx->data_ptr, ch->elem_sz);
        s_wait_grant(tx, SUSPENDERS_OK);
        suspenders_ticket_unlock(&ch->lock);
        suspenders_resume(tx->cr);
        return true;
    }

    if (ch->buf_sz == 0 && can_block) {
        s_wait_park(&ch->receivers, &ch->lock, out);
        suspenders_ticket_unlock(&ch->lock);
        return s_wait_block() == SUSPENDERS_OK;
    }

    suspenders_ticket_unlock(&ch->lock);
    return false;
}


/* ============================================================================
 * SYNCHRONIZATION PRIMITIVES - mutex, rwlock, cond, waitgroup
 *
 * All are value types built on the wait protocol: a ticket lock protects the
 * primitive's state and FIFO waiter list; grants hand the resource directly
 * to the head waiter. Blocking calls consume pending cancels, honor
 * suspenders_deadline, and accept a per-call absolute deadline (_dl).
 * ============================================================================ */
int suspenders_mutex_init(suspenders_mutex_t *m) {
    if (SUSPENDERS_UNLIKELY(!m)) return SUSPENDERS_INVAL;
    memset(m, 0, sizeof(*m));
    suspenders_ticket_init(&m->lock);
    return SUSPENDERS_OK;
}

int suspenders_mutex_trylock(suspenders_mutex_t *m) {
    if (SUSPENDERS_UNLIKELY(!m || !suspenders_running)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&m->lock);
    if (!m->owner) {
        m->owner = suspenders_running;
        suspenders_ticket_unlock(&m->lock);
        return SUSPENDERS_OK;
    }
    suspenders_ticket_unlock(&m->lock);
    suspenders_errno = SUSPENDERS_BUSY;
    return SUSPENDERS_BUSY;
}

int suspenders_mutex_lock_dl(suspenders_mutex_t *m, uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!m)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;

    suspenders_ticket_lock(&m->lock);
    if (!m->owner) {
        m->owner = cr;
        suspenders_ticket_unlock(&m->lock);
        return SUSPENDERS_OK;
    }
    if (SUSPENDERS_UNLIKELY(m->owner == cr)) {
        suspenders_ticket_unlock(&m->lock);
        suspenders_errno = SUSPENDERS_PERM;   /* not recursive */
        return SUSPENDERS_PERM;
    }
    suspenders_cr_t *owner = m->owner;
    s_wait_park(&m->waiters, &m->lock, NULL);
    suspenders_ticket_unlock(&m->lock);
    /* Single-level priority inheritance: raise the holder to our level. */
    suspenders_boost(owner, (suspenders_qos_t)suspenders_atomic_load(
        cr->effective_qos, SUSPENDERS_MEMORY_ORDER_RELAXED));
    return s_wait_block_dl(deadline_ns);
}

int suspenders_mutex_lock(suspenders_mutex_t *m) {
    return suspenders_mutex_lock_dl(m, 0);
}

/* Re-acquire without cancel/deadline checks (cond re-lock must always
 * succeed so the caller returns holding the mutex). */
static void s_mutex_lock_raw(suspenders_mutex_t *m) {
    suspenders_cr_t *cr = suspenders_running;
    suspenders_ticket_lock(&m->lock);
    if (!m->owner) {
        m->owner = cr;
        suspenders_ticket_unlock(&m->lock);
        return;
    }
    s_wait_park(&m->waiters, &m->lock, NULL);
    suspenders_ticket_unlock(&m->lock);
    suspenders_wq_node_t *node = &cr->wq_node;
    for (;;) {
        while (node->status == S_WQ_WAITING) suspenders_suspend();
        if (node->status == SUSPENDERS_OK) break;
        /* Canceled/timed out while re-locking: keep waiting for the lock,
         * the failure was already reported by the wait that preceded it. */
        suspenders_ticket_lock(&m->lock);
        if (!m->owner) {
            m->owner = cr;
            suspenders_ticket_unlock(&m->lock);
            break;
        }
        s_wait_park(&m->waiters, &m->lock, NULL);
        suspenders_ticket_unlock(&m->lock);
    }
    node->wait_list = NULL;
    node->wait_lock = NULL;
}

int suspenders_mutex_unlock(suspenders_mutex_t *m) {
    suspenders_cr_t *cr = suspenders_running;
    if (SUSPENDERS_UNLIKELY(!m || !cr)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&m->lock);
    if (SUSPENDERS_UNLIKELY(m->owner != cr)) {
        suspenders_ticket_unlock(&m->lock);
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }
    /* Drop any inherited boost now that we release the resource. */
    suspenders_atomic_store(cr->effective_qos, cr->base_qos,
                            SUSPENDERS_MEMORY_ORDER_RELAXED);
    suspenders_wq_node_t *n = s_wq_pop(&m->waiters);
    if (n) {
        m->owner = n->cr;
        s_wait_grant(n, SUSPENDERS_OK);
        suspenders_ticket_unlock(&m->lock);
        suspenders_resume(n->cr);
    } else {
        m->owner = NULL;
        suspenders_ticket_unlock(&m->lock);
    }
    return SUSPENDERS_OK;
}

/* rwlock: waiters share one FIFO; data_ptr tags a waiting writer. */
#define S_RW_WRITER ((void*)(uintptr_t)1)

int suspenders_rwlock_init(suspenders_rwlock_t *rw) {
    if (SUSPENDERS_UNLIKELY(!rw)) return SUSPENDERS_INVAL;
    memset(rw, 0, sizeof(*rw));
    suspenders_ticket_init(&rw->lock);
    return SUSPENDERS_OK;
}

int suspenders_rwlock_tryrdlock(suspenders_rwlock_t *rw) {
    if (SUSPENDERS_UNLIKELY(!rw)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&rw->lock);
    if (!rw->writer && !rw->waiters.head) {
        rw->readers++;
        suspenders_ticket_unlock(&rw->lock);
        return SUSPENDERS_OK;
    }
    suspenders_ticket_unlock(&rw->lock);
    suspenders_errno = SUSPENDERS_BUSY;
    return SUSPENDERS_BUSY;
}

int suspenders_rwlock_rdlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!rw)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;

    suspenders_ticket_lock(&rw->lock);
    /* A queued waiter (necessarily headed by a writer batch) blocks new
     * readers so writers cannot starve. */
    if (!rw->writer && !rw->waiters.head) {
        rw->readers++;
        suspenders_ticket_unlock(&rw->lock);
        return SUSPENDERS_OK;
    }
    s_wait_park(&rw->waiters, &rw->lock, NULL);
    suspenders_ticket_unlock(&rw->lock);
    return s_wait_block_dl(deadline_ns);
}

int suspenders_rwlock_rdlock(suspenders_rwlock_t *rw) {
    return suspenders_rwlock_rdlock_dl(rw, 0);
}

int suspenders_rwlock_trywrlock(suspenders_rwlock_t *rw) {
    if (SUSPENDERS_UNLIKELY(!rw)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&rw->lock);
    if (!rw->writer && rw->readers == 0) {
        rw->writer = true;
        suspenders_ticket_unlock(&rw->lock);
        return SUSPENDERS_OK;
    }
    suspenders_ticket_unlock(&rw->lock);
    suspenders_errno = SUSPENDERS_BUSY;
    return SUSPENDERS_BUSY;
}

int suspenders_rwlock_wrlock_dl(suspenders_rwlock_t *rw, uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!rw)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;

    suspenders_ticket_lock(&rw->lock);
    if (!rw->writer && rw->readers == 0 && !rw->waiters.head) {
        rw->writer = true;
        suspenders_ticket_unlock(&rw->lock);
        return SUSPENDERS_OK;
    }
    s_wait_park(&rw->waiters, &rw->lock, S_RW_WRITER);
    suspenders_ticket_unlock(&rw->lock);
    return s_wait_block_dl(deadline_ns);
}

int suspenders_rwlock_wrlock(suspenders_rwlock_t *rw) {
    return suspenders_rwlock_wrlock_dl(rw, 0);
}

int suspenders_rwlock_unlock(suspenders_rwlock_t *rw) {
    if (SUSPENDERS_UNLIKELY(!rw)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&rw->lock);
    if (rw->writer) {
        rw->writer = false;
    } else if (rw->readers > 0) {
        rw->readers--;
    } else {
        suspenders_ticket_unlock(&rw->lock);
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }

    /* Grant the head of the queue: one writer, or a batch of readers. */
    suspenders_wq_node_t *granted = NULL, *granted_tail = NULL;
    if (rw->readers == 0 && !rw->writer && rw->waiters.head) {
        if (rw->waiters.head->data_ptr == S_RW_WRITER) {
            suspenders_wq_node_t *n = s_wq_pop(&rw->waiters);
            rw->writer = true;
            s_wait_grant(n, SUSPENDERS_OK);
            granted = granted_tail = n;
        } else {
            while (rw->waiters.head && rw->waiters.head->data_ptr != S_RW_WRITER) {
                suspenders_wq_node_t *n = s_wq_pop(&rw->waiters);
                rw->readers++;
                s_wait_grant(n, SUSPENDERS_OK);
                if (granted_tail) granted_tail->next = n;
                else granted = n;
                granted_tail = n;
            }
        }
    }
    suspenders_ticket_unlock(&rw->lock);

    while (granted) {
        suspenders_wq_node_t *next = granted->next;
        granted->next = NULL;
        suspenders_resume(granted->cr);
        granted = next;
    }
    return SUSPENDERS_OK;
}

int suspenders_cond_init(suspenders_cond_t *c) {
    if (SUSPENDERS_UNLIKELY(!c)) return SUSPENDERS_INVAL;
    memset(c, 0, sizeof(*c));
    suspenders_ticket_init(&c->lock);
    return SUSPENDERS_OK;
}

int suspenders_cond_wait_dl(suspenders_cond_t *c, suspenders_mutex_t *m,
                            uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!c || !m)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;
    if (SUSPENDERS_UNLIKELY(m->owner != cr)) {
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }

    suspenders_ticket_lock(&c->lock);
    s_wait_park(&c->waiters, &c->lock, NULL);
    suspenders_ticket_unlock(&c->lock);

    suspenders_mutex_unlock(m);
    int st = s_wait_block_dl(deadline_ns);
    s_mutex_lock_raw(m);   /* always return holding the mutex */
    return st;
}

int suspenders_cond_wait(suspenders_cond_t *c, suspenders_mutex_t *m) {
    return suspenders_cond_wait_dl(c, m, 0);
}

int suspenders_cond_signal(suspenders_cond_t *c) {
    if (SUSPENDERS_UNLIKELY(!c)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&c->lock);
    suspenders_wq_node_t *n = s_wq_pop(&c->waiters);
    if (n) s_wait_grant(n, SUSPENDERS_OK);
    suspenders_ticket_unlock(&c->lock);
    if (n) suspenders_resume(n->cr);
    return SUSPENDERS_OK;
}

int suspenders_cond_broadcast(suspenders_cond_t *c) {
    if (SUSPENDERS_UNLIKELY(!c)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&c->lock);
    suspenders_wq_node_t *head = c->waiters.head;
    for (suspenders_wq_node_t *n = head; n; n = n->next) {
        s_wait_grant(n, SUSPENDERS_OK);
    }
    c->waiters.head = c->waiters.tail = NULL;
    suspenders_ticket_unlock(&c->lock);
    while (head) {
        suspenders_wq_node_t *next = head->next;
        head->next = NULL;
        suspenders_resume(head->cr);
        head = next;
    }
    return SUSPENDERS_OK;
}

int suspenders_waitgroup_init(suspenders_waitgroup_t *wg) {
    if (SUSPENDERS_UNLIKELY(!wg)) return SUSPENDERS_INVAL;
    memset(wg, 0, sizeof(*wg));
    suspenders_ticket_init(&wg->lock);
    return SUSPENDERS_OK;
}

int suspenders_waitgroup_add(suspenders_waitgroup_t *wg, int delta) {
    if (SUSPENDERS_UNLIKELY(!wg)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&wg->lock);
    if (SUSPENDERS_UNLIKELY(wg->count + delta < 0)) {
        suspenders_ticket_unlock(&wg->lock);
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    wg->count += delta;
    suspenders_wq_node_t *head = NULL;
    if (wg->count == 0 && wg->waiters.head) {
        head = wg->waiters.head;
        for (suspenders_wq_node_t *n = head; n; n = n->next) {
            s_wait_grant(n, SUSPENDERS_OK);
        }
        wg->waiters.head = wg->waiters.tail = NULL;
    }
    suspenders_ticket_unlock(&wg->lock);
    while (head) {
        suspenders_wq_node_t *next = head->next;
        head->next = NULL;
        suspenders_resume(head->cr);
        head = next;
    }
    return SUSPENDERS_OK;
}

int suspenders_waitgroup_done(suspenders_waitgroup_t *wg) {
    return suspenders_waitgroup_add(wg, -1);
}

int suspenders_waitgroup_wait_dl(suspenders_waitgroup_t *wg, uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!wg)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;

    suspenders_ticket_lock(&wg->lock);
    if (wg->count == 0) {
        suspenders_ticket_unlock(&wg->lock);
        return SUSPENDERS_OK;
    }
    s_wait_park(&wg->waiters, &wg->lock, NULL);
    suspenders_ticket_unlock(&wg->lock);
    return s_wait_block_dl(deadline_ns);
}

int suspenders_waitgroup_wait(suspenders_waitgroup_t *wg) {
    return suspenders_waitgroup_wait_dl(wg, 0);
}

/* ============================================================================
 * EVENT BACKEND IMPLEMENTATIONS
 * ============================================================================ */

/* Backend struct definition */
struct suspenders_backend_s {
    int type;
    union {
#if SUSPENDERS_BACKEND_IOURING
        struct io_uring ring;
#endif
#if SUSPENDERS_BACKEND_KQUEUE
        struct {
            int kq;
            struct kevent *events;
            int events_cap;
        } kqueue;
#endif
#if SUSPENDERS_BACKEND_WSAPOLL || SUSPENDERS_BACKEND_POLL
        struct {
            struct pollfd *fds;
            suspenders_cr_t **crs;
            int nfds;
            int cap;
        } poll;
#endif
    };
};

/* Backend type constants */
#define SUSPENDERS_BE_IOURING  1
#define SUSPENDERS_BE_KQUEUE   2
#define SUSPENDERS_BE_WSAPOLL  3
#define SUSPENDERS_BE_POLL     4

/* Backend API */
static suspenders_backend_t* s_backend_create(unsigned hint);
static void s_backend_destroy(suspenders_backend_t *be);
static int s_backend_register(suspenders_backend_t *be, suspenders_sock_t fd, uint32_t events, suspenders_cr_t *cr);
static void s_backend_unregister(suspenders_backend_t *be, suspenders_sock_t fd);
static int s_backend_wait(suspenders_backend_t *be, int timeout_ms);

/* ============================================================================
 * IO_URING BACKEND
 * ============================================================================ */
#if SUSPENDERS_BACKEND_IOURING

static suspenders_backend_t* s_backend_create(unsigned hint) {
    suspenders_backend_t *be = (suspenders_backend_t*)malloc(sizeof(*be));
    if (!be) return NULL;
    memset(be, 0, sizeof(*be));
    be->type = SUSPENDERS_BE_IOURING;
    int ret = io_uring_queue_init(hint, &be->ring, 0);
    if (ret < 0) {
        free(be);
        return NULL;
    }
    return be;
}

static void s_backend_destroy(suspenders_backend_t *be) {
    if (!be) return;
    io_uring_queue_exit(&be->ring);
    free(be);
}

static int s_backend_register(suspenders_backend_t *be, suspenders_sock_t fd, uint32_t events, suspenders_cr_t *cr) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&be->ring);
    if (!sqe) return -1;
    short poll_events = 0;
    if (events & POLLIN) poll_events |= POLLIN;
    if (events & POLLOUT) poll_events |= POLLOUT;
    if (events & POLLERR) poll_events |= POLLERR;
    io_uring_prep_poll_add(sqe, (int)fd, poll_events);
    io_uring_sqe_set_data(sqe, cr);
    return 0;
}

static void s_backend_unregister(suspenders_backend_t *be, suspenders_sock_t fd) {
    (void)be;
    (void)fd;
    /* io_uring poll_add is one-shot; no explicit unregister needed.
     * If cancellation is required, callers should close the fd. */
}

static int s_backend_wait(suspenders_backend_t *be, int timeout_ms) {
    struct io_uring_cqe *cqe;
    unsigned head;
    int found = 0;

    io_uring_submit(&be->ring);

    /* Try non-blocking peek first */
    io_uring_for_each_cqe(&be->ring, head, cqe) {
        suspenders_cr_t *cr = (suspenders_cr_t*)io_uring_cqe_get_data(cqe);
        if (cr && cr->state == SUSPENDERS_STATE_SUSPENDED) {
            cr->io_result = cqe->res;
            suspenders_ready_enqueue(cr);
            found++;
        }
        io_uring_cqe_seen(&be->ring, cqe);
    }

    if (found || timeout_ms == 0) return found;

    /* Block for at least one CQE */
    int ret;
    if (timeout_ms < 0) {
        ret = io_uring_wait_cqe(&be->ring, &cqe);
    } else {
        struct __kernel_timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
        ret = io_uring_wait_cqe_timeout(&be->ring, &cqe, &ts);
    }
    if (ret == 0 && cqe) {
        suspenders_cr_t *cr = (suspenders_cr_t*)io_uring_cqe_get_data(cqe);
        if (cr && cr->state == SUSPENDERS_STATE_SUSPENDED) {
            cr->io_result = cqe->res;
            suspenders_ready_enqueue(cr);
            found++;
        }
        io_uring_cqe_seen(&be->ring, cqe);

        /* Drain additional CQEs */
        io_uring_for_each_cqe(&be->ring, head, cqe) {
            suspenders_cr_t *cr = (suspenders_cr_t*)io_uring_cqe_get_data(cqe);
            if (cr && cr->state == SUSPENDERS_STATE_SUSPENDED) {
                cr->io_result = cqe->res;
                suspenders_ready_enqueue(cr);
                found++;
            }
            io_uring_cqe_seen(&be->ring, cqe);
        }
    }
    return found;
}

/* ============================================================================
 * KQUEUE BACKEND
 * ============================================================================ */
#elif SUSPENDERS_BACKEND_KQUEUE

static suspenders_backend_t* s_backend_create(unsigned hint) {
    (void)hint;
    suspenders_backend_t *be = (suspenders_backend_t*)malloc(sizeof(*be));
    if (!be) return NULL;
    memset(be, 0, sizeof(*be));
    be->type = SUSPENDERS_BE_KQUEUE;
    be->kqueue.kq = kqueue();
    if (be->kqueue.kq < 0) {
        free(be);
        return NULL;
    }
    be->kqueue.events_cap = 256;
    be->kqueue.events = (struct kevent*)malloc(sizeof(struct kevent) * be->kqueue.events_cap);
    if (!be->kqueue.events) {
        close(be->kqueue.kq);
        free(be);
        return NULL;
    }
    return be;
}

static void s_backend_destroy(suspenders_backend_t *be) {
    if (!be) return;
    if (be->kqueue.kq >= 0) close(be->kqueue.kq);
    free(be->kqueue.events);
    free(be);
}

static int s_backend_register(suspenders_backend_t *be, suspenders_sock_t fd, uint32_t events, suspenders_cr_t *cr) {
    struct kevent ev[2];
    int nev = 0;
    if (events & POLLIN) {
        EV_SET(&ev[nev++], (uintptr_t)fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, cr);
    }
    if (events & POLLOUT) {
        EV_SET(&ev[nev++], (uintptr_t)fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, cr);
    }
    if (nev == 0) return 0;
    return kevent(be->kqueue.kq, ev, nev, NULL, 0, NULL);
}

static void s_backend_unregister(suspenders_backend_t *be, suspenders_sock_t fd) {
    (void)be;
    (void)fd;
    /* EV_ONESHOT auto-deletes after trigger. If we need explicit delete before trigger:
     * struct kevent ev[2];
     * EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
     * EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
     * kevent(be->kqueue.kq, ev, 2, NULL, 0, NULL);
     * For now, one-shot semantics are sufficient.
     */
}

static int s_backend_wait(suspenders_backend_t *be, int timeout_ms) {
    struct timespec ts;
    struct timespec *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }

    int n = kevent(be->kqueue.kq, NULL, 0, be->kqueue.events, be->kqueue.events_cap, tsp);
    for (int i = 0; i < n; i++) {
        suspenders_cr_t *cr = (suspenders_cr_t*)be->kqueue.events[i].udata;
        if (cr && cr->state == SUSPENDERS_STATE_SUSPENDED) {
            suspenders_ready_enqueue(cr);
        }
    }
    return n;
}

/* ============================================================================
 * POLL / WSAPOLL BACKEND
 * ============================================================================ */
#elif SUSPENDERS_BACKEND_WSAPOLL || SUSPENDERS_BACKEND_POLL

static suspenders_backend_t* s_backend_create(unsigned hint) {
    (void)hint;
    suspenders_backend_t *be = (suspenders_backend_t*)malloc(sizeof(*be));
    if (!be) return NULL;
    memset(be, 0, sizeof(*be));
#if SUSPENDERS_BACKEND_WSAPOLL
    be->type = SUSPENDERS_BE_WSAPOLL;
#else
    be->type = SUSPENDERS_BE_POLL;
#endif
    be->poll.cap = 256;
    be->poll.fds = (struct pollfd*)malloc(sizeof(struct pollfd) * be->poll.cap);
    be->poll.crs = (suspenders_cr_t**)malloc(sizeof(suspenders_cr_t*) * be->poll.cap);
    if (!be->poll.fds || !be->poll.crs) {
        free(be->poll.fds);
        free(be->poll.crs);
        free(be);
        return NULL;
    }
    be->poll.nfds = 0;
    return be;
}

static void s_backend_destroy(suspenders_backend_t *be) {
    if (!be) return;
    free(be->poll.fds);
    free(be->poll.crs);
    free(be);
}

static int s_backend_register(suspenders_backend_t *be, suspenders_sock_t fd, uint32_t events, suspenders_cr_t *cr) {
    if (be->poll.nfds >= be->poll.cap) {
        int new_cap = be->poll.cap * 2;
        struct pollfd *new_fds = (struct pollfd*)realloc(be->poll.fds, sizeof(struct pollfd) * new_cap);
        suspenders_cr_t **new_crs = (suspenders_cr_t**)realloc(be->poll.crs, sizeof(suspenders_cr_t*) * new_cap);
        if (!new_fds || !new_crs) return -1;
        be->poll.fds = new_fds;
        be->poll.crs = new_crs;
        be->poll.cap = new_cap;
    }
    int idx = be->poll.nfds++;
    be->poll.fds[idx].fd = (int)fd;
    be->poll.fds[idx].events = 0;
    if (events & POLLIN) be->poll.fds[idx].events |= POLLIN;
    if (events & POLLOUT) be->poll.fds[idx].events |= POLLOUT;
    be->poll.crs[idx] = cr;
    return 0;
}

static void s_backend_unregister(suspenders_backend_t *be, suspenders_sock_t fd) {
    int j = 0;
    for (int i = 0; i < be->poll.nfds; i++) {
        if (be->poll.fds[i].fd == (int)fd) {
            continue; /* skip - consumed or cancelled */
        }
        be->poll.fds[j] = be->poll.fds[i];
        be->poll.crs[j] = be->poll.crs[i];
        j++;
    }
    be->poll.nfds = j;
}

static int s_backend_wait(suspenders_backend_t *be, int timeout_ms) {
    if (be->poll.nfds == 0) {
        if (timeout_ms < 0) timeout_ms = 1;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        Sleep(timeout_ms);
#else
        usleep(timeout_ms * 1000);
#endif
        return 0;
    }

#if SUSPENDERS_BACKEND_WSAPOLL
    int n = WSAPoll(be->poll.fds, be->poll.nfds, timeout_ms);
#else
    int n = poll(be->poll.fds, be->poll.nfds, timeout_ms);
#endif
    if (n <= 0) return n;

    int j = 0;
    int found = 0;
    for (int i = 0; i < be->poll.nfds; i++) {
        if (be->poll.fds[i].revents) {
            suspenders_cr_t *cr = be->poll.crs[i];
            if (cr && cr->state == SUSPENDERS_STATE_SUSPENDED) {
                suspenders_ready_enqueue(cr);
                found++;
            }
            /* One-shot: don't copy back to array */
        } else {
            be->poll.fds[j] = be->poll.fds[i];
            be->poll.crs[j] = be->poll.crs[i];
            j++;
        }
    }
    be->poll.nfds = j;
    return found;
}

#else
    #error "No event backend available for this platform"
#endif


/* ============================================================================
 * TRANSPORT IMPLEMENTATIONS
 * ============================================================================ */

static bool suspenders_hose_parse_uri(const char *uri, suspenders_hose_t *d, char *host, int *port, const char **scheme_out) {
    static const struct { const char *prefix; suspenders_hose_protocol_t proto; } schemes[] = {
        {"tcp://",  SUSPENDERS_HOSE_PROTO_TCP},
        {"udp://",  SUSPENDERS_HOSE_PROTO_UDP},
        {"unix://", SUSPENDERS_HOSE_PROTO_UNIX},
        {"pipe://", SUSPENDERS_HOSE_PROTO_PIPE},
        {"tty://",  SUSPENDERS_HOSE_PROTO_TTY},
    };
    for (size_t i = 0; i < sizeof(schemes)/sizeof(schemes[0]); i++) {
        size_t len = strlen(schemes[i].prefix);
        if (strncmp(uri, schemes[i].prefix, len) == 0) {
            *scheme_out = schemes[i].prefix;
            d->protocol = schemes[i].proto;
            if (d->protocol == SUSPENDERS_HOSE_PROTO_TCP || d->protocol == SUSPENDERS_HOSE_PROTO_UDP) {
                return sscanf(uri + len, "%255[^:]:%d", host, port) == 2;
            } else if (d->protocol == SUSPENDERS_HOSE_PROTO_UNIX || d->protocol == SUSPENDERS_HOSE_PROTO_PIPE || d->protocol == SUSPENDERS_HOSE_PROTO_TTY) {
                strncpy(host, uri + len, 256);
                host[255] = '\0';
                *port = 0;
                return true;
            }
            return false;
        }
    }
    return false;
}

/* ============================================================================
 * TCP TRANSPORT
 * ============================================================================ */
static bool tcp_dial(suspenders_hose_t *h, const char *host, int port) {
    h->fd = suspenders_socket(AF_INET, SOCK_STREAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }

    int ret = connect(h->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EINPROGRESS && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }

        suspenders_cr_t *cr = suspenders_running;
        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }

        int sock_err = 0;
        socklen_t errlen = sizeof(sock_err);
        if (getsockopt(h->fd, SOL_SOCKET, SO_ERROR, (char*)&sock_err, &errlen) < 0 || sock_err != 0) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }
    }

    h->roles |= SUSPENDERS_HOSE_ROLE_DIALER | SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    return true;
}

static bool tcp_listen(suspenders_hose_t *h, const char *host, int port) {
    (void)host;
    h->fd = suspenders_socket(AF_INET, SOCK_STREAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }

    int opt = 1;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    setsockopt(h->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(h->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(h->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    if (listen(h->fd, 128) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    h->roles |= SUSPENDERS_HOSE_ROLE_LISTENER;
    return true;
}

static bool tcp_accept(suspenders_hose_t *listener, suspenders_hose_t *client) {
    if (!(listener->roles & SUSPENDERS_HOSE_ROLE_LISTENER) || listener->fd == SUSPENDERS_INVALID_SOCK) return false;

    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        suspenders_sock_t fd = accept(listener->fd, NULL, NULL);
        if (fd != SUSPENDERS_INVALID_SOCK) {
            if (!suspenders_set_nonblocking(fd)) {
                suspenders_close_socket(fd);
                return false;
            }
            suspenders_hose_init(client, NULL);
            client->fd = fd;
            client->protocol = listener->protocol;
            client->transport = listener->transport;
            client->roles = SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
            return true;
        }
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return false;

        s_backend_register(suspenders_backend, listener->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, listener->fd);
        if (cr->io_result < 0) return false;
    }
}

static ssize_t tcp_read(suspenders_hose_t *h, void *dest, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;

    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        ssize_t n = suspenders_sock_recv(h->fd, dest, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;

        s_backend_register(suspenders_backend, h->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static ssize_t tcp_write(suspenders_hose_t *h, const void *src, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;

    suspenders_cr_t *cr = suspenders_running;
    size_t written = 0;
    while (written < len) {
        ssize_t n = suspenders_sock_send(h->fd, (const char*)src + written, len - written, 0);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n == 0) return (ssize_t)written;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;

        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
    return (ssize_t)written;
}

static void tcp_close(suspenders_hose_t *h) {
    if (h->fd != SUSPENDERS_INVALID_SOCK) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
    }
    h->roles = 0;
}

static const suspenders_transport_ops_t tcp_transport_ops = {
    .scheme = "tcp://",
    .dial = tcp_dial,
    .listen = tcp_listen,
    .accept = tcp_accept,
    .read = tcp_read,
    .write = tcp_write,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = tcp_close,
};

/* ============================================================================
 * UDP TRANSPORT
 * ============================================================================ */
static bool udp_dial(suspenders_hose_t *h, const char *host, int port) {
    h->fd = suspenders_socket(AF_INET, SOCK_DGRAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    if (connect(h->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    h->roles |= SUSPENDERS_HOSE_ROLE_DIALER | SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    return true;
}

static bool udp_listen(suspenders_hose_t *h, const char *host, int port) {
    (void)host;
    h->fd = suspenders_socket(AF_INET, SOCK_DGRAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(h->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    h->roles |= SUSPENDERS_HOSE_ROLE_LISTENER | SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    return true;
}

static ssize_t udp_read(suspenders_hose_t *h, void *dest, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        ssize_t n = suspenders_sock_recv(h->fd, dest, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static ssize_t udp_write(suspenders_hose_t *h, const void *src, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        ssize_t n = suspenders_sock_send(h->fd, src, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static ssize_t udp_recvfrom(suspenders_hose_t *h, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        ssize_t n = recvfrom(h->fd, (char*)dest, (int)len, 0, addr, addrlen);
#else
        ssize_t n = recvfrom(h->fd, dest, len, 0, addr, addrlen);
#endif
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static ssize_t udp_sendto(suspenders_hose_t *h, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        ssize_t n = sendto(h->fd, (const char*)src, (int)len, 0, addr, addrlen);
#else
        ssize_t n = sendto(h->fd, src, len, 0, addr, addrlen);
#endif
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static void udp_close(suspenders_hose_t *h) {
    if (h->fd != SUSPENDERS_INVALID_SOCK) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
    }
    h->roles = 0;
}

static const suspenders_transport_ops_t udp_transport_ops = {
    .scheme = "udp://",
    .dial = udp_dial,
    .listen = udp_listen,
    .accept = NULL,
    .read = udp_read,
    .write = udp_write,
    .recvfrom = udp_recvfrom,
    .sendto = udp_sendto,
    .close = udp_close,
};

/* ============================================================================
 * UNIX DOMAIN SOCKET TRANSPORT (POSIX only)
 * ============================================================================ */
#if !defined(SUSPENDERS_PLATFORM_WINDOWS)

static bool unix_dial(suspenders_hose_t *h, const char *path, int port) {
    (void)port;
    h->fd = suspenders_socket(AF_UNIX, SOCK_STREAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    int ret = connect(h->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EINPROGRESS && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }
        suspenders_cr_t *cr = suspenders_running;
        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }

        int sock_err = 0;
        socklen_t errlen = sizeof(sock_err);
        if (getsockopt(h->fd, SOL_SOCKET, SO_ERROR, &sock_err, &errlen) < 0 || sock_err != 0) {
            suspenders_close_socket(h->fd);
            h->fd = SUSPENDERS_INVALID_SOCK;
            return false;
        }
    }
    h->roles |= SUSPENDERS_HOSE_ROLE_DIALER | SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    return true;
}

static bool unix_listen(suspenders_hose_t *h, const char *path, int port) {
    (void)port;
    h->fd = suspenders_socket(AF_UNIX, SOCK_STREAM, 0);
    if (h->fd == SUSPENDERS_INVALID_SOCK) return false;
    if (!suspenders_set_nonblocking(h->fd)) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }

    (void)unlink(path);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(h->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    if (listen(h->fd, 128) < 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
    }
    h->roles |= SUSPENDERS_HOSE_ROLE_LISTENER;
    return true;
}

static bool unix_accept(suspenders_hose_t *listener, suspenders_hose_t *client) {
    if (!(listener->roles & SUSPENDERS_HOSE_ROLE_LISTENER) || listener->fd == SUSPENDERS_INVALID_SOCK) return false;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        suspenders_sock_t fd = accept(listener->fd, NULL, NULL);
        if (fd != SUSPENDERS_INVALID_SOCK) {
            if (!suspenders_set_nonblocking(fd)) {
                suspenders_close_socket(fd);
                return false;
            }
            suspenders_hose_init(client, NULL);
            client->fd = fd;
            client->protocol = listener->protocol;
            client->transport = listener->transport;
            client->roles = SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
            return true;
        }
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return false;
        s_backend_register(suspenders_backend, listener->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, listener->fd);
        if (cr->io_result < 0) return false;
    }
}

static ssize_t unix_read(suspenders_hose_t *h, void *dest, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    while (1) {
        ssize_t n = suspenders_sock_recv(h->fd, dest, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLIN, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
}

static ssize_t unix_write(suspenders_hose_t *h, const void *src, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    suspenders_cr_t *cr = suspenders_running;
    size_t written = 0;
    while (written < len) {
        ssize_t n = suspenders_sock_send(h->fd, (const char*)src + written, len - written, 0);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n == 0) return (ssize_t)written;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) return -1;
        s_backend_register(suspenders_backend, h->fd, POLLOUT, cr);
        suspenders_suspend();
        s_backend_unregister(suspenders_backend, h->fd);
        if (cr->io_result < 0) return -1;
    }
    return (ssize_t)written;
}

static void unix_close(suspenders_hose_t *h) {
    if (h->fd != SUSPENDERS_INVALID_SOCK) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
    }
    h->roles = 0;
}

static const suspenders_transport_ops_t unix_transport_ops = {
    .scheme = "unix://",
    .dial = unix_dial,
    .listen = unix_listen,
    .accept = unix_accept,
    .read = unix_read,
    .write = unix_write,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = unix_close,
};

#endif /* !Windows */

/* ============================================================================
 * TTY TRANSPORT
 * ============================================================================ */
static bool tty_dial(suspenders_hose_t *h, const char *device, int port) {
    (void)port;
    int fd = -1;
    if (strcmp(device, "stdin") == 0) fd = 0;
    else if (strcmp(device, "stdout") == 0) fd = 1;
    else if (strcmp(device, "stderr") == 0) fd = 2;
    else return false;

    h->fd = (suspenders_sock_t)fd;
    h->roles |= SUSPENDERS_HOSE_ROLE_READER | SUSPENDERS_HOSE_ROLE_WRITER;
    return true;
}

static void tty_close(suspenders_hose_t *h) {
    h->fd = SUSPENDERS_INVALID_SOCK;
    h->roles = 0;
}

static const suspenders_transport_ops_t tty_transport_ops = {
    .scheme = "tty://",
    .dial = tty_dial,
    .listen = NULL,
    .accept = NULL,
    .read = NULL,
    .write = NULL,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = tty_close,
};

/* ============================================================================
 * HOSE API
 * ============================================================================ */
void suspenders_hose_init(suspenders_hose_t *d, struct buf *b) {
    memset(d, 0, sizeof(*d));
    d->fd = SUSPENDERS_INVALID_SOCK;
    d->buffer = b;
}

bool suspenders_hose_dial(suspenders_hose_t *d, const char *uri) {
    char host[256] = {0};
    int port = 0;
    const char *scheme = NULL;
    if (!suspenders_hose_parse_uri(uri, d, host, &port, &scheme)) return false;

    const suspenders_transport_ops_t *ops = suspenders_transport_find(scheme);
    if (!ops || !ops->dial) return false;

    d->transport = ops;
    return ops->dial(d, host, port);
}

bool suspenders_hose_listen(suspenders_hose_t *d, const char *uri) {
    char host[256] = {0};
    int port = 0;
    const char *scheme = NULL;
    if (!suspenders_hose_parse_uri(uri, d, host, &port, &scheme)) return false;

    const suspenders_transport_ops_t *ops = suspenders_transport_find(scheme);
    if (!ops || !ops->listen) return false;

    d->transport = ops;
    return ops->listen(d, host, port);
}

bool suspenders_hose_accept(suspenders_hose_t *d, suspenders_hose_t *client) {
    if (!d->transport || !d->transport->accept) return false;
    return d->transport->accept(d, client);
}

ssize_t suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len) {
    if (!d->transport || !d->transport->read) return -1;
    return d->transport->read(d, dest, len);
}

ssize_t suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len) {
    if (!d->transport || !d->transport->write) return -1;
    return d->transport->write(d, src, len);
}

ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen) {
    if (!d->transport || !d->transport->recvfrom) return -1;
    return d->transport->recvfrom(d, dest, len, addr, addrlen);
}

ssize_t suspenders_hose_sendto(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen) {
    if (!d->transport || !d->transport->sendto) return -1;
    return d->transport->sendto(d, src, len, addr, addrlen);
}

void suspenders_hose_close(suspenders_hose_t *d) {
    if (d->transport && d->transport->close) {
        d->transport->close(d);
    } else {
        if (d->fd != SUSPENDERS_INVALID_SOCK) {
            suspenders_close_socket(d->fd);
            d->fd = SUSPENDERS_INVALID_SOCK;
        }
        d->roles = 0;
    }
}

/* ============================================================================
 * TRANSPORT REGISTRY
 * ============================================================================ */
bool suspenders_transport_register(const suspenders_transport_ops_t *ops) {
    if (suspenders_transport_count >= 16 || !ops || !ops->scheme) return false;
    suspenders_transport_registry[suspenders_transport_count++] = ops;
    return true;
}

const suspenders_transport_ops_t* suspenders_transport_find(const char *scheme) {
    for (int i = 0; i < suspenders_transport_count; i++) {
        if (strcmp(suspenders_transport_registry[i]->scheme, scheme) == 0) {
            return suspenders_transport_registry[i];
        }
    }
    return NULL;
}


/* ============================================================================
 * TIMERS
 * ============================================================================ */
typedef struct {
    suspenders_timer_t **data;
    size_t count;
    size_t cap;
} suspenders_timer_heap_t;

static SUSPENDERS_TLS suspenders_timer_heap_t suspenders_timer_heap = {0};

static bool suspenders_timer_heap_ensure_cap(suspenders_timer_heap_t *h, size_t needed) {
    if (needed <= h->cap) return true;
    size_t new_cap = h->cap ? h->cap * 2 : 16;
    while (new_cap < needed) new_cap *= 2;
    memento_thread_heap_t *heap = memento_thread_heap_get();
    suspenders_timer_t **new_data = (suspenders_timer_t**)memento_thread_heap_alloc(
        heap, new_cap * sizeof(*new_data));
    if (!new_data) return false;
    if (h->data) {
        memcpy(new_data, h->data, h->count * sizeof(*new_data));
        memento_thread_heap_free(heap, h->data, h->cap * sizeof(*new_data));
    }
    h->data = new_data;
    h->cap = new_cap;
    return true;
}

static void suspenders_timer_heap_swap(suspenders_timer_t **a, suspenders_timer_t **b) {
    suspenders_timer_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

static void suspenders_timer_heap_sift_up(suspenders_timer_heap_t *h, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (h->data[parent]->deadline_ns <= h->data[idx]->deadline_ns) break;
        suspenders_timer_heap_swap(&h->data[parent], &h->data[idx]);
        idx = parent;
    }
}

static void suspenders_timer_heap_sift_down(suspenders_timer_heap_t *h, size_t idx) {
    size_t n = h->count;
    while (1) {
        size_t left = idx * 2 + 1;
        size_t right = idx * 2 + 2;
        size_t smallest = idx;
        if (left < n && h->data[left]->deadline_ns < h->data[smallest]->deadline_ns)
            smallest = left;
        if (right < n && h->data[right]->deadline_ns < h->data[smallest]->deadline_ns)
            smallest = right;
        if (smallest == idx) break;
        suspenders_timer_heap_swap(&h->data[idx], &h->data[smallest]);
        idx = smallest;
    }
}

static bool suspenders_timer_heap_push(suspenders_timer_t *t) {
    suspenders_timer_heap_t *h = &suspenders_timer_heap;
    if (!suspenders_timer_heap_ensure_cap(h, h->count + 1)) return false;
    h->data[h->count] = t;
    h->count++;
    suspenders_timer_heap_sift_up(h, h->count - 1);
    return true;
}

static suspenders_timer_t* suspenders_timer_heap_pop(void) {
    suspenders_timer_heap_t *h = &suspenders_timer_heap;
    if (h->count == 0) return NULL;
    suspenders_timer_t *t = h->data[0];
    h->data[0] = h->data[h->count - 1];
    h->count--;
    if (h->count > 0) suspenders_timer_heap_sift_down(h, 0);
    return t;
}

static void suspenders_timer_heap_remove(suspenders_timer_t *t) {
    suspenders_timer_heap_t *h = &suspenders_timer_heap;
    for (size_t i = 0; i < h->count; i++) {
        if (h->data[i] == t) {
            h->data[i] = h->data[h->count - 1];
            h->count--;
            if (i < h->count) {
                suspenders_timer_heap_sift_down(h, i);
                suspenders_timer_heap_sift_up(h, i);
            }
            break;
        }
    }
}

uint64_t suspenders_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Arm a caller-owned (typically stack- or cr-resident) timer whose
 * deadline_ns/cb/arg are already set. */
static bool s_timer_arm(suspenders_timer_t *t) {
    t->active = true;
    if (!suspenders_timer_heap_push(t)) {
        t->active = false;
        return false;
    }
    return true;
}

static void s_timer_disarm(suspenders_timer_t *t) {
    if (!t->active) return;
    t->active = false;
    suspenders_timer_heap_remove(t);
}

suspenders_timer_t* suspenders_timer_create(int ms, bool repeat,
                                            void (*cb)(void*), void *arg) {
    if (ms <= 0 || !cb) {
        suspenders_errno = SUSPENDERS_INVAL;
        return NULL;
    }
    suspenders_timer_t *t = (suspenders_timer_t*)memento_thread_heap_alloc(
        memento_thread_heap_get(), sizeof(*t));
    if (!t) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    t->deadline_ns = suspenders_now_ns() + (uint64_t)ms * 1000000ULL;
    t->period_ms = ms;
    t->repeat = repeat;
    t->heap_allocated = true;
    t->cb = cb;
    t->arg = arg;
    if (!s_timer_arm(t)) {
        memento_thread_heap_free(memento_thread_heap_get(), t, sizeof(*t));
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    return t;
}

void suspenders_timer_cancel(suspenders_timer_t *t) {
    if (!t) return;
    s_timer_disarm(t);
    if (t->heap_allocated) {
        memento_thread_heap_free(memento_thread_heap_get(), t, sizeof(*t));
    }
}

static void suspenders_timer_fire_expired(void) {
    suspenders_timer_heap_t *h = &suspenders_timer_heap;
    uint64_t now = suspenders_now_ns();
    while (h->count > 0 && h->data[0]->deadline_ns <= now) {
        suspenders_timer_t *t = suspenders_timer_heap_pop();
        if (SUSPENDERS_UNLIKELY(!t)) continue;
        if (t->repeat) {
            t->deadline_ns = now + (uint64_t)t->period_ms * 1000000ULL;
            if (!suspenders_timer_heap_push(t)) {
                t->active = false;
            }
        } else {
            t->active = false;
        }
        /* The callback may release the timer; don't touch t afterwards. */
        void (*cb)(void*) = t->cb;
        void *arg = t->arg;
        if (cb) cb(arg);
    }
}

static void s_sleep_cb(void *arg) {
    suspenders_cr_t *cr = (suspenders_cr_t*)arg;
    suspenders_wq_node_t *node = &cr->wq_node;
    if (node->status == S_WQ_WAITING) {
        node->status = SUSPENDERS_OK;
        suspenders_resume(cr);
    }
}

int suspenders_sleep_dl(uint64_t deadline_ns) {
    suspenders_cr_t *cr;
    int pre = s_block_begin(&cr);
    if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;

    if (deadline_ns <= suspenders_now_ns()) {
        suspenders_yield();
        return SUSPENDERS_OK;
    }

    suspenders_wq_node_t *node = &cr->wq_node;
    node->cr = cr;
    node->status = S_WQ_WAITING;
    node->wait_list = NULL;
    node->wait_lock = NULL;

    suspenders_timer_t t;
    memset(&t, 0, sizeof(t));
    t.deadline_ns = deadline_ns;
    t.cb = s_sleep_cb;
    t.arg = cr;
    if (SUSPENDERS_UNLIKELY(!s_timer_arm(&t))) {
        node->status = SUSPENDERS_OK;
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }

    int st = s_wait_block();
    s_timer_disarm(&t);
    return st;
}

int suspenders_sleep_ns(uint64_t ns) {
    return suspenders_sleep_dl(suspenders_now_ns() + ns);
}

/* ============================================================================
 * DISPATCH QUEUE AND COROUTINE POOL
 * ============================================================================ */
typedef struct {
    void (*fn)(void*);
    void *arg;
} suspenders_task_t;

void suspenders_chan_destroy(suspenders_chan_t *ch) {
    if (!ch) return;
    memento_thread_heap_free(memento_thread_heap_get(), ch, sizeof(*ch));
}

/* -------------------------------------------------------------------------- */
/* Dispatch queue (serial task queue)                                         */
/* -------------------------------------------------------------------------- */
struct suspenders_dispatch_queue_s {
    char label[64];
    suspenders_chan_t *chan;
    suspenders_cr_t *worker;
    bool shutdown;
};

static void suspenders_dispatch_worker(void *arg) {
    suspenders_dispatch_queue_t *q = (suspenders_dispatch_queue_t*)arg;
    while (!q->shutdown) {
        suspenders_task_t task;
        memset(&task, 0, sizeof(task));
        if (!suspenders_chan_recv(q->chan, &task)) continue;
        if (!task.fn) break; /* sentinel */
        task.fn(task.arg);
    }
}

suspenders_dispatch_queue_t* suspenders_dispatch_queue_create(const char *label,
                                                               suspenders_qos_t qos) {
    suspenders_dispatch_queue_t *q = (suspenders_dispatch_queue_t*)malloc(sizeof(*q));
    if (!q) return NULL;
    memset(q, 0, sizeof(*q));
    if (label) {
        strncpy(q->label, label, sizeof(q->label) - 1);
        q->label[sizeof(q->label) - 1] = '\0';
    }
    q->chan = suspenders_chan_create(sizeof(suspenders_task_t), 0);
    if (!q->chan) {
        free(q);
        return NULL;
    }
    q->worker = suspenders_spawn(suspenders_dispatch_worker, q, qos);
    if (!q->worker) {
        suspenders_chan_destroy(q->chan);
        free(q);
        return NULL;
    }
    return q;
}

void suspenders_dispatch_queue_destroy(suspenders_dispatch_queue_t *q) {
    if (!q) return;
    q->shutdown = true;
    if (q->worker) suspenders_cancel(q->worker);
    suspenders_chan_destroy(q->chan);
    free(q);
}

void suspenders_dispatch_async(suspenders_dispatch_queue_t *q,
                               void (*fn)(void*), void *arg) {
    if (!q || q->shutdown || !fn) return;
    suspenders_task_t task = {fn, arg};
    suspenders_chan_send(q->chan, &task);
}

void suspenders_dispatch_barrier_async(suspenders_dispatch_queue_t *q,
                                       void (*fn)(void*), void *arg) {
    /* Serial queue: tasks are already executed in submission order, so a
     * barrier is equivalent to a normal async submission. */
    suspenders_dispatch_async(q, fn, arg);
}

/* -------------------------------------------------------------------------- */
/* Coroutine pool (N workers consuming a shared task channel)                 */
/* -------------------------------------------------------------------------- */
struct suspenders_pool_s {
    suspenders_chan_t *chan;
    unsigned nworkers;
    suspenders_cr_t **workers;
    bool shutdown;
};

static void suspenders_pool_worker(void *arg) {
    suspenders_pool_t *pool = (suspenders_pool_t*)arg;
    while (!pool->shutdown) {
        suspenders_task_t task;
        memset(&task, 0, sizeof(task));
        if (!suspenders_chan_recv(pool->chan, &task)) continue;
        if (!task.fn) break; /* sentinel */
        task.fn(task.arg);
    }
}

suspenders_pool_t* suspenders_pool_create(unsigned nworkers,
                                          suspenders_qos_t qos) {
    if (nworkers == 0) nworkers = 1;
    suspenders_pool_t *pool = (suspenders_pool_t*)malloc(sizeof(*pool));
    if (!pool) return NULL;
    memset(pool, 0, sizeof(*pool));
    pool->nworkers = nworkers;
    pool->chan = suspenders_chan_create(sizeof(suspenders_task_t), 0);
    if (!pool->chan) {
        free(pool);
        return NULL;
    }
    pool->workers = (suspenders_cr_t**)malloc(nworkers * sizeof(suspenders_cr_t*));
    if (!pool->workers) {
        suspenders_chan_destroy(pool->chan);
        free(pool);
        return NULL;
    }
    for (unsigned i = 0; i < nworkers; i++) {
        pool->workers[i] = suspenders_spawn(suspenders_pool_worker, pool, qos);
        if (!pool->workers[i]) {
            pool->shutdown = true;
            for (unsigned j = 0; j < i; j++) {
                suspenders_task_t sentinel;
                memset(&sentinel, 0, sizeof(sentinel));
                suspenders_chan_send(pool->chan, &sentinel);
            }
            suspenders_chan_destroy(pool->chan);
            free(pool->workers);
            free(pool);
            return NULL;
        }
    }
    return pool;
}

void suspenders_pool_destroy(suspenders_pool_t *pool) {
    if (!pool) return;
    pool->shutdown = true;
    for (unsigned i = 0; i < pool->nworkers; i++) {
        if (pool->workers[i]) suspenders_cancel(pool->workers[i]);
    }
    suspenders_chan_destroy(pool->chan);
    free(pool->workers);
    free(pool);
}

void suspenders_pool_submit(suspenders_pool_t *pool,
                            void (*fn)(void*), void *arg) {
    if (!pool || pool->shutdown || !fn) return;
    suspenders_task_t task = {fn, arg};
    suspenders_chan_send(pool->chan, &task);
}

/* ============================================================================
 * SCHEDULER INITIALIZATION & LIFECYCLE
 * ============================================================================ */
int suspenders_init(unsigned num_workers, unsigned queue_hint) {
    (void)num_workers;

    memento_init();
    memento_thread_heap_t *heap = memento_thread_heap_get();
    if (SUSPENDERS_UNLIKELY(!heap)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }

    suspenders_backend = s_backend_create(queue_hint);
    if (SUSPENDERS_UNLIKELY(!suspenders_backend)) {
        suspenders_errno = SUSPENDERS_ERROR;
        return SUSPENDERS_ERROR;
    }

    suspenders_transport_register(&tcp_transport_ops);
    suspenders_transport_register(&udp_transport_ops);
#if !defined(SUSPENDERS_PLATFORM_WINDOWS)
    suspenders_transport_register(&unix_transport_ops);
#endif
    suspenders_transport_register(&tty_transport_ops);

#ifdef SUSPENDERS_PLATFORM_WINDOWS
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    suspenders_initialized = 1;
    return SUSPENDERS_OK;
}

void suspenders_run(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) {
        fprintf(stderr, "suspenders: not initialized\n");
        return;
    }

    s_main_cr_get();

    suspenders_running = suspenders_main_cr;
    suspenders_main_cr->state = SUSPENDERS_STATE_RUNNING;

    while (1) {
        suspenders_cr_t *cr = suspenders_ready_dequeue();
        if (cr) {
            suspenders_running = cr;
            cr->state = SUSPENDERS_STATE_RUNNING;
            suspenders_ctx_switch(&suspenders_main_cr->ctx, &cr->ctx);
            suspenders_running = suspenders_main_cr;
            continue;
        }

        while (suspenders_atomic_load(zombie_head, SUSPENDERS_MEMORY_ORDER_RELAXED)) {
            suspenders_cr_t *zombie = (suspenders_cr_t*)suspenders_atomic_load(zombie_head, SUSPENDERS_MEMORY_ORDER_RELAXED);
            suspenders_atomic_store(zombie_head, (uintptr_t)zombie->next, SUSPENDERS_MEMORY_ORDER_RELAXED);
#ifdef SUSPENDERS_PLATFORM_WINDOWS
            if (zombie->ctx.fiber && zombie->ctx.fiber != suspenders_main_fiber) {
                DeleteFiber(zombie->ctx.fiber);
            }
#endif
            /* The cr_t lives inside its arena; destroying it frees both. */
            memento_arena_t *arena = zombie->arena;
            if (arena) memento_arena_destroy(arena);
        }

        if (suspenders_ready_queue_empty() &&
            suspenders_atomic_load(active_coroutines, SUSPENDERS_MEMORY_ORDER_RELAXED) == 0 &&
            suspenders_timer_heap.count == 0) {
            break;
        }

        if (suspenders_ready_queue_empty()) {
            int timeout_ms = -1;
            if (suspenders_timer_heap.count > 0) {
                uint64_t now = suspenders_now_ns();
                uint64_t next = suspenders_timer_heap.data[0]->deadline_ns;
                if (next <= now) {
                    timeout_ms = 0;
                } else {
                    uint64_t delta_ns = next - now;
                    timeout_ms = (int)(delta_ns / 1000000);
                    if (timeout_ms < 1) timeout_ms = 1;
                }
            }
            if (suspenders_backend) {
                s_backend_wait(suspenders_backend, timeout_ms);
            } else if (timeout_ms > 0) {
                struct timespec ts;
                ts.tv_sec = timeout_ms / 1000;
                ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
                nanosleep(&ts, NULL);
            }
            suspenders_timer_fire_expired();
        }
    }

    suspenders_main_cr->state = SUSPENDERS_STATE_READY;
}

void suspenders_shutdown(void) {
    if (suspenders_initialized) {
        if (suspenders_backend) {
            s_backend_destroy(suspenders_backend);
            suspenders_backend = NULL;
        }
        if (suspenders_timer_heap.data) {
            memento_thread_heap_free(memento_thread_heap_get(),
                                     suspenders_timer_heap.data,
                                     suspenders_timer_heap.cap * sizeof(suspenders_timer_t*));
            memset(&suspenders_timer_heap, 0, sizeof(suspenders_timer_heap));
        }
        /* Reset per-thread scheduler identity so a re-init starts clean. */
        suspenders_main_cr = NULL;
        suspenders_running = NULL;
        memento_shutdown();
        suspenders_initialized = 0;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        WSACleanup();
#endif
    }
}

/* ============================================================================
 * ASYNC I/O HELPERS
 * ============================================================================ */
#if SUSPENDERS_BACKEND_IOURING
void suspenders_writev_async(int fd, struct iovec *iovs, int count) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&suspenders_backend->ring);
    if (!sqe) return;
    io_uring_prep_writev(sqe, fd, iovs, count, 0);
    io_uring_sqe_set_data(sqe, suspenders_running);
    suspenders_suspend();
}
#endif


#ifdef __cplusplus
  #if defined(__clang__)
    #pragma clang diagnostic pop
  #elif defined(__GNUC__)
    #pragma GCC diagnostic pop
  #endif
#endif

#endif /* SUSPENDERS_IMPLEMENTATION */
