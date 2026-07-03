
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
#include <limits.h>

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

/* Backend selection (SUSPENDERS_FORCE_POLL pins the portable poll backend,
 * mainly to keep it verified on Linux CI) */
#if defined(SUSPENDERS_FORCE_POLL)
    #define SUSPENDERS_BACKEND_POLL 1
#elif SUSPENDERS_PLATFORM_LINUX
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
    /* Windows has no <sys/uio.h>; provide the iovec shape used by the
     * transport vtable (readv/writev return ENOTSUP there). */
    struct iovec { void *iov_base; size_t iov_len; };
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <poll.h>
    #include <errno.h>
    #include <sys/uio.h>
    #if SUSPENDERS_PLATFORM_BSD
        #include <sys/event.h>
        #include <sys/time.h>
    #endif
    #if SUSPENDERS_PLATFORM_LINUX
        #include <sys/syscall.h>
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
    suspenders_atomic_int status;   /* atomic: deciders may run on another worker */
    suspenders_atomic_int waker_busy; /* decider handshake: set before the status
                                       * flip, cleared after the wake — the waiter
                                       * must not recycle the node (or exit) while
                                       * the decider may still touch node/cr */
    struct suspenders_wq_list_s     *wait_list;
    struct suspenders_ticket_lock_s *wait_lock;
    void       *select_rec;   /* non-NULL for suspenders_select stack nodes */
    int         select_idx;
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
    suspenders_atomic_int state;  /* suspenders_state_t; atomic: advisory cross-worker reads */
    suspenders_atomic_int effective_qos;
    int              base_qos;
    memento_arena_t *arena;       /* Stack arena (also holds this struct) */
    suspenders_wq_node_t  wq_node;
    int32_t          io_result;   /* Result from last I/O op */
    struct suspenders_cr_s *next;      /* Ready queue / injector linkage (owner thread) */
    /* Identity & introspection */
    uint64_t         id;
    char             name[32];
    size_t           stack_size;
    /* Cancellation, deadline, cleanup */
    suspenders_atomic_int cancel_requested;
    suspenders_cleanup_t *cleanup_head;
    suspenders_timer_t    deadline_timer;   /* armed by suspenders_deadline */
    bool             deadline_armed;
    bool             daemon;   /* excluded from run-completion accounting
                                * (queue drainers); reclaimed at shutdown */
    /* Ownership: a coroutine pins to the worker that first runs it; all
     * cross-worker wakes go through that worker's MPSC inbox. inbox_next is
     * the inbox stack link — it must be distinct from `next`, which the
     * owner's ready queue uses concurrently. wake_inflight dedups inbox
     * pushes (a cr rides its single inbox_next); pending_wake is owner-only:
     * a wake that arrived while the cr was not suspended is re-delivered
     * when the cr next switches out instead of being dropped. */
    memento_thread_heap_t *owner_heap;
    suspenders_atomic_uintptr_t worker;   /* suspenders_worker_t*, written once at pin */
    struct suspenders_cr_s *inbox_next;
    suspenders_atomic_int wake_inflight;
    int              pending_wake;
    /* Owner-thread live list (s_live_crs): every pinned, unfinished cr is
     * tracked so shutdown can reclaim coroutines still parked mid-flight. */
    struct suspenders_cr_s *live_prev;
    struct suspenders_cr_s *live_next;
    /* In-flight completion-mode I/O op (io_uring); cancel/deadline route
     * through IORING_OP_ASYNC_CANCEL so exactly one CQE wakes the cr. */
    void            *pending_io;
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

/* Channel - buffered ring + zero-copy rendezvous fast path (FIFO waiters).
 * Invariants under lock: receivers parked => buffer empty;
 * senders parked => buffer full (or rendezvous). */
typedef struct {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) size_t elem_sz;
    size_t buf_sz;                 /* capacity in elements (0 = rendezvous) */
    suspenders_atomic_size_t count;/* buffered elements */
    size_t head;                   /* ring read index */
    char  *buf;                    /* ring storage (allocated with the chan) */
    bool   closed;
    suspenders_wq_list_t senders;
    suspenders_wq_list_t receivers;
    void  *alloc_base;             /* raw allocation (chan is 64-aligned inside) */
    size_t alloc_size;
    memento_thread_heap_t *owner_heap;
    suspenders_ticket_lock_t lock;
} suspenders_chan_t;

/* One case of a suspenders_select: send val to ch, or receive into val. */
typedef struct {
    suspenders_chan_t *ch;
    void *val;
    bool  is_send;
} suspenders_chan_op_t;

#define SUSPENDERS_SELECT_MAX 64

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
    ssize_t (*readv)(suspenders_hose_t *h, const struct iovec *iov, int iovcnt);
    ssize_t (*writev)(suspenders_hose_t *h, const struct iovec *iov, int iovcnt);
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

/* suspenders_hose_shutdown 'how' values (match SHUT_* / SD_*) */
#define SUSPENDERS_SHUT_RD   0
#define SUSPENDERS_SHUT_WR   1
#define SUSPENDERS_SHUT_RDWR 2

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

/* Task queues (libdispatch-style). A queue drains submitted tasks with
 * `concurrency` daemon coroutines (1 = serial: strict submission order).
 * Idle queues never keep suspenders_run alive; pending/running tasks do.
 * async: FIFO submit — blocks on a full queue inside a coroutine, returns
 *   SUSPENDERS_FULL outside one.
 * sync: submit and wait for completion (coroutine callers only).
 * after: run fn once `delay_ns` has elapsed (submitted at expiry).
 * barrier_async: fn runs alone — after every earlier task has finished and
 *   before any later one starts (plain async on a serial queue).
 * destroy: closes the queue; inside a coroutine it waits for the drain
 *   (pending tasks still run), outside one the queue is freed at
 *   suspenders_shutdown after its drainers exit. */
typedef struct suspenders_queue_s suspenders_queue_t;
typedef struct suspenders_pool_s suspenders_pool_t;

suspenders_queue_t* suspenders_queue_create(const char *label,
                                            suspenders_qos_t qos,
                                            unsigned concurrency);
suspenders_queue_t* suspenders_get_global_queue(suspenders_qos_t qos);
int  suspenders_queue_async(suspenders_queue_t *q, void (*fn)(void*), void *arg);
int  suspenders_queue_sync(suspenders_queue_t *q, void (*fn)(void*), void *arg);
int  suspenders_queue_after(suspenders_queue_t *q, uint64_t delay_ns,
                            void (*fn)(void*), void *arg);
int  suspenders_queue_barrier_async(suspenders_queue_t *q,
                                    void (*fn)(void*), void *arg);
void suspenders_queue_destroy(suspenders_queue_t *q);
const char* suspenders_queue_label(const suspenders_queue_t *q);

/* Coroutine pool: thin wrapper over a concurrent queue. */
suspenders_pool_t* suspenders_pool_create(unsigned nworkers,
                                          suspenders_qos_t qos);
void suspenders_pool_destroy(suspenders_pool_t *pool);
void suspenders_pool_submit(suspenders_pool_t *pool,
                            void (*fn)(void*), void *arg);

/* Channels. Blocking ops return SUSPENDERS_OK or an error code
 * (CLOSED / CANCELED / TIMEDOUT / PERM outside a coroutine). try ops
 * return FULL / EMPTY instead of blocking. close follows Go semantics:
 * receivers drain the buffer, then get SUSPENDERS_CLOSED; senders fail
 * immediately. */
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
suspenders_chan_t* suspenders_chan_make(size_t elem_sz, size_t buf_sz);
void suspenders_chan_destroy(suspenders_chan_t *ch);
int  suspenders_chan_send(suspenders_chan_t *ch, void *val);
int  suspenders_chan_send_dl(suspenders_chan_t *ch, void *val, uint64_t deadline_ns);
int  suspenders_chan_try_send(suspenders_chan_t *ch, void *val);
int  suspenders_chan_recv(suspenders_chan_t *ch, void *out);
int  suspenders_chan_recv_dl(suspenders_chan_t *ch, void *out, uint64_t deadline_ns);
int  suspenders_chan_try_recv(suspenders_chan_t *ch, void *out);
int  suspenders_chan_close(suspenders_chan_t *ch);

/* Wait on up to SUSPENDERS_SELECT_MAX channel cases with randomized
 * fairness. Returns the ready case's index (suspenders_errno is
 * SUSPENDERS_OK, or SUSPENDERS_CLOSED if that case's channel closed), or
 * a negative error (TIMEDOUT / CANCELED / INVAL / PERM). */
int suspenders_select(suspenders_chan_op_t *ops, int n);
int suspenders_select_dl(suspenders_chan_op_t *ops, int n, uint64_t deadline_ns);

/* Hose API - transport agnostic async I/O. Blocking ops suspend the calling
 * coroutine; on failure they return -1 (or false) with suspenders_errno set
 * (CANCELED / TIMEDOUT / ERROR + errno). _dl variants take an absolute
 * deadline from suspenders_now_ns (0 = wait forever) and fail with
 * SUSPENDERS_TIMEDOUT when it passes. */
void  suspenders_hose_init(suspenders_hose_t *d, struct buf *b);
bool  suspenders_hose_dial(suspenders_hose_t *d, const char *uri);
bool  suspenders_hose_dial_dl(suspenders_hose_t *d, const char *uri, uint64_t deadline_ns);
bool  suspenders_hose_listen(suspenders_hose_t *d, const char *uri);
bool  suspenders_hose_accept(suspenders_hose_t *d, suspenders_hose_t *client);
bool  suspenders_hose_accept_dl(suspenders_hose_t *d, suspenders_hose_t *client, uint64_t deadline_ns);
ssize_t suspenders_hose_read(suspenders_hose_t *d, void *dest, size_t len);
ssize_t suspenders_hose_read_dl(suspenders_hose_t *d, void *dest, size_t len, uint64_t deadline_ns);
ssize_t suspenders_hose_write(suspenders_hose_t *d, const void *src, size_t len);
ssize_t suspenders_hose_write_dl(suspenders_hose_t *d, const void *src, size_t len, uint64_t deadline_ns);
ssize_t suspenders_hose_readv(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
ssize_t suspenders_hose_writev(suspenders_hose_t *d, const struct iovec *iov, int iovcnt);
ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen);
ssize_t suspenders_hose_recvfrom_dl(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen, uint64_t deadline_ns);
ssize_t suspenders_hose_sendto(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen);
ssize_t suspenders_hose_sendto_dl(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen, uint64_t deadline_ns);

/* SUSPENDERS_SHUT_RD / SHUT_WR / SHUT_RDWR (plain shutdown(2) values) */
int   suspenders_hose_shutdown(suspenders_hose_t *d, int how);
int   suspenders_hose_set_option(suspenders_hose_t *d, int level, int optname,
                                 const void *optval, socklen_t optlen);
int   suspenders_hose_peername(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen);
int   suspenders_hose_sockname(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen);
void  suspenders_hose_close(suspenders_hose_t *d);

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

#ifndef SUSPENDERS_PLATFORM_WINDOWS
    #include <pthread.h>
#endif
#if SUSPENDERS_PLATFORM_LINUX
    #include <sys/eventfd.h>
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
 * storage: naturally 64-byte aligned and immune to allocator lifetime.
 * Every worker thread (including the suspenders_run caller, worker 0) has
 * its own copy of all of this: private ready queues, backend, timer heap. */
static SUSPENDERS_TLS suspenders_cr_t *suspenders_running = NULL;
static SUSPENDERS_TLS suspenders_cr_t suspenders_main_cr_storage;
static SUSPENDERS_TLS suspenders_cr_t *suspenders_main_cr = NULL;
static SUSPENDERS_TLS suspenders_cr_t *ready_queue_heads[SUSPENDERS_QOS_COUNT] = {0};
static SUSPENDERS_TLS suspenders_cr_t *ready_queue_tails[SUSPENDERS_QOS_COUNT] = {0};
static SUSPENDERS_TLS suspenders_cr_t *s_zombies = NULL;   /* crs are pinned: exit is local */
static SUSPENDERS_TLS suspenders_cr_t *s_live_crs = NULL;  /* pinned, unfinished crs (owner thread) */
static SUSPENDERS_TLS int suspenders_initialized = 0;
static suspenders_atomic_int active_coroutines = 0;
static suspenders_atomic_uintptr_t suspenders_next_id = 1;
/* Tasks submitted to queues but not yet finished. Daemon drainers are not
 * counted in active_coroutines, so this is what keeps suspenders_run alive
 * while queue work is pending or executing. */
static suspenders_atomic_int s_queue_work = 0;

/* wq_node.status while a waiter is parked; decided waits carry an error
 * code (SUSPENDERS_OK / _CANCELED / _TIMEDOUT), all <= 0. */
#define S_WQ_WAITING 1

/* Timer internals (definitions in the TIMERS section below) */
static bool s_timer_arm(suspenders_timer_t *t);
static void s_timer_disarm(suspenders_timer_t *t);
#if SUSPENDERS_BACKEND_IOURING
/* Submit IORING_OP_ASYNC_CANCEL for an in-flight completion-mode op; the
 * canceled op's own CQE (-ECANCELED) then wakes the coroutine exactly once. */
static void s_iou_cancel(void *io_op);
#endif

/* Backend instance */
static SUSPENDERS_TLS struct suspenders_backend_s *suspenders_backend = NULL;

/* ============================================================================
 * WORKERS - one scheduler thread each. A coroutine pins to the worker that
 * first runs it (taken from the global injector); after that, every
 * cross-worker wake goes through the pinned worker's MPSC inbox (Treiber
 * stack riding on cr->next, deduped by cr->wake_inflight) plus an eventfd
 * kick when the worker is parked in its backend wait. The eventfd stays
 * readable until drained, so a signal that races the park is never lost.
 * ============================================================================ */
typedef struct suspenders_worker_s {
    SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE)
    suspenders_atomic_uintptr_t inbox;   /* MPSC stack of suspenders_cr_t* */
    suspenders_atomic_int parked;        /* 1 while blocked in backend wait */
    int  index;
    suspenders_sock_t wake_rd;           /* eventfd (rd == wr) or pipe */
    suspenders_sock_t wake_wr;
#ifndef SUSPENDERS_PLATFORM_WINDOWS
    pthread_t thread;                    /* helper workers only */
#endif
    void *wake_op;                       /* io_uring: this worker's wake sentinel op */
} suspenders_worker_t;

static suspenders_worker_t *s_workers = NULL;
static unsigned s_num_workers = 0;
static unsigned s_queue_hint = 256;
static suspenders_atomic_int s_stop = 0;
static SUSPENDERS_TLS suspenders_worker_t *s_worker = NULL;  /* this thread's worker */

/* Global injector: freshly spawned coroutines land here and are taken by
 * whichever worker goes idle first (or by worker 0 in single-worker mode
 * when spawned from a foreign thread). */
static suspenders_ticket_lock_t s_injector_lock;
static suspenders_cr_t *s_injector_heads[SUSPENDERS_QOS_COUNT];
static suspenders_cr_t *s_injector_tails[SUSPENDERS_QOS_COUNT];
static suspenders_atomic_int s_injector_count = 0;

/* --- worker wake fd (eventfd on Linux, pipe elsewhere) ------------------- */
static bool s_wake_fd_create(suspenders_worker_t *w) {
#if SUSPENDERS_PLATFORM_LINUX
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) return false;
    w->wake_rd = w->wake_wr = fd;
    return true;
#elif defined(SUSPENDERS_PLATFORM_WINDOWS)
    /* Multi-worker is POSIX-only for now; single worker never signals. */
    w->wake_rd = w->wake_wr = SUSPENDERS_INVALID_SOCK;
    return true;
#else
    int fds[2];
    if (pipe(fds) != 0) return false;
    (void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
    (void)fcntl(fds[1], F_SETFL, O_NONBLOCK);
    w->wake_rd = fds[0];
    w->wake_wr = fds[1];
    return true;
#endif
}

static void s_wake_fd_destroy(suspenders_worker_t *w) {
#ifndef SUSPENDERS_PLATFORM_WINDOWS
    if (w->wake_rd >= 0) close(w->wake_rd);
    if (w->wake_wr >= 0 && w->wake_wr != w->wake_rd) close(w->wake_wr);
#endif
    w->wake_rd = w->wake_wr = SUSPENDERS_INVALID_SOCK;
}

static void s_wake_drain(suspenders_worker_t *w) {
#ifndef SUSPENDERS_PLATFORM_WINDOWS
    char buf[64];
    while (read(w->wake_rd, buf, sizeof(buf)) > 0) { /* drain */ }
#else
    (void)w;
#endif
}

/* Kick a worker if (and only if) it might be parked. The eventfd stays
 * readable until drained, so signal-vs-park races cannot lose the wake. */
static void s_worker_signal(suspenders_worker_t *w) {
#ifndef SUSPENDERS_PLATFORM_WINDOWS
    if (w->wake_wr < 0) return;
    if (suspenders_atomic_load(w->parked, SUSPENDERS_MEMORY_ORDER_SEQ_CST)) {
#if SUSPENDERS_PLATFORM_LINUX
        uint64_t one = 1;
        ssize_t r = write(w->wake_wr, &one, sizeof(one));
#else
        char one = 1;
        ssize_t r = write(w->wake_wr, &one, 1);
#endif
        (void)r;
    }
#else
    (void)w;
#endif
}

/* Cross-worker wake: push the cr onto its pinned worker's inbox. The
 * wake_inflight flag dedups concurrent pushes (a cr can only ride its
 * single inbox_next link once); the consumer clears it after popping. A
 * skipped push is never lost: the pending entry either delivers, or the
 * consumer records pending_wake and re-delivers at the cr's next park. */
/* cr->worker is written once when the cr pins and read by foreign threads
 * (resume/cancel routing) — seq_cst so it pairs with the cancel_requested
 * handshake: a canceler that reads no pin stored the flag before the pin,
 * so s_injector_take's flag check sees it (Dekker). */
static inline suspenders_worker_t* s_cr_worker(suspenders_cr_t *cr) {
    return (suspenders_worker_t*)suspenders_atomic_load(cr->worker,
                                                        SUSPENDERS_MEMORY_ORDER_SEQ_CST);
}

static void s_inbox_wake(suspenders_cr_t *cr) {
    suspenders_worker_t *w = s_cr_worker(cr);
    if (SUSPENDERS_UNLIKELY(!w)) return;   /* not yet pinned: still injector-owned */
    if (suspenders_atomic_exchange(cr->wake_inflight, 1,
                                   SUSPENDERS_MEMORY_ORDER_ACQ_REL) != 0) {
        return;   /* already queued on the inbox */
    }
    uintptr_t head;
    do {
        head = suspenders_atomic_load(w->inbox, SUSPENDERS_MEMORY_ORDER_RELAXED);
        cr->inbox_next = (suspenders_cr_t*)head;
    } while (!suspenders_atomic_compare_exchange_weak(w->inbox, head, (uintptr_t)cr,
                                                      SUSPENDERS_MEMORY_ORDER_SEQ_CST,
                                                      SUSPENDERS_MEMORY_ORDER_RELAXED));
    s_worker_signal(w);
}

/* Transport registry */
static const suspenders_transport_ops_t *suspenders_transport_registry[16];
static int suspenders_transport_count = 0;

#ifdef SUSPENDERS_PLATFORM_WINDOWS
static SUSPENDERS_TLS void *suspenders_main_fiber = NULL;
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
    stack_high[0] = (void*)(uintptr_t)suspenders_cr_exit;   /* via uintptr_t: ISO C has no fn->obj ptr cast */
    ctx->rip = (void*)(uintptr_t)_suspenders_asm_entry;
    ctx->rsp = stack_high;
    ctx->r12 = (void*)(uintptr_t)func;
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
    ctx->x[1] = (void*)(uintptr_t)func;   /* via uintptr_t: ISO C has no fn->obj ptr cast */
    ctx->x[2] = (void*)0xdeaddeaddeaddead;
    ctx->sp = (void*)((char*)stack_base + stack_size);
    ctx->lr = (void*)(uintptr_t)_suspenders_asm_entry;
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
/* Owner-thread live list: linked when a cr pins to this worker, unlinked at
 * exit. Whatever remains at teardown is a still-parked coroutine whose arena
 * would otherwise leak on shutdown-while-busy. */
static inline void s_live_link(suspenders_cr_t *cr) {
    cr->live_prev = NULL;
    cr->live_next = s_live_crs;
    if (s_live_crs) s_live_crs->live_prev = cr;
    s_live_crs = cr;
}

static inline void s_live_unlink(suspenders_cr_t *cr) {
    if (cr->live_prev) cr->live_prev->live_next = cr->live_next;
    else if (s_live_crs == cr) s_live_crs = cr->live_next;
    if (cr->live_next) cr->live_next->live_prev = cr->live_prev;
    cr->live_prev = cr->live_next = NULL;
}

static inline void suspenders_zombie_enqueue(suspenders_cr_t *cr) {
    /* Coroutines are pinned: exit always happens on the owning worker, so
     * the zombie list is plain TLS. */
    s_live_unlink(cr);
    cr->next = s_zombies;
    s_zombies = cr;
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
            if (!cr->daemon &&
                suspenders_atomic_fetch_sub(active_coroutines, 1,
                    SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 1 &&
                s_workers && s_worker != &s_workers[0]) {
                /* Last coroutine finished on a helper: worker 0 may be parked
                 * waiting to detect termination — kick it. */
                s_worker_signal(&s_workers[0]);
            }
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
    node->waker_busy = 0;
    node->wait_list = list;
    node->wait_lock = lock;
    node->select_rec = NULL;
    s_wq_push(list, node);
}

/* Decide a wait. The status flip is the linearization point: the waiter may
 * observe it (via a stray wake) and race ahead without ever being resumed,
 * recycling the node — or exiting and freeing the whole cr. waker_busy is
 * set first so the waiter cannot return until the decider has finished its
 * wake call; every grant MUST be followed by s_wait_decide_end after the
 * resume/wake. */
static inline void s_wait_grant(suspenders_wq_node_t *n, int status) {
    n->waker_busy = 1;   /* seq_cst store, ordered before the status store */
    n->wait_list = NULL;
    n->status = status;
}

static inline void s_wait_decide_end(suspenders_wq_node_t *n) {
    suspenders_atomic_store(n->waker_busy, 0, SUSPENDERS_MEMORY_ORDER_RELEASE);
}

/* Close the decider window for a chain of granted+woken nodes (linked via
 * `next`, which stays ours while waker_busy traps each waiter in
 * s_wait_decider_sync). MUST run after the structure's ticket lock is
 * released: a released waiter may free the structure — including that very
 * lock — immediately, so the final unlock has to happen while every waiter
 * is still trapped. */
static inline void s_wait_decide_end_chain(suspenders_wq_node_t *n) {
    while (n) {
        suspenders_wq_node_t *next = n->next;
        n->next = NULL;
        s_wait_decide_end(n);
        n = next;
    }
}

/* Waiter side: wait out a decider still inside its grant→wake window. The
 * decider may be a coroutine on this very worker that got preempted between
 * the status flip and the wake, so yield while spinning. */
static void s_wait_decider_sync(suspenders_wq_node_t *node) {
    int spins = 0;
    while (suspenders_atomic_load(node->waker_busy, SUSPENDERS_MEMORY_ORDER_ACQUIRE)) {
        if (SUSPENDERS_UNLIKELY(++spins > 64)) {
            suspenders_yield();
            spins = 0;
        } else {
#if defined(SUSPENDERS_ARCH_X86_64)
            __builtin_ia32_pause();
#elif defined(SUSPENDERS_ARCH_AARCH64)
            __asm__ volatile("yield" ::: "memory");
#endif
        }
    }
}

/* Suspend until the wait is decided; returns the final status. Consumes a
 * pending cancel request when it delivered the wake. Call after releasing
 * the structure's lock. */
static int s_wait_block(void) {
    suspenders_wq_node_t *node = &suspenders_running->wq_node;
    while (node->status == S_WQ_WAITING) suspenders_suspend();
    s_wait_decider_sync(node);
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

static void s_wait_finish_wake(suspenders_wq_node_t *n);

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
        }
        node->waker_busy = 1;   /* before the status flip; see s_wait_grant */
        node->wait_list = NULL;
        node->status = status;
        won = true;
    }
    if (lk) suspenders_ticket_unlock(lk);
    if (won) s_wait_finish_wake(node);
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
    if (SUSPENDERS_UNLIKELY(!cr)) return;
    suspenders_worker_t *w = s_cr_worker(cr);
    if (SUSPENDERS_UNLIKELY(w && w != s_worker)) {
        /* Pinned to another worker: route through its inbox. The owner
         * discards the wake if the cr turns out not to be suspended. */
        s_inbox_wake(cr);
        return;
    }
    if (cr->state == SUSPENDERS_STATE_RUNNING || cr->state == SUSPENDERS_STATE_READY ||
        cr->state == SUSPENDERS_STATE_DONE) return;
    suspenders_ready_enqueue(cr);
    if (suspenders_running && suspenders_running != suspenders_main_cr &&
        suspenders_running->effective_qos > cr->effective_qos) {
        suspenders_ready_enqueue(suspenders_running);
        suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
    }
}

/* Wake a granted waiter without ever yielding. Batch grant/broadcast paths
 * call this while still holding the structure's ticket lock — a node must
 * not be touched after its grant (the waiter can observe the status and
 * recycle it), so the wake has to happen inline, and the preempting
 * suspenders_resume is off-limits under a spin lock. */
static void s_wake_no_preempt(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(!cr)) return;
    suspenders_worker_t *w = s_cr_worker(cr);
    if (SUSPENDERS_UNLIKELY(w && w != s_worker)) {
        s_inbox_wake(cr);
        return;
    }
    if (cr->state == SUSPENDERS_STATE_RUNNING || cr->state == SUSPENDERS_STATE_READY ||
        cr->state == SUSPENDERS_STATE_DONE) return;
    suspenders_ready_enqueue(cr);
}

/* Wake after a grant, close the decider window, then honor QoS preemption.
 * Call after releasing the structure's lock. The preempting
 * suspenders_resume must not run while waker_busy is set: the woken cr
 * would spin in s_wait_decider_sync while the strict QoS queues starve
 * this (lower-priority) granter of the chance to clear the flag. */
static void s_wait_finish_wake(suspenders_wq_node_t *n) {
    suspenders_cr_t *cr = n->cr;
    bool local = (s_cr_worker(cr) == s_worker);
    int qos = suspenders_atomic_load(cr->effective_qos, SUSPENDERS_MEMORY_ORDER_RELAXED);
    s_wake_no_preempt(cr);
    s_wait_decide_end(n);
    if (local && suspenders_running && suspenders_running != suspenders_main_cr &&
        suspenders_running->effective_qos > qos) {
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
    suspenders_atomic_store(cr->cancel_requested, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
    suspenders_worker_t *w = s_cr_worker(cr);
    if (SUSPENDERS_UNLIKELY(w && w != s_worker)) {
        /* Deliver on the owning worker (inbox consumer re-runs this logic
         * locally): a foreign s_waiter_wake could read a stale wait_lock
         * while the target re-parks, and I/O delivery touches the owner's
         * ring/io_result. */
        s_inbox_wake(cr);
        return SUSPENDERS_OK;
    }
    /* Local: the target cannot be mid-park/re-park, so its wq fields are
     * stable and the wait can be decided under the structure's lock. */
    if (s_waiter_wake(cr, SUSPENDERS_CANCELED)) return SUSPENDERS_OK;
    if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
#if SUSPENDERS_BACKEND_IOURING
        if (cr->pending_io) {
            s_iou_cancel(cr->pending_io);
            return SUSPENDERS_OK;   /* the op's own CQE delivers the wake */
        }
#endif
        /* Blocked on readiness I/O or a bare suspend: deliver via io_result. */
        cr->io_result = -1;
        suspenders_resume(cr);
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
#if SUSPENDERS_BACKEND_IOURING
    if (cr->pending_io) {
        s_iou_cancel(cr->pending_io);   /* op's own CQE delivers the wake */
        return;
    }
#endif
    if (!s_waiter_wake(cr, SUSPENDERS_TIMEDOUT) &&
        cr->state == SUSPENDERS_STATE_SUSPENDED) {
        /* Blocked on readiness I/O or a bare suspend: -2 = timeout sentinel
         * (-1 = canceled), consumed by s_io_wait_ready. */
        cr->io_result = -2;
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

static suspenders_cr_t* s_spawn_impl(void (*func)(void*), void *arg,
                                     suspenders_qos_t qos, bool daemon) {
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

    cr->daemon = daemon;
    if (!daemon) {
        suspenders_atomic_fetch_add(active_coroutines, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
    }

    if (s_num_workers > 1 || SUSPENDERS_UNLIKELY(!s_worker)) {
        /* Fresh spawns go to the global injector; the coroutine pins to
         * whichever worker takes it first. (Also the path for spawns from
         * foreign threads, which have no local ready queue.) */
        int q = suspenders_clamp_qos((int)qos);
        suspenders_ticket_lock(&s_injector_lock);
        cr->next = NULL;
        if (s_injector_tails[q]) s_injector_tails[q]->next = cr;
        else s_injector_heads[q] = cr;
        s_injector_tails[q] = cr;
        suspenders_atomic_fetch_add(s_injector_count, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        suspenders_ticket_unlock(&s_injector_lock);
        for (unsigned i = 0; i < s_num_workers; i++) {
            if (suspenders_atomic_load(s_workers[i].parked,
                                       SUSPENDERS_MEMORY_ORDER_SEQ_CST)) {
                s_worker_signal(&s_workers[i]);
                break;
            }
        }
    } else {
        suspenders_atomic_store(cr->worker, (uintptr_t)s_worker,
                                SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        s_live_link(cr);
        suspenders_ready_enqueue(cr);
    }

    return cr;
}

suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos) {
    return s_spawn_impl(func, arg, qos, false);
}

suspenders_cr_t* suspenders_go(void (*func)(void*), void *arg) {
    return s_spawn_impl(func, arg, SUSPENDERS_QOS_NORMAL, false);
}

/* ============================================================================
 * CHANNEL - buffered ring + zero-copy rendezvous, close, try ops, select
 * ============================================================================ */

/* Select record shared by one suspenders_select call's stack nodes. */
typedef struct {
    suspenders_atomic_int winner;   /* case index, -1 until decided */
} s_select_rec_t;

/* Pop the next usable waiter from a channel wait list (caller holds the
 * channel lock). Select nodes must win their select's winner CAS; losers
 * (their select was already decided by another channel) are discarded. */
static suspenders_wq_node_t* s_chan_take_waiter(suspenders_wq_list_t *list) {
    suspenders_wq_node_t *n;
    while ((n = s_wq_pop(list)) != NULL) {
        if (!n->select_rec) return n;
        s_select_rec_t *rec = (s_select_rec_t*)n->select_rec;
        int expect = -1;
        if (suspenders_atomic_compare_exchange_strong(rec->winner, expect, n->select_idx,
                                                      SUSPENDERS_MEMORY_ORDER_ACQ_REL,
                                                      SUSPENDERS_MEMORY_ORDER_ACQUIRE)) {
            return n;
        }
        n->wait_list = NULL;   /* loser */
    }
    return NULL;
}

static inline void s_chan_buf_push(suspenders_chan_t *ch, const void *val) {
    size_t count = suspenders_atomic_load(ch->count, SUSPENDERS_MEMORY_ORDER_RELAXED);
    size_t idx = (ch->head + count) % ch->buf_sz;
    memcpy(ch->buf + idx * ch->elem_sz, val, ch->elem_sz);
    suspenders_atomic_store(ch->count, count + 1, SUSPENDERS_MEMORY_ORDER_RELAXED);
}

static inline void s_chan_buf_pop(suspenders_chan_t *ch, void *out) {
    size_t count = suspenders_atomic_load(ch->count, SUSPENDERS_MEMORY_ORDER_RELAXED);
    memcpy(out, ch->buf + ch->head * ch->elem_sz, ch->elem_sz);
    ch->head = (ch->head + 1) % ch->buf_sz;
    suspenders_atomic_store(ch->count, count - 1, SUSPENDERS_MEMORY_ORDER_RELAXED);
}

suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz) {
    if (SUSPENDERS_UNLIKELY(elem_sz == 0)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return NULL;
    }
    /* Over-allocate so the 64-byte-aligned chan (and its ring, right after
     * it) fit regardless of the heap's natural 16-byte alignment. */
    memento_thread_heap_t *heap = memento_thread_heap_get();
    size_t total = sizeof(suspenders_chan_t) + SUSPENDERS_CACHELINE + elem_sz * buf_sz;
    void *base = memento_thread_heap_alloc(heap, total);
    if (SUSPENDERS_UNLIKELY(!base)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    uintptr_t aligned = ((uintptr_t)base + SUSPENDERS_CACHELINE - 1) &
                        ~((uintptr_t)SUSPENDERS_CACHELINE - 1);
    suspenders_chan_t *ch = (suspenders_chan_t*)aligned;
    memset(ch, 0, sizeof(*ch));
    ch->elem_sz = elem_sz;
    ch->buf_sz = buf_sz;
    ch->buf = buf_sz ? (char*)(ch + 1) : NULL;
    ch->alloc_base = base;
    ch->alloc_size = total;
    ch->owner_heap = heap;
    suspenders_ticket_init(&ch->lock);
    return ch;
}

suspenders_chan_t* suspenders_chan_make(size_t elem_sz, size_t buf_sz) {
    return suspenders_chan_create(elem_sz, buf_sz);
}

void suspenders_chan_destroy(suspenders_chan_t *ch) {
    if (!ch) return;
    memento_thread_heap_free(ch->owner_heap, ch->alloc_base, ch->alloc_size);
}

static int s_chan_send_impl(suspenders_chan_t *ch, void *val,
                            uint64_t deadline_ns, bool try_only) {
    if (SUSPENDERS_UNLIKELY(!ch || !val)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr = suspenders_running;
    bool can_block = !try_only && cr && cr != suspenders_main_cr;
    if (can_block) {
        int pre = s_pre_block(cr);
        if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;
    }

    suspenders_ticket_lock(&ch->lock);

    if (SUSPENDERS_UNLIKELY(ch->closed)) {
        suspenders_ticket_unlock(&ch->lock);
        suspenders_errno = SUSPENDERS_CLOSED;
        return SUSPENDERS_CLOSED;
    }

    suspenders_wq_node_t *rx = s_chan_take_waiter(&ch->receivers);
    if (rx) {
        memcpy(rx->data_ptr, val, ch->elem_sz);
        s_wait_grant(rx, SUSPENDERS_OK);
        suspenders_ticket_unlock(&ch->lock);
        s_wait_finish_wake(rx);
        return SUSPENDERS_OK;
    }

    if (ch->buf_sz &&
        suspenders_atomic_load(ch->count, SUSPENDERS_MEMORY_ORDER_RELAXED) < ch->buf_sz) {
        s_chan_buf_push(ch, val);
        suspenders_ticket_unlock(&ch->lock);
        return SUSPENDERS_OK;
    }

    if (!can_block) {
        suspenders_ticket_unlock(&ch->lock);
        int st = try_only ? SUSPENDERS_FULL : SUSPENDERS_PERM;
        suspenders_errno = st;
        return st;
    }

    s_wait_park(&ch->senders, &ch->lock, val);
    suspenders_ticket_unlock(&ch->lock);
    return s_wait_block_dl(deadline_ns);
}

static int s_chan_recv_impl(suspenders_chan_t *ch, void *out,
                            uint64_t deadline_ns, bool try_only) {
    if (SUSPENDERS_UNLIKELY(!ch || !out)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr = suspenders_running;
    bool can_block = !try_only && cr && cr != suspenders_main_cr;
    if (can_block) {
        int pre = s_pre_block(cr);
        if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;
    }

    suspenders_ticket_lock(&ch->lock);

    if (ch->buf_sz &&
        suspenders_atomic_load(ch->count, SUSPENDERS_MEMORY_ORDER_RELAXED) > 0) {
        s_chan_buf_pop(ch, out);
        /* A parked sender can now deposit into the freed slot (FIFO). */
        suspenders_wq_node_t *tx = s_chan_take_waiter(&ch->senders);
        if (tx) {
            s_chan_buf_push(ch, tx->data_ptr);
            s_wait_grant(tx, SUSPENDERS_OK);
            suspenders_ticket_unlock(&ch->lock);
            s_wait_finish_wake(tx);
        } else {
            suspenders_ticket_unlock(&ch->lock);
        }
        return SUSPENDERS_OK;
    }

    suspenders_wq_node_t *tx = s_chan_take_waiter(&ch->senders);
    if (tx) {
        memcpy(out, tx->data_ptr, ch->elem_sz);
        s_wait_grant(tx, SUSPENDERS_OK);
        suspenders_ticket_unlock(&ch->lock);
        s_wait_finish_wake(tx);
        return SUSPENDERS_OK;
    }

    if (SUSPENDERS_UNLIKELY(ch->closed)) {
        suspenders_ticket_unlock(&ch->lock);
        suspenders_errno = SUSPENDERS_CLOSED;
        return SUSPENDERS_CLOSED;
    }

    if (!can_block) {
        suspenders_ticket_unlock(&ch->lock);
        int st = try_only ? SUSPENDERS_EMPTY : SUSPENDERS_PERM;
        suspenders_errno = st;
        return st;
    }

    s_wait_park(&ch->receivers, &ch->lock, out);
    suspenders_ticket_unlock(&ch->lock);
    return s_wait_block_dl(deadline_ns);
}

int suspenders_chan_send(suspenders_chan_t *ch, void *val) {
    return s_chan_send_impl(ch, val, 0, false);
}

int suspenders_chan_send_dl(suspenders_chan_t *ch, void *val, uint64_t deadline_ns) {
    return s_chan_send_impl(ch, val, deadline_ns, false);
}

int suspenders_chan_try_send(suspenders_chan_t *ch, void *val) {
    return s_chan_send_impl(ch, val, 0, true);
}

int suspenders_chan_recv(suspenders_chan_t *ch, void *out) {
    return s_chan_recv_impl(ch, out, 0, false);
}

int suspenders_chan_recv_dl(suspenders_chan_t *ch, void *out, uint64_t deadline_ns) {
    return s_chan_recv_impl(ch, out, deadline_ns, false);
}

int suspenders_chan_try_recv(suspenders_chan_t *ch, void *out) {
    return s_chan_recv_impl(ch, out, 0, true);
}

int suspenders_chan_close(suspenders_chan_t *ch) {
    if (SUSPENDERS_UNLIKELY(!ch)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&ch->lock);
    if (ch->closed) {
        suspenders_ticket_unlock(&ch->lock);
        suspenders_errno = SUSPENDERS_CLOSED;
        return SUSPENDERS_CLOSED;
    }
    ch->closed = true;

    /* Fail every parked waiter with CLOSED (buffered data stays drainable;
     * receivers only park when the buffer is empty). Grant+wake under the
     * lock; the decider windows stay open across the unlock so no waiter
     * can free the channel before we release its lock. */
    suspenders_wq_node_t *n;
    suspenders_wq_node_t *granted = NULL;
    suspenders_wq_list_t *lists[2] = { &ch->senders, &ch->receivers };
    for (int i = 0; i < 2; i++) {
        while ((n = s_chan_take_waiter(lists[i])) != NULL) {
            s_wait_grant(n, SUSPENDERS_CLOSED);
            s_wake_no_preempt(n->cr);
            n->next = granted;
            granted = n;
        }
    }
    suspenders_ticket_unlock(&ch->lock);
    s_wait_decide_end_chain(granted);
    return SUSPENDERS_OK;
}

/* ---------------------------------------------------------------------------
 * SELECT - randomized-fairness multi-channel wait
 * ------------------------------------------------------------------------- */
static SUSPENDERS_TLS uint32_t s_select_seed = 0;

static uint32_t s_rand32(void) {
    uint32_t x = s_select_seed;
    if (x == 0) x = (uint32_t)(suspenders_now_ns() | 1);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_select_seed = x;
    return x;
}

/* True when a select case could complete right now (caller holds ch->lock).
 * Closes the lost-wakeup window between the lock-free try-scan and node
 * registration: a counterpart that parked in that window saw no waiter and
 * will never wake us. Nodes of this select (rec matches) don't count; stale
 * losers of other selects do — the rescan pass discards them and re-parks. */
static bool s_select_case_ready(const suspenders_chan_op_t *op,
                                const s_select_rec_t *rec) {
    suspenders_chan_t *ch = op->ch;
    if (ch->closed) return true;
    size_t count = suspenders_atomic_load(ch->count, SUSPENDERS_MEMORY_ORDER_RELAXED);
    const suspenders_wq_node_t *w;
    if (op->is_send) {
        if (ch->buf_sz && count < ch->buf_sz) return true;
        for (w = ch->receivers.head; w; w = w->next)
            if (w->select_rec != (const void*)rec) return true;
    } else {
        if (ch->buf_sz && count > 0) return true;
        for (w = ch->senders.head; w; w = w->next)
            if (w->select_rec != (const void*)rec) return true;
    }
    return false;
}

int suspenders_select_dl(suspenders_chan_op_t *ops, int n, uint64_t deadline_ns) {
    if (SUSPENDERS_UNLIKELY(!ops || n <= 0 || n > SUSPENDERS_SELECT_MAX)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_cr_t *cr = suspenders_running;
    bool can_block = cr && cr != suspenders_main_cr;
    if (can_block) {
        int pre = s_pre_block(cr);
        if (SUSPENDERS_UNLIKELY(pre != 0)) return pre;
    }

    /* Randomized-start try scan: a ready (or closed) case wins outright. */
    uint32_t r = s_rand32();
    for (int i = 0; i < n; i++) {
        int idx = (int)((r + (uint32_t)i) % (uint32_t)n);
        suspenders_chan_op_t *op = &ops[idx];
        int st = op->is_send ? suspenders_chan_try_send(op->ch, op->val)
                             : suspenders_chan_try_recv(op->ch, op->val);
        if (st == SUSPENDERS_OK || st == SUSPENDERS_CLOSED) {
            suspenders_errno = st;
            return idx;
        }
    }

    if (!can_block) {
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }

    /* Nothing ready: park a stack node on every channel. Cancellation and
     * deadlines are delivered through cr->wq_node (no list). */
    s_select_rec_t rec;
    suspenders_atomic_init(rec.winner, -1);
    suspenders_wq_node_t nodes[SUSPENDERS_SELECT_MAX];

    suspenders_wq_node_t *cnode = &cr->wq_node;
    cnode->cr = cr;
    cnode->status = S_WQ_WAITING;
    cnode->waker_busy = 0;
    cnode->wait_list = NULL;
    cnode->wait_lock = NULL;
    cnode->select_rec = NULL;

    suspenders_timer_t t;
    bool armed = false;
    if (deadline_ns) {
        memset(&t, 0, sizeof(t));
        t.deadline_ns = deadline_ns;
        t.cb = s_wait_deadline_cb;
        t.arg = cr;
        armed = s_timer_arm(&t);
    }

    int widx = -1;
    int scanidx = -1, scanst = 0;   /* case completed by a rescan pass */
    for (;;) {
        /* Register on every channel, re-checking readiness under each lock:
         * a counterpart that parked after the try-scan never saw us and
         * would never wake us. On readiness, stop and rescan instead. */
        bool rescan = false;
        int registered = 0;
        for (int i = 0; i < n; i++) {
            suspenders_wq_node_t *node = &nodes[i];
            node->cr = cr;
            node->data_ptr = ops[i].val;
            node->status = S_WQ_WAITING;
            node->waker_busy = 0;
            node->select_rec = &rec;
            node->select_idx = i;
            suspenders_chan_t *ch = ops[i].ch;
            suspenders_ticket_lock(&ch->lock);
            if (s_select_case_ready(&ops[i], &rec)) {
                suspenders_ticket_unlock(&ch->lock);
                rescan = true;
                break;
            }
            suspenders_wq_list_t *list = ops[i].is_send ? &ch->senders : &ch->receivers;
            node->wait_list = list;
            node->wait_lock = &ch->lock;
            s_wq_push(list, node);
            suspenders_ticket_unlock(&ch->lock);
            registered++;
        }

        if (!rescan) {
            while (suspenders_atomic_load(rec.winner, SUSPENDERS_MEMORY_ORDER_ACQUIRE) == -1 &&
                   cnode->status == S_WQ_WAITING) {
                suspenders_suspend();
            }
        }

        /* Unregister any node still parked (under each channel's lock). */
        for (int i = 0; i < registered; i++) {
            suspenders_chan_t *ch = ops[i].ch;
            suspenders_ticket_lock(&ch->lock);
            if (nodes[i].wait_list) {
                s_wq_remove(nodes[i].wait_list, &nodes[i]);
                nodes[i].wait_list = NULL;
            }
            suspenders_ticket_unlock(&ch->lock);
        }

        widx = suspenders_atomic_load(rec.winner, SUSPENDERS_MEMORY_ORDER_ACQUIRE);
        if (widx >= 0 || cnode->status != S_WQ_WAITING) break;
        if (!rescan) break;   /* defensive: the wait loop always exits decided */

        /* A case became ready mid-registration; no node is parked and no
         * winner exists, so the try ops can run again safely. */
        uint32_t r2 = s_rand32();
        for (int i = 0; i < n && scanidx < 0; i++) {
            int idx = (int)((r2 + (uint32_t)i) % (uint32_t)n);
            suspenders_chan_op_t *op = &ops[idx];
            int st = op->is_send ? suspenders_chan_try_send(op->ch, op->val)
                                 : suspenders_chan_try_recv(op->ch, op->val);
            if (st == SUSPENDERS_OK || st == SUSPENDERS_CLOSED) {
                scanidx = idx;
                scanst = st;
            }
        }
        if (scanidx >= 0) break;
        /* Someone else consumed it — park again. */
    }
    if (armed) s_timer_disarm(&t);
    /* The winning channel (possibly on another worker) and any cancel/
     * deadline decider may still be inside their grant→wake window; the
     * stack nodes and this coroutine must outlive them. */
    if (widx >= 0) s_wait_decider_sync(&nodes[widx]);
    s_wait_decider_sync(cnode);
    int st = cnode->status;
    cnode->status = SUSPENDERS_OK;   /* never leave a stale WAITING marker */

    if (widx >= 0) {
        /* The winning channel completed the op (or closed under us). */
        suspenders_errno = nodes[widx].status == SUSPENDERS_CLOSED ? SUSPENDERS_CLOSED
                                                                   : SUSPENDERS_OK;
        return widx;
    }

    if (scanidx >= 0) {
        /* A rescan pass completed the op. If a cancel/deadline raced in,
         * the flag stays pending and the next blocking call reports it. */
        suspenders_errno = scanst;
        return scanidx;
    }

    if (st == SUSPENDERS_CANCELED) {
        suspenders_atomic_store(cr->cancel_requested, 0, SUSPENDERS_MEMORY_ORDER_RELAXED);
    } else if (st == S_WQ_WAITING) {
        st = SUSPENDERS_ERROR;   /* stray wake with no decision; report */
    }
    suspenders_errno = st;
    return st;
}

int suspenders_select(suspenders_chan_op_t *ops, int n) {
    return suspenders_select_dl(ops, n, 0);
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
        s_wait_decider_sync(node);
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
        s_wait_finish_wake(n);
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

    /* Grant the head of the queue: one writer, or a batch of readers.
     * Grant+wake happen under the lock; the decider windows stay open
     * across the unlock so no released waiter can free the rwlock before
     * we let go of it. */
    suspenders_wq_node_t *granted = NULL;
    if (rw->readers == 0 && !rw->writer && rw->waiters.head) {
        if (rw->waiters.head->data_ptr == S_RW_WRITER) {
            suspenders_wq_node_t *n = s_wq_pop(&rw->waiters);
            rw->writer = true;
            s_wait_grant(n, SUSPENDERS_OK);
            s_wake_no_preempt(n->cr);
            n->next = granted;
            granted = n;
        } else {
            while (rw->waiters.head && rw->waiters.head->data_ptr != S_RW_WRITER) {
                suspenders_wq_node_t *n = s_wq_pop(&rw->waiters);
                rw->readers++;
                s_wait_grant(n, SUSPENDERS_OK);
                s_wake_no_preempt(n->cr);
                n->next = granted;
                granted = n;
            }
        }
    }
    suspenders_ticket_unlock(&rw->lock);
    s_wait_decide_end_chain(granted);
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
    if (n) s_wait_finish_wake(n);
    return SUSPENDERS_OK;
}

int suspenders_cond_broadcast(suspenders_cond_t *c) {
    if (SUSPENDERS_UNLIKELY(!c)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    suspenders_ticket_lock(&c->lock);
    /* Grant+wake under the lock; decide_end only after the unlock (a
     * released waiter may free the cond, lock included). The chain links
     * stay ours while each waker_busy traps its waiter. */
    suspenders_wq_node_t *granted = c->waiters.head;
    c->waiters.head = c->waiters.tail = NULL;
    for (suspenders_wq_node_t *n = granted; n; n = n->next) {
        s_wait_grant(n, SUSPENDERS_OK);
        s_wake_no_preempt(n->cr);
    }
    suspenders_ticket_unlock(&c->lock);
    s_wait_decide_end_chain(granted);
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
    suspenders_wq_node_t *granted = NULL;
    if (wg->count == 0 && wg->waiters.head) {
        /* Grant+wake under the lock; decide_end only after the unlock (a
         * released waiter may free the waitgroup, lock included — e.g. a
         * queue destroyer freeing the queue as its drain_wg clears). */
        granted = wg->waiters.head;
        wg->waiters.head = wg->waiters.tail = NULL;
        for (suspenders_wq_node_t *n = granted; n; n = n->next) {
            s_wait_grant(n, SUSPENDERS_OK);
            s_wake_no_preempt(n->cr);
        }
    }
    suspenders_ticket_unlock(&wg->lock);
    s_wait_decide_end_chain(granted);
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
#if !SUSPENDERS_BACKEND_IOURING
static int s_backend_register(suspenders_backend_t *be, suspenders_sock_t fd, uint32_t events, suspenders_cr_t *cr);
static void s_backend_unregister(suspenders_backend_t *be, suspenders_sock_t fd);
#endif
static int s_backend_wait(suspenders_backend_t *be, int timeout_ms);

/* In-flight completion-mode I/O op, stack-resident in the waiting coroutine
 * and used as the CQE user_data. NULL user_data marks helper SQEs
 * (link-timeout, async-cancel) whose CQEs are skipped: a coroutine is woken
 * by exactly one CQE — its op's own. */
typedef struct suspenders_io_op_s {
    suspenders_cr_t *cr;
    int32_t          result;     /* cqe->res: bytes / fd / -errno */
    uint8_t          opcode;
    bool             completed;
} suspenders_io_op_t;

/* Per-op deadline for the next blocking I/O call on this thread, set by the
 * hose _dl wrappers (0 = none). Merged with an armed suspenders_deadline. */
static SUSPENDERS_TLS uint64_t s_io_deadline_ns = 0;

static uint64_t s_io_effective_deadline(suspenders_cr_t *cr) {
    uint64_t dl = s_io_deadline_ns;
    if (cr && cr->deadline_armed) {
        uint64_t cdl = cr->deadline_timer.deadline_ns;
        if (!dl || cdl < dl) dl = cdl;
    }
    return dl;
}

/* ============================================================================
 * IO_URING BACKEND (completion mode)
 * ============================================================================ */
#if SUSPENDERS_BACKEND_IOURING

enum {
    S_IOU_RECV, S_IOU_SEND, S_IOU_READV, S_IOU_WRITEV,
    S_IOU_ACCEPT, S_IOU_CONNECT, S_IOU_RECVMSG, S_IOU_SENDMSG
};

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

static struct io_uring_sqe* s_iou_get_sqe(void) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&suspenders_backend->ring);
    if (SUSPENDERS_UNLIKELY(!sqe)) {
        io_uring_submit(&suspenders_backend->ring);
        sqe = io_uring_get_sqe(&suspenders_backend->ring);
    }
    return sqe;
}

static void s_iou_cancel(void *io_op) {
    struct io_uring_sqe *sqe = s_iou_get_sqe();
    if (SUSPENDERS_UNLIKELY(!sqe)) return;  /* op completes normally; the
        still-set cancel flag is consumed by the next blocking call */
    io_uring_prep_cancel(sqe, io_op, 0);
    io_uring_sqe_set_data(sqe, NULL);
    io_uring_submit(&suspenders_backend->ring);
}

/* Arm (or re-arm) the one-shot readability poll on this worker's wake fd.
 * Its CQE carries the worker's sentinel op (op->cr == NULL). */
static void s_iou_arm_wake(suspenders_worker_t *w) {
    if (!w || w->wake_rd < 0 || !w->wake_op) return;
    struct io_uring_sqe *sqe = s_iou_get_sqe();
    if (SUSPENDERS_UNLIKELY(!sqe)) return;
    io_uring_prep_poll_add(sqe, (int)w->wake_rd, POLLIN);
    io_uring_sqe_set_data(sqe, w->wake_op);
}

/* Prep + park for one completion-mode op. Callers guarantee a real coroutine
 * context and that entry cancel/deadline checks already ran (s_io_pre). */
static ssize_t s_iou_op(int code, suspenders_sock_t fd,
                        void *buf, size_t len,
                        const struct iovec *iov, int iovcnt,
                        const struct sockaddr *addr, socklen_t addrlen,
                        struct msghdr *msg) {
    suspenders_cr_t *cr = suspenders_running;
    struct io_uring_sqe *sqe = s_iou_get_sqe();
    if (SUSPENDERS_UNLIKELY(!sqe)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return -1;
    }

    switch (code) {
    case S_IOU_RECV:    io_uring_prep_recv(sqe, (int)fd, buf, len, 0); break;
    case S_IOU_SEND:    io_uring_prep_send(sqe, (int)fd, buf, len, 0); break;
    case S_IOU_READV:   io_uring_prep_readv(sqe, (int)fd, iov, (unsigned)iovcnt, 0); break;
    case S_IOU_WRITEV:  io_uring_prep_writev(sqe, (int)fd, iov, (unsigned)iovcnt, 0); break;
    case S_IOU_ACCEPT:  io_uring_prep_accept(sqe, (int)fd, NULL, NULL, 0); break;
    case S_IOU_CONNECT: io_uring_prep_connect(sqe, (int)fd, addr, addrlen); break;
    case S_IOU_RECVMSG: io_uring_prep_recvmsg(sqe, (int)fd, msg, 0); break;
    case S_IOU_SENDMSG: io_uring_prep_sendmsg(sqe, (int)fd, msg, 0); break;
    default:
        suspenders_errno = SUSPENDERS_INVAL;
        return -1;
    }

    suspenders_io_op_t op = { cr, 0, (uint8_t)code, false };
    io_uring_sqe_set_data(sqe, &op);

    /* Deadline via linked timeout; the timespec lives on this parked stack. */
    struct __kernel_timespec ts;
    uint64_t dl = s_io_effective_deadline(cr);
    if (dl) {
        sqe->flags |= IOSQE_IO_LINK;
        struct io_uring_sqe *tsqe = s_iou_get_sqe();
        if (SUSPENDERS_LIKELY(tsqe)) {
            uint64_t now = suspenders_now_ns();
            uint64_t delta = dl > now ? dl - now : 0;
            ts.tv_sec  = (long long)(delta / 1000000000ull);
            ts.tv_nsec = (long long)(delta % 1000000000ull);
            io_uring_prep_link_timeout(tsqe, &ts, 0);
            io_uring_sqe_set_data(tsqe, NULL);
        } else {
            /* No room for the timeout SQE (only possible if submit failed);
             * run the op untimed rather than corrupt the link chain. */
            sqe->flags &= (uint8_t)~IOSQE_IO_LINK;
        }
    }

    cr->pending_io = &op;
    while (!op.completed) suspenders_suspend();
    cr->pending_io = NULL;

    int32_t res = op.result;
    if (SUSPENDERS_UNLIKELY(res < 0)) {
        if (res == -ECANCELED) {
            /* Either suspenders_cancel/deadline via ASYNC_CANCEL, or the
             * linked timeout fired. A consumed cancel flag disambiguates. */
            if (suspenders_atomic_exchange(cr->cancel_requested, 0,
                                           SUSPENDERS_MEMORY_ORDER_RELAXED)) {
                suspenders_errno = SUSPENDERS_CANCELED;
            } else {
                suspenders_errno = SUSPENDERS_TIMEDOUT;
            }
        } else {
            errno = -res;
            suspenders_errno = SUSPENDERS_ERROR;
        }
        return -1;
    }
    return (ssize_t)res;
}

static int s_iou_flush_cqes(suspenders_backend_t *be) {
    struct io_uring_cqe *cqe;
    unsigned head;
    unsigned seen = 0;
    int found = 0;
    io_uring_for_each_cqe(&be->ring, head, cqe) {
        suspenders_io_op_t *op = (suspenders_io_op_t*)io_uring_cqe_get_data(cqe);
        if (op) {   /* NULL user_data: link-timeout / async-cancel CQE */
            if (SUSPENDERS_UNLIKELY(!op->cr)) {
                /* Worker wake sentinel: drain the eventfd, re-arm the poll. */
                s_wake_drain(s_worker);
                s_iou_arm_wake(s_worker);
                found++;
            } else {
                op->result = cqe->res;
                op->completed = true;
                if (op->cr->state == SUSPENDERS_STATE_SUSPENDED) {
                    suspenders_ready_enqueue(op->cr);
                }
                found++;
            }
        }
        seen++;
    }
    io_uring_cq_advance(&be->ring, seen);
    return found;
}

static int s_backend_wait(suspenders_backend_t *be, int timeout_ms) {
    io_uring_submit(&be->ring);

    int found = s_iou_flush_cqes(be);
    if (found || timeout_ms == 0) return found;

    struct io_uring_cqe *cqe = NULL;
    int ret;
    if (timeout_ms < 0) {
        ret = io_uring_wait_cqe(&be->ring, &cqe);
    } else {
        struct __kernel_timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
        ret = io_uring_wait_cqe_timeout(&be->ring, &cqe, &ts);
    }
    if (ret == 0) found = s_iou_flush_cqes(be);
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
        if (cr) {
            if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
                suspenders_ready_enqueue(cr);
            }
        } else if (s_worker) {
            /* Worker wake fd (registered one-shot): drain and re-arm. */
            s_wake_drain(s_worker);
            s_backend_register(be, s_worker->wake_rd, POLLIN, NULL);
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
        suspenders_cr_t *cr = be->poll.crs[i];
        if (be->poll.fds[i].revents) {
            if (cr) {
                if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
                    suspenders_ready_enqueue(cr);
                    found++;
                }
                /* One-shot: don't copy back to array */
                continue;
            }
            /* NULL cr: this worker's wake fd — drain and keep registered. */
            s_wake_drain(s_worker);
            found++;
            be->poll.fds[i].revents = 0;
        }
        be->poll.fds[j] = be->poll.fds[i];
        be->poll.crs[j] = be->poll.crs[i];
        j++;
    }
    be->poll.nfds = j;
    return found;
}

#else
    #error "No event backend available for this platform"
#endif

/* ============================================================================
 * UNIFIED BLOCKING I/O LAYER
 *
 * io_uring: one-shot completion ops (s_iou_op). kqueue/poll/WSAPoll:
 * readiness park + nonblocking syscall loop. Outside a coroutine both
 * degrade to a blocking poll(2) loop. All paths honor cancel + deadlines.
 * ============================================================================ */

static bool s_io_is_cr(void) {
    return suspenders_running && suspenders_running != suspenders_main_cr;
}

/* Entry check for blocking I/O: consume a pending cancel, honor expired
 * deadlines. Returns 0 to proceed, -1 with suspenders_errno set. */
static int s_io_pre(void) {
    suspenders_cr_t *cr = s_io_is_cr() ? suspenders_running : NULL;
    if (cr && SUSPENDERS_UNLIKELY(
            suspenders_atomic_exchange(cr->cancel_requested, 0,
                                       SUSPENDERS_MEMORY_ORDER_RELAXED) != 0)) {
        suspenders_errno = SUSPENDERS_CANCELED;
        return -1;
    }
    uint64_t dl = s_io_effective_deadline(cr);
    if (SUSPENDERS_UNLIKELY(dl && suspenders_now_ns() >= dl)) {
        suspenders_errno = SUSPENDERS_TIMEDOUT;
        return -1;
    }
    return 0;
}

/* Blocking readiness wait for non-coroutine context. */
static int s_io_block_poll(suspenders_sock_t fd, short events) {
    int timeout = -1;
    uint64_t dl = s_io_deadline_ns;
    if (dl) {
        uint64_t now = suspenders_now_ns();
        if (now >= dl) {
            suspenders_errno = SUSPENDERS_TIMEDOUT;
            return -1;
        }
        uint64_t ms = (dl - now) / 1000000ull;
        timeout = ms > INT_MAX ? INT_MAX : (int)ms + 1;
    }
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    WSAPOLLFD p = { fd, events, 0 };
    int n = WSAPoll(&p, 1, timeout);
#else
    struct pollfd p = { (int)fd, events, 0 };
    int n = poll(&p, 1, timeout);
#endif
    if (n == 0) {
        suspenders_errno = SUSPENDERS_TIMEDOUT;
        return -1;
    }
    if (n < 0) {
        suspenders_errno = SUSPENDERS_ERROR;
        return -1;
    }
    return 0;
}

#if !SUSPENDERS_BACKEND_IOURING
static void s_io_timeout_cb(void *arg) {
    suspenders_cr_t *cr = (suspenders_cr_t*)arg;
    if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
        cr->io_result = -2;   /* timeout sentinel (-1 = canceled) */
        suspenders_resume(cr);
    }
}
#endif

/* Park until fd is ready (readiness backends), or block in poll(2) outside
 * a coroutine. Returns 0 when ready, -1 with suspenders_errno set. */
static int s_io_wait_ready(suspenders_sock_t fd, uint32_t events) {
    if (!s_io_is_cr()) return s_io_block_poll(fd, (short)events);
#if SUSPENDERS_BACKEND_IOURING
    /* Coroutines on io_uring use completion ops, never readiness waits. */
    return s_io_block_poll(fd, (short)events);
#else
    suspenders_cr_t *cr = suspenders_running;
    cr->io_result = 0;
    if (SUSPENDERS_UNLIKELY(
            s_backend_register(suspenders_backend, fd, events, cr) != 0)) {
        suspenders_errno = SUSPENDERS_ERROR;
        return -1;
    }
    suspenders_timer_t t;
    bool armed = false;
    uint64_t dl = s_io_effective_deadline(cr);
    if (dl) {
        memset(&t, 0, sizeof(t));
        t.deadline_ns = dl;
        t.cb = s_io_timeout_cb;
        t.arg = cr;
        armed = s_timer_arm(&t);
    }
    suspenders_suspend();
    if (armed) s_timer_disarm(&t);
    s_backend_unregister(suspenders_backend, fd);
    if (SUSPENDERS_UNLIKELY(cr->io_result == -1)) {
        suspenders_atomic_exchange(cr->cancel_requested, 0,
                                   SUSPENDERS_MEMORY_ORDER_RELAXED);
        suspenders_errno = SUSPENDERS_CANCELED;
        return -1;
    }
    if (SUSPENDERS_UNLIKELY(cr->io_result == -2)) {
        suspenders_errno = SUSPENDERS_TIMEDOUT;
        return -1;
    }
    return 0;
#endif
}

static ssize_t s_io_recv(suspenders_sock_t fd, void *buf, size_t len) {
    if (s_io_pre() != 0) return -1;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr())
        return s_iou_op(S_IOU_RECV, fd, buf, len, NULL, 0, NULL, 0, NULL);
#endif
    for (;;) {
        ssize_t n = suspenders_sock_recv(fd, buf, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLIN) != 0) return -1;
    }
}

static ssize_t s_io_send(suspenders_sock_t fd, const void *buf, size_t len) {
    if (s_io_pre() != 0) return -1;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr())
        return s_iou_op(S_IOU_SEND, fd, (void*)(uintptr_t)buf, len, NULL, 0, NULL, 0, NULL);
#endif
    for (;;) {
        ssize_t n = suspenders_sock_send(fd, buf, len, 0);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLOUT) != 0) return -1;
    }
}

static ssize_t s_io_readv(suspenders_sock_t fd, const struct iovec *iov, int iovcnt) {
    if (s_io_pre() != 0) return -1;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    (void)fd; (void)iov; (void)iovcnt;
    suspenders_errno = SUSPENDERS_ERROR;
    return -1;
#else
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr())
        return s_iou_op(S_IOU_READV, fd, NULL, 0, iov, iovcnt, NULL, 0, NULL);
#endif
    for (;;) {
        ssize_t n = readv(fd, iov, iovcnt);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLIN) != 0) return -1;
    }
#endif
}

static ssize_t s_io_writev(suspenders_sock_t fd, const struct iovec *iov, int iovcnt) {
    if (s_io_pre() != 0) return -1;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    (void)fd; (void)iov; (void)iovcnt;
    suspenders_errno = SUSPENDERS_ERROR;
    return -1;
#else
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr())
        return s_iou_op(S_IOU_WRITEV, fd, NULL, 0, iov, iovcnt, NULL, 0, NULL);
#endif
    for (;;) {
        ssize_t n = writev(fd, iov, iovcnt);
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLOUT) != 0) return -1;
    }
#endif
}

static suspenders_sock_t s_io_accept(suspenders_sock_t fd) {
    if (s_io_pre() != 0) return SUSPENDERS_INVALID_SOCK;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr()) {
        ssize_t r = s_iou_op(S_IOU_ACCEPT, fd, NULL, 0, NULL, 0, NULL, 0, NULL);
        return r < 0 ? SUSPENDERS_INVALID_SOCK : (suspenders_sock_t)r;
    }
#endif
    for (;;) {
        suspenders_sock_t c = accept(fd, NULL, NULL);
        if (c != SUSPENDERS_INVALID_SOCK) return c;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return SUSPENDERS_INVALID_SOCK;
        }
        if (s_io_wait_ready(fd, POLLIN) != 0) return SUSPENDERS_INVALID_SOCK;
    }
}

static int s_io_connect(suspenders_sock_t fd, const struct sockaddr *addr, socklen_t addrlen) {
    if (s_io_pre() != 0) return -1;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr()) {
        return s_iou_op(S_IOU_CONNECT, fd, NULL, 0, NULL, 0, addr, addrlen, NULL) < 0 ? -1 : 0;
    }
#endif
    if (connect(fd, addr, addrlen) == 0) return 0;
    int err = suspenders_sock_errno();
    if (err != SUSPENDERS_EINPROGRESS && err != SUSPENDERS_EWOULDBLOCK) {
        suspenders_errno = SUSPENDERS_ERROR;
        return -1;
    }
    if (s_io_wait_ready(fd, POLLOUT) != 0) return -1;
    int sock_err = 0;
    socklen_t errlen = sizeof(sock_err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&sock_err, &errlen) < 0 ||
        sock_err != 0) {
        if (sock_err) errno = sock_err;
        suspenders_errno = SUSPENDERS_ERROR;
        return -1;
    }
    return 0;
}

static ssize_t s_io_recvfrom(suspenders_sock_t fd, void *buf, size_t len,
                             struct sockaddr *addr, socklen_t *addrlen) {
    if (s_io_pre() != 0) return -1;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr()) {
        struct iovec iv = { buf, len };
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = addr;
        msg.msg_namelen = addrlen ? *addrlen : 0;
        msg.msg_iov = &iv;
        msg.msg_iovlen = 1;
        ssize_t r = s_iou_op(S_IOU_RECVMSG, fd, NULL, 0, NULL, 0, NULL, 0, &msg);
        if (r >= 0 && addrlen) *addrlen = msg.msg_namelen;
        return r;
    }
#endif
    for (;;) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        ssize_t n = recvfrom(fd, (char*)buf, (int)len, 0, addr, addrlen);
#else
        ssize_t n = recvfrom(fd, buf, len, 0, addr, addrlen);
#endif
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLIN) != 0) return -1;
    }
}

static ssize_t s_io_sendto(suspenders_sock_t fd, const void *buf, size_t len,
                           const struct sockaddr *addr, socklen_t addrlen) {
    if (s_io_pre() != 0) return -1;
#if SUSPENDERS_BACKEND_IOURING
    if (s_io_is_cr()) {
        struct iovec iv = { (void*)(uintptr_t)buf, len };
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = (void*)(uintptr_t)addr;
        msg.msg_namelen = addrlen;
        msg.msg_iov = &iv;
        msg.msg_iovlen = 1;
        return s_iou_op(S_IOU_SENDMSG, fd, NULL, 0, NULL, 0, NULL, 0, &msg);
    }
#endif
    for (;;) {
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        ssize_t n = sendto(fd, (const char*)buf, (int)len, 0, addr, addrlen);
#else
        ssize_t n = sendto(fd, buf, len, 0, addr, addrlen);
#endif
        if (n >= 0) return n;
        int err = suspenders_sock_errno();
        if (err != SUSPENDERS_EAGAIN && err != SUSPENDERS_EWOULDBLOCK) {
            suspenders_errno = SUSPENDERS_ERROR;
            return -1;
        }
        if (s_io_wait_ready(fd, POLLOUT) != 0) return -1;
    }
}

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
 * SHARED STREAM-SOCKET OPS (TCP + Unix)
 * ============================================================================ */
static bool s_stream_accept(suspenders_hose_t *listener, suspenders_hose_t *client) {
    if (!(listener->roles & SUSPENDERS_HOSE_ROLE_LISTENER) ||
        listener->fd == SUSPENDERS_INVALID_SOCK) return false;
    suspenders_sock_t fd = s_io_accept(listener->fd);
    if (fd == SUSPENDERS_INVALID_SOCK) return false;
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

static ssize_t s_stream_read(suspenders_hose_t *h, void *dest, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_recv(h->fd, dest, len);
}

static ssize_t s_stream_write(suspenders_hose_t *h, const void *src, size_t len) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    size_t written = 0;
    while (written < len) {
        ssize_t n = s_io_send(h->fd, (const char*)src + written, len - written);
        if (n < 0) return -1;
        if (n == 0) break;
        written += (size_t)n;
    }
    return (ssize_t)written;
}

static ssize_t s_stream_readv(suspenders_hose_t *h, const struct iovec *iov, int iovcnt) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_readv(h->fd, iov, iovcnt);
}

static ssize_t s_stream_writev(suspenders_hose_t *h, const struct iovec *iov, int iovcnt) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_writev(h->fd, iov, iovcnt);
}

static void s_sock_close(suspenders_hose_t *h) {
    if (h->fd != SUSPENDERS_INVALID_SOCK) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
    }
    h->roles = 0;
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

    if (s_io_connect(h->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
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

static const suspenders_transport_ops_t tcp_transport_ops = {
    .scheme = "tcp://",
    .dial = tcp_dial,
    .listen = tcp_listen,
    .accept = s_stream_accept,
    .read = s_stream_read,
    .write = s_stream_write,
    .readv = s_stream_readv,
    .writev = s_stream_writev,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = s_sock_close,
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
    return s_io_recv(h->fd, dest, len);
}

static ssize_t udp_write(suspenders_hose_t *h, const void *src, size_t len) {
    /* Datagram: single-shot, never split across sends */
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_send(h->fd, src, len);
}

static ssize_t udp_recvfrom(suspenders_hose_t *h, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_READER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_recvfrom(h->fd, dest, len, addr, addrlen);
}

static ssize_t udp_sendto(suspenders_hose_t *h, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen) {
    if (!(h->roles & SUSPENDERS_HOSE_ROLE_WRITER) || h->fd == SUSPENDERS_INVALID_SOCK) return -1;
    return s_io_sendto(h->fd, src, len, addr, addrlen);
}

static const suspenders_transport_ops_t udp_transport_ops = {
    .scheme = "udp://",
    .dial = udp_dial,
    .listen = udp_listen,
    .accept = NULL,
    .read = udp_read,
    .write = udp_write,
    .readv = NULL,
    .writev = NULL,
    .recvfrom = udp_recvfrom,
    .sendto = udp_sendto,
    .close = s_sock_close,
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

    if (s_io_connect(h->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        suspenders_close_socket(h->fd);
        h->fd = SUSPENDERS_INVALID_SOCK;
        return false;
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

static const suspenders_transport_ops_t unix_transport_ops = {
    .scheme = "unix://",
    .dial = unix_dial,
    .listen = unix_listen,
    .accept = s_stream_accept,
    .read = s_stream_read,
    .write = s_stream_write,
    .readv = s_stream_readv,
    .writev = s_stream_writev,
    .recvfrom = NULL,
    .sendto = NULL,
    .close = s_sock_close,
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

ssize_t suspenders_hose_readv(suspenders_hose_t *d, const struct iovec *iov, int iovcnt) {
    if (!d->transport || !d->transport->readv) {
        suspenders_errno = SUSPENDERS_NOTFOUND;
        return -1;
    }
    return d->transport->readv(d, iov, iovcnt);
}

ssize_t suspenders_hose_writev(suspenders_hose_t *d, const struct iovec *iov, int iovcnt) {
    if (!d->transport || !d->transport->writev) {
        suspenders_errno = SUSPENDERS_NOTFOUND;
        return -1;
    }
    return d->transport->writev(d, iov, iovcnt);
}

ssize_t suspenders_hose_recvfrom(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen) {
    if (!d->transport || !d->transport->recvfrom) return -1;
    return d->transport->recvfrom(d, dest, len, addr, addrlen);
}

ssize_t suspenders_hose_sendto(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen) {
    if (!d->transport || !d->transport->sendto) return -1;
    return d->transport->sendto(d, src, len, addr, addrlen);
}

int suspenders_hose_shutdown(suspenders_hose_t *d, int how) {
    if (d->fd == SUSPENDERS_INVALID_SOCK) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    if (shutdown(d->fd, how) != 0) {
        suspenders_errno = SUSPENDERS_ERROR;
        return SUSPENDERS_ERROR;
    }
    return SUSPENDERS_OK;
}

int suspenders_hose_set_option(suspenders_hose_t *d, int level, int optname,
                               const void *optval, socklen_t optlen) {
    if (d->fd == SUSPENDERS_INVALID_SOCK) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    int rc = setsockopt(d->fd, level, optname, (const char*)optval, (int)optlen);
#else
    int rc = setsockopt(d->fd, level, optname, optval, optlen);
#endif
    if (rc != 0) {
        suspenders_errno = SUSPENDERS_ERROR;
        return SUSPENDERS_ERROR;
    }
    return SUSPENDERS_OK;
}

int suspenders_hose_peername(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen) {
    if (d->fd == SUSPENDERS_INVALID_SOCK) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    if (getpeername(d->fd, addr, addrlen) != 0) {
        suspenders_errno = SUSPENDERS_ERROR;
        return SUSPENDERS_ERROR;
    }
    return SUSPENDERS_OK;
}

int suspenders_hose_sockname(suspenders_hose_t *d, struct sockaddr *addr, socklen_t *addrlen) {
    if (d->fd == SUSPENDERS_INVALID_SOCK) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    if (getsockname(d->fd, addr, addrlen) != 0) {
        suspenders_errno = SUSPENDERS_ERROR;
        return SUSPENDERS_ERROR;
    }
    return SUSPENDERS_OK;
}

/* _dl variants: arm the TLS per-op deadline consumed by the I/O layer. */
bool suspenders_hose_dial_dl(suspenders_hose_t *d, const char *uri, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    bool ok = suspenders_hose_dial(d, uri);
    s_io_deadline_ns = 0;
    return ok;
}

bool suspenders_hose_accept_dl(suspenders_hose_t *d, suspenders_hose_t *client, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    bool ok = suspenders_hose_accept(d, client);
    s_io_deadline_ns = 0;
    return ok;
}

ssize_t suspenders_hose_read_dl(suspenders_hose_t *d, void *dest, size_t len, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    ssize_t n = suspenders_hose_read(d, dest, len);
    s_io_deadline_ns = 0;
    return n;
}

ssize_t suspenders_hose_write_dl(suspenders_hose_t *d, const void *src, size_t len, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    ssize_t n = suspenders_hose_write(d, src, len);
    s_io_deadline_ns = 0;
    return n;
}

ssize_t suspenders_hose_recvfrom_dl(suspenders_hose_t *d, void *dest, size_t len, struct sockaddr *addr, socklen_t *addrlen, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    ssize_t n = suspenders_hose_recvfrom(d, dest, len, addr, addrlen);
    s_io_deadline_ns = 0;
    return n;
}

ssize_t suspenders_hose_sendto_dl(suspenders_hose_t *d, const void *src, size_t len, const struct sockaddr *addr, socklen_t addrlen, uint64_t deadline_ns) {
    s_io_deadline_ns = deadline_ns;
    ssize_t n = suspenders_hose_sendto(d, src, len, addr, addrlen);
    s_io_deadline_ns = 0;
    return n;
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
    node->waker_busy = 0;
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
 * TASK QUEUES (libdispatch-style) AND COROUTINE POOL
 *
 * A queue is a buffered channel of tasks drained by `concurrency` daemon
 * coroutines. Daemons are excluded from run-completion accounting, so an
 * idle queue never keeps suspenders_run alive; the s_queue_work counter
 * (incremented at submit, decremented after the task body returns) is what
 * holds the runtime open while work is pending or executing.
 * ============================================================================ */
#ifndef SUSPENDERS_QUEUE_CAP
#define SUSPENDERS_QUEUE_CAP 256   /* buffered tasks per queue */
#endif

typedef struct {
    void (*fn)(void*);
    void *arg;
    int barrier;                    /* run alone (concurrency > 1 only) */
    suspenders_waitgroup_t *sync_wg;/* completion for suspenders_queue_sync */
} s_queue_task_t;

struct suspenders_queue_s {
    char label[64];
    int qos;
    unsigned concurrency;
    suspenders_chan_t *chan;
    /* Barrier coordination (only touched when concurrency > 1). */
    suspenders_mutex_t lock;
    suspenders_cond_t cv;
    unsigned active;                /* non-barrier tasks executing now */
    int barrier_pending;
    suspenders_waitgroup_t drain_wg;/* one count per live drainer */
    memento_thread_heap_t *owner_heap;
    struct suspenders_queue_s *orphan_next;
};

/* Queues released outside a coroutine cannot wait for their drainers; they
 * park here and are freed by suspenders_shutdown once nothing runs. */
static suspenders_queue_t *s_orphan_queues = NULL;
static suspenders_ticket_lock_t s_orphan_lock;
static suspenders_queue_t *s_global_queues[SUSPENDERS_QOS_COUNT];
static suspenders_ticket_lock_t s_global_queue_lock;

static void s_queue_work_end(void) {
    if (suspenders_atomic_fetch_sub(s_queue_work, 1,
            SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 1 &&
        s_workers && s_worker != &s_workers[0]) {
        /* Last pending task finished on a helper: worker 0 may be parked
         * waiting to detect run completion. */
        s_worker_signal(&s_workers[0]);
    }
}

/* Queue-internal lock: a stray cancel of a drainer consumes the flag on the
 * first failed acquire; retrying then blocks normally. */
static void s_queue_lock(suspenders_queue_t *q) {
    while (suspenders_mutex_lock(&q->lock) != SUSPENDERS_OK) {}
}

static void s_queue_drainer(void *arg) {
    suspenders_queue_t *q = (suspenders_queue_t*)arg;
    for (;;) {
        s_queue_task_t t;
        int st = suspenders_chan_recv(q->chan, &t);
        if (st == SUSPENDERS_CLOSED) break;   /* buffer drained + closed */
        if (st != SUSPENDERS_OK) continue;    /* stray cancel: keep serving */
        if (q->concurrency > 1) {
            s_queue_lock(q);
            if (t.barrier) {
                /* Channel FIFO guarantees every earlier task was dequeued
                 * first; wait for the executing ones, run alone, release. */
                while (q->barrier_pending) suspenders_cond_wait(&q->cv, &q->lock);
                q->barrier_pending = 1;
                while (q->active > 0) suspenders_cond_wait(&q->cv, &q->lock);
                suspenders_mutex_unlock(&q->lock);
                t.fn(t.arg);
                s_queue_lock(q);
                q->barrier_pending = 0;
                suspenders_cond_broadcast(&q->cv);
                suspenders_mutex_unlock(&q->lock);
            } else {
                while (q->barrier_pending) suspenders_cond_wait(&q->cv, &q->lock);
                q->active++;
                suspenders_mutex_unlock(&q->lock);
                t.fn(t.arg);
                s_queue_lock(q);
                if (--q->active == 0) suspenders_cond_broadcast(&q->cv);
                suspenders_mutex_unlock(&q->lock);
            }
        } else {
            t.fn(t.arg);   /* serial: strict submission order */
        }
        if (t.sync_wg) suspenders_waitgroup_done(t.sync_wg);
        s_queue_work_end();
    }
    suspenders_waitgroup_done(&q->drain_wg);
}

suspenders_queue_t* suspenders_queue_create(const char *label,
                                            suspenders_qos_t qos,
                                            unsigned concurrency) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) {
        suspenders_errno = SUSPENDERS_NOTINIT;
        return NULL;
    }
    if (concurrency == 0) concurrency = 1;
    memento_thread_heap_t *heap = memento_thread_heap_get();
    suspenders_queue_t *q =
        (suspenders_queue_t*)memento_thread_heap_alloc(heap, sizeof(*q));
    if (SUSPENDERS_UNLIKELY(!q)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    memset(q, 0, sizeof(*q));
    if (label) {
        strncpy(q->label, label, sizeof(q->label) - 1);
        q->label[sizeof(q->label) - 1] = '\0';
    }
    q->qos = suspenders_clamp_qos((int)qos);
    q->concurrency = concurrency;
    q->owner_heap = heap;
    q->chan = suspenders_chan_create(sizeof(s_queue_task_t), SUSPENDERS_QUEUE_CAP);
    if (SUSPENDERS_UNLIKELY(!q->chan)) {
        memento_thread_heap_free(heap, q, sizeof(*q));
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    suspenders_mutex_init(&q->lock);
    suspenders_cond_init(&q->cv);
    suspenders_waitgroup_init(&q->drain_wg);
    suspenders_waitgroup_add(&q->drain_wg, (int)concurrency);
    unsigned spawned = 0;
    for (unsigned i = 0; i < concurrency; i++) {
        if (s_spawn_impl(s_queue_drainer, q, (suspenders_qos_t)q->qos, true)) {
            spawned++;
        } else {
            suspenders_waitgroup_done(&q->drain_wg);
        }
    }
    if (SUSPENDERS_UNLIKELY(spawned == 0)) {
        suspenders_chan_destroy(q->chan);
        memento_thread_heap_free(heap, q, sizeof(*q));
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    q->concurrency = spawned;   /* degraded but functional on partial OOM */
    return q;
}

suspenders_queue_t* suspenders_get_global_queue(suspenders_qos_t qos) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) {
        suspenders_errno = SUSPENDERS_NOTINIT;
        return NULL;
    }
    static const char *s_global_names[SUSPENDERS_QOS_COUNT] = {
        "global.realtime", "global.high", "global.normal", "global.low"
    };
    int i = suspenders_clamp_qos((int)qos);
    suspenders_ticket_lock(&s_global_queue_lock);
    if (!s_global_queues[i]) {
        unsigned conc = s_num_workers ? s_num_workers : 1;
        s_global_queues[i] = suspenders_queue_create(s_global_names[i],
                                                     (suspenders_qos_t)i, conc);
    }
    suspenders_queue_t *q = s_global_queues[i];
    suspenders_ticket_unlock(&s_global_queue_lock);
    return q;
}

static int s_queue_submit(suspenders_queue_t *q, const s_queue_task_t *t) {
    suspenders_atomic_fetch_add(s_queue_work, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
    /* Coroutines get backpressure on a full queue; foreign threads and the
     * pre-run main thread cannot block, so they get FULL instead. */
    int st = suspenders_self()
                 ? suspenders_chan_send(q->chan, (void*)t)
                 : suspenders_chan_try_send(q->chan, (void*)t);
    if (SUSPENDERS_UNLIKELY(st != SUSPENDERS_OK)) {
        s_queue_work_end();
        suspenders_errno = st;
        return st;
    }
    return SUSPENDERS_OK;
}

int suspenders_queue_async(suspenders_queue_t *q, void (*fn)(void*), void *arg) {
    if (SUSPENDERS_UNLIKELY(!q || !fn)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    s_queue_task_t t = { fn, arg, 0, NULL };
    return s_queue_submit(q, &t);
}

int suspenders_queue_barrier_async(suspenders_queue_t *q,
                                   void (*fn)(void*), void *arg) {
    if (SUSPENDERS_UNLIKELY(!q || !fn)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    s_queue_task_t t = { fn, arg, q->concurrency > 1, NULL };
    return s_queue_submit(q, &t);
}

int suspenders_queue_sync(suspenders_queue_t *q, void (*fn)(void*), void *arg) {
    if (SUSPENDERS_UNLIKELY(!q || !fn)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    if (SUSPENDERS_UNLIKELY(!suspenders_self())) {
        suspenders_errno = SUSPENDERS_PERM;
        return SUSPENDERS_PERM;
    }
    suspenders_waitgroup_t wg;
    suspenders_waitgroup_init(&wg);
    suspenders_waitgroup_add(&wg, 1);
    s_queue_task_t t = { fn, arg, 0, &wg };
    int st = s_queue_submit(q, &t);
    if (SUSPENDERS_UNLIKELY(st != SUSPENDERS_OK)) return st;
    /* The task references this stack frame: a cancel cannot abandon the
     * wait (the flag is consumed and re-reported by the next block). */
    while (suspenders_waitgroup_wait(&wg) != SUSPENDERS_OK) {}
    return SUSPENDERS_OK;
}

typedef struct {
    suspenders_queue_t *q;
    void (*fn)(void*);
    void *arg;
    uint64_t delay_ns;
    memento_thread_heap_t *heap;
} s_after_task_t;

static void s_queue_after_cr(void *arg) {
    s_after_task_t *a = (s_after_task_t*)arg;
    suspenders_sleep_ns(a->delay_ns);   /* early cancel still submits */
    suspenders_queue_async(a->q, a->fn, a->arg);
    memento_thread_heap_free(a->heap, a, sizeof(*a));
}

/* The queue must outlive its pending after() submissions. The delay rides a
 * regular (non-daemon) coroutine, so it keeps suspenders_run alive. */
int suspenders_queue_after(suspenders_queue_t *q, uint64_t delay_ns,
                           void (*fn)(void*), void *arg) {
    if (SUSPENDERS_UNLIKELY(!q || !fn)) {
        suspenders_errno = SUSPENDERS_INVAL;
        return SUSPENDERS_INVAL;
    }
    memento_thread_heap_t *heap = memento_thread_heap_get();
    s_after_task_t *a =
        (s_after_task_t*)memento_thread_heap_alloc(heap, sizeof(*a));
    if (SUSPENDERS_UNLIKELY(!a)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }
    a->q = q;
    a->fn = fn;
    a->arg = arg;
    a->delay_ns = delay_ns;
    a->heap = heap;
    if (SUSPENDERS_UNLIKELY(!s_spawn_impl(s_queue_after_cr, a,
                                          (suspenders_qos_t)q->qos, false))) {
        memento_thread_heap_free(heap, a, sizeof(*a));
        return suspenders_errno;
    }
    return SUSPENDERS_OK;
}

void suspenders_queue_destroy(suspenders_queue_t *q) {
    if (!q) return;
    suspenders_chan_close(q->chan);   /* drainers finish the buffer, then exit */
    if (suspenders_self()) {
        while (suspenders_waitgroup_wait(&q->drain_wg) != SUSPENDERS_OK) {}
        suspenders_chan_destroy(q->chan);
        memento_thread_heap_free(q->owner_heap, q, sizeof(*q));
    } else {
        /* Cannot block outside a coroutine: freed by suspenders_shutdown
         * once every worker is joined and nothing can touch the queue. */
        suspenders_ticket_lock(&s_orphan_lock);
        q->orphan_next = s_orphan_queues;
        s_orphan_queues = q;
        suspenders_ticket_unlock(&s_orphan_lock);
    }
}

const char* suspenders_queue_label(const suspenders_queue_t *q) {
    return q ? q->label : NULL;
}

/* Shutdown-time reclaim (single-threaded by the shutdown contract): global
 * queues and orphans are freed outright — their daemon drainers can never
 * run again and their arenas are reclaimed by the live-cr reap. */
static void s_queue_shutdown_reap(void) {
    for (int i = 0; i < SUSPENDERS_QOS_COUNT; i++) {
        suspenders_queue_t *q = s_global_queues[i];
        if (q) {
            suspenders_chan_destroy(q->chan);
            memento_thread_heap_free(q->owner_heap, q, sizeof(*q));
            s_global_queues[i] = NULL;
        }
    }
    suspenders_queue_t *q = s_orphan_queues;
    s_orphan_queues = NULL;
    while (q) {
        suspenders_queue_t *n = q->orphan_next;
        suspenders_chan_destroy(q->chan);
        memento_thread_heap_free(q->owner_heap, q, sizeof(*q));
        q = n;
    }
    suspenders_atomic_store(s_queue_work, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
}

/* -------------------------------------------------------------------------- */
/* Coroutine pool: thin wrapper over a concurrent queue                       */
/* -------------------------------------------------------------------------- */
struct suspenders_pool_s {
    suspenders_queue_t *q;
    memento_thread_heap_t *heap;
};

suspenders_pool_t* suspenders_pool_create(unsigned nworkers,
                                          suspenders_qos_t qos) {
    memento_thread_heap_t *heap = memento_thread_heap_get();
    suspenders_pool_t *pool =
        (suspenders_pool_t*)memento_thread_heap_alloc(heap, sizeof(*pool));
    if (SUSPENDERS_UNLIKELY(!pool)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return NULL;
    }
    pool->q = suspenders_queue_create("pool", qos, nworkers ? nworkers : 1);
    if (SUSPENDERS_UNLIKELY(!pool->q)) {
        memento_thread_heap_free(heap, pool, sizeof(*pool));
        return NULL;
    }
    pool->heap = heap;
    return pool;
}

void suspenders_pool_destroy(suspenders_pool_t *pool) {
    if (!pool) return;
    suspenders_queue_destroy(pool->q);
    memento_thread_heap_free(pool->heap, pool, sizeof(*pool));
}

void suspenders_pool_submit(suspenders_pool_t *pool,
                            void (*fn)(void*), void *arg) {
    if (!pool) return;
    (void)suspenders_queue_async(pool->q, fn, arg);
}

/* ============================================================================
 * SCHEDULER INITIALIZATION & LIFECYCLE
 * ============================================================================ */
/* Reap this worker's finished coroutines (plain TLS list; crs are pinned). */
static void s_zombie_reap(void) {
    while (s_zombies) {
        suspenders_cr_t *zombie = s_zombies;
        s_zombies = zombie->next;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        if (zombie->ctx.fiber && zombie->ctx.fiber != suspenders_main_fiber) {
            DeleteFiber(zombie->ctx.fiber);
        }
#endif
        /* The cr_t lives inside its arena; destroying it frees both. The
         * arena is stamped with its creating heap, so this is foreign-safe
         * when the spawner ran on another thread. */
        memento_arena_t *arena = zombie->arena;
        if (arena) memento_arena_destroy(arena);
    }
}

/* Shutdown-while-busy reclaim: destroy the arenas of this worker's still-
 * parked coroutines. Only safe at teardown — after the worker loop has
 * exited (helpers) or all helpers are joined (worker 0) — when nothing can
 * wake or run them anymore. Their stacks are simply discarded; cleanup
 * handlers do not run (documented shutdown semantics). */
static void s_live_reap(void) {
    while (s_live_crs) {
        suspenders_cr_t *cr = s_live_crs;
        s_live_crs = cr->live_next;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
        if (cr->ctx.fiber && cr->ctx.fiber != suspenders_main_fiber) {
            DeleteFiber(cr->ctx.fiber);
        }
#endif
        if (cr->arena) memento_arena_destroy(cr->arena);
    }
}

/* Take one coroutine from the global injector and pin it to this worker. */
static suspenders_cr_t* s_injector_take(void) {
    if (suspenders_atomic_load(s_injector_count, SUSPENDERS_MEMORY_ORDER_RELAXED) == 0) {
        return NULL;
    }
    suspenders_cr_t *cr = NULL;
    suspenders_ticket_lock(&s_injector_lock);
    for (int q = 0; q < SUSPENDERS_QOS_COUNT; q++) {
        cr = s_injector_heads[q];
        if (cr) {
            s_injector_heads[q] = cr->next;
            if (!s_injector_heads[q]) s_injector_tails[q] = NULL;
            cr->next = NULL;
            suspenders_atomic_fetch_sub(s_injector_count, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
            break;
        }
    }
    suspenders_ticket_unlock(&s_injector_lock);
    if (cr) {
        suspenders_atomic_store(cr->worker, (uintptr_t)s_worker,   /* pin: all
                                future wakes come here */
                                SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        s_live_link(cr);
        /* A cancel that raced ahead of the first run had no worker to
         * deliver to; carry it as a pending wake so the first switch-out
         * (even a bare suspend) delivers it. */
        if (SUSPENDERS_UNLIKELY(suspenders_atomic_load(cr->cancel_requested,
                                    SUSPENDERS_MEMORY_ORDER_SEQ_CST) != 0)) {
            cr->pending_wake = 1;
        }
    }
    return cr;
}

/* Deliver one inbox wake on the owning worker. A pending cancel re-runs the
 * cancel delivery locally, where the ring and io_result are safe to touch.
 * A wake for a coroutine that is not suspended (it was READY: an earlier
 * wake already queued it) is recorded in pending_wake and re-delivered when
 * the cr next switches out — dropping it would lose a grant whose inbox
 * push was deduped against this entry. */
static void s_inbox_deliver(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(suspenders_atomic_load(cr->cancel_requested,
                                SUSPENDERS_MEMORY_ORDER_SEQ_CST) != 0)) {
        if (s_waiter_wake(cr, SUSPENDERS_CANCELED)) return;
#if SUSPENDERS_BACKEND_IOURING
        if (cr->pending_io) {
            s_iou_cancel(cr->pending_io);
            return;
        }
#endif
        if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
            cr->io_result = -1;
            suspenders_ready_enqueue(cr);
        } else if (cr->state != SUSPENDERS_STATE_DONE) {
            cr->pending_wake = 1;
        }
        return;
    }
    if (cr->state == SUSPENDERS_STATE_SUSPENDED) {
        suspenders_ready_enqueue(cr);
    } else if (cr->state != SUSPENDERS_STATE_DONE) {
        cr->pending_wake = 1;
    }
}

/* Consume this worker's inbox. */
static void s_inbox_drain(void) {
    uintptr_t head = suspenders_atomic_exchange(s_worker->inbox, 0,
                                                SUSPENDERS_MEMORY_ORDER_ACQUIRE);
    suspenders_cr_t *rev = NULL;
    suspenders_cr_t *list = (suspenders_cr_t*)head;
    while (list) {   /* reverse the LIFO stack for FIFO fairness */
        suspenders_cr_t *n = list->inbox_next;
        list->inbox_next = rev;
        rev = list;
        list = n;
    }
    while (rev) {
        suspenders_cr_t *cr = rev;
        rev = rev->inbox_next;
        cr->inbox_next = NULL;
        suspenders_atomic_store(cr->wake_inflight, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        s_inbox_deliver(cr);
    }
}

/* True when the whole runtime is out of work (worker 0's exit condition). */
static bool s_run_complete(void) {
    return suspenders_ready_queue_empty() &&
           suspenders_atomic_load(s_worker->inbox, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
           suspenders_atomic_load(s_injector_count, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
           suspenders_atomic_load(active_coroutines, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
           suspenders_atomic_load(s_queue_work, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
           suspenders_timer_heap.count == 0;
}

/* The scheduler loop every worker runs. Worker 0 (the suspenders_run
 * caller) exits when the runtime drains; helpers run until shutdown. */
static void s_worker_loop(bool is_run_caller) {
    suspenders_worker_t *w = s_worker;
    for (;;) {
        if (SUSPENDERS_UNLIKELY(suspenders_atomic_load(s_stop,
                                    SUSPENDERS_MEMORY_ORDER_ACQUIRE))) break;

        if (suspenders_atomic_load(w->inbox, SUSPENDERS_MEMORY_ORDER_RELAXED)) {
            s_inbox_drain();
        }

        suspenders_cr_t *cr = suspenders_ready_dequeue();
        if (!cr) cr = s_injector_take();
        if (cr) {
            suspenders_running = cr;
            cr->state = SUSPENDERS_STATE_RUNNING;
            suspenders_ctx_switch(&suspenders_main_cr->ctx, &cr->ctx);
            suspenders_running = suspenders_main_cr;
            /* A wake arrived (via inbox) while the cr was READY/RUNNING and
             * could not be delivered then; it may have been meant for the
             * park the cr just entered — re-deliver now. */
            if (SUSPENDERS_UNLIKELY(cr->pending_wake) &&
                cr->state != SUSPENDERS_STATE_DONE) {
                cr->pending_wake = 0;
                s_inbox_deliver(cr);
            }
            continue;
        }

        s_zombie_reap();

        if (is_run_caller && s_run_complete()) break;

        /* Idle: flush foreign frees back to our heap, then park. */
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
        memento_thread_heap_flush(memento_thread_heap_get());
        /* Park protocol: set parked, then re-check every wake source
         * (including run-completion for worker 0). A producer either sees
         * parked==1 and writes the eventfd, or we see its work here. */
        suspenders_atomic_store(w->parked, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        if (suspenders_atomic_load(w->inbox, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
            suspenders_atomic_load(s_injector_count, SUSPENDERS_MEMORY_ORDER_SEQ_CST) == 0 &&
            !suspenders_atomic_load(s_stop, SUSPENDERS_MEMORY_ORDER_SEQ_CST) &&
            !(is_run_caller && s_run_complete())) {
            if (suspenders_backend) {
                s_backend_wait(suspenders_backend, timeout_ms);
            } else if (timeout_ms > 0) {
                struct timespec ts;
                ts.tv_sec = timeout_ms / 1000;
                ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
                nanosleep(&ts, NULL);
            }
        }
        suspenders_atomic_store(w->parked, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
        suspenders_timer_fire_expired();
    }
}

/* Register this worker's wake fd with its (thread-local) backend. */
static void s_worker_register_wake(suspenders_worker_t *w) {
#if SUSPENDERS_BACKEND_IOURING
    s_iou_arm_wake(w);
#elif !defined(SUSPENDERS_PLATFORM_WINDOWS)
    s_backend_register(suspenders_backend, w->wake_rd, POLLIN, NULL);
#else
    (void)w;
#endif
}

#ifndef SUSPENDERS_PLATFORM_WINDOWS
static void* s_worker_thread_main(void *arg) {
    suspenders_worker_t *w = (suspenders_worker_t*)arg;
    s_worker = w;

#if defined(SUSPENDERS_PIN_WORKERS) && SUSPENDERS_PLATFORM_LINUX
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) {
        CPU_SET((unsigned)w->index % (unsigned)ncpu, &cpus);
        (void)pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);
    }
#endif

    if (SUSPENDERS_UNLIKELY(!memento_thread_heap_get())) return NULL;
    suspenders_backend = s_backend_create(s_queue_hint);
    if (SUSPENDERS_UNLIKELY(!suspenders_backend)) return NULL;

#if SUSPENDERS_BACKEND_IOURING
    static SUSPENDERS_TLS suspenders_io_op_t s_wake_op_tls;
    memset(&s_wake_op_tls, 0, sizeof(s_wake_op_tls));   /* cr == NULL: sentinel */
    w->wake_op = &s_wake_op_tls;
#endif
    s_worker_register_wake(w);

    s_main_cr_get();
    suspenders_running = suspenders_main_cr;
    suspenders_main_cr->state = SUSPENDERS_STATE_RUNNING;
    suspenders_initialized = 1;

    s_worker_loop(false);

    /* Teardown: this thread's zombies, still-parked coroutines, timers,
     * backend; the memento TLS destructor retires the heap (drained by
     * memento_shutdown). */
    s_zombie_reap();
    s_live_reap();
    if (suspenders_timer_heap.data) {
        memento_thread_heap_free(memento_thread_heap_get(),
                                 suspenders_timer_heap.data,
                                 suspenders_timer_heap.cap * sizeof(suspenders_timer_t*));
        memset(&suspenders_timer_heap, 0, sizeof(suspenders_timer_heap));
    }
    s_backend_destroy(suspenders_backend);
    suspenders_backend = NULL;
    suspenders_main_cr = NULL;
    suspenders_running = NULL;
    suspenders_initialized = 0;
    return NULL;
}
#endif /* !Windows */

int suspenders_init(unsigned num_workers, unsigned queue_hint) {
    memento_init();
    memento_thread_heap_t *heap = memento_thread_heap_get();
    if (SUSPENDERS_UNLIKELY(!heap)) {
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }

    if (queue_hint == 0) queue_hint = 256;
    s_queue_hint = queue_hint;

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
    if (num_workers > 1) num_workers = 1;   /* multi-worker is POSIX-only */
#endif
    if (num_workers == 0) num_workers = 1;

    suspenders_atomic_store(s_stop, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
    suspenders_ticket_init(&s_injector_lock);
    memset(s_injector_heads, 0, sizeof(s_injector_heads));
    memset(s_injector_tails, 0, sizeof(s_injector_tails));
    suspenders_atomic_store(s_injector_count, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);

    /* sizeof(worker) is a multiple of the cache line (aligned first member),
     * as aligned_alloc requires. */
    s_workers = (suspenders_worker_t*)aligned_alloc(
        SUSPENDERS_CACHELINE, num_workers * sizeof(suspenders_worker_t));
    if (SUSPENDERS_UNLIKELY(!s_workers)) {
        s_backend_destroy(suspenders_backend);
        suspenders_backend = NULL;
        suspenders_errno = SUSPENDERS_NOMEM;
        return SUSPENDERS_NOMEM;
    }
    memset(s_workers, 0, num_workers * sizeof(suspenders_worker_t));
    s_num_workers = num_workers;
    for (unsigned i = 0; i < num_workers; i++) {
        suspenders_worker_t *w = &s_workers[i];
        w->index = (int)i;
        suspenders_atomic_init(w->inbox, 0);
        suspenders_atomic_init(w->parked, 0);
        if (!s_wake_fd_create(w)) {
            for (unsigned j = 0; j < i; j++) s_wake_fd_destroy(&s_workers[j]);
            free(s_workers);
            s_workers = NULL;
            s_num_workers = 0;
            s_backend_destroy(suspenders_backend);
            suspenders_backend = NULL;
            suspenders_errno = SUSPENDERS_ERROR;
            return SUSPENDERS_ERROR;
        }
    }

    s_worker = &s_workers[0];
#if SUSPENDERS_BACKEND_IOURING
    {
        static SUSPENDERS_TLS suspenders_io_op_t s_wake_op_w0;
        memset(&s_wake_op_w0, 0, sizeof(s_wake_op_w0));
        s_workers[0].wake_op = &s_wake_op_w0;
    }
#endif
    s_worker_register_wake(&s_workers[0]);

#ifndef SUSPENDERS_PLATFORM_WINDOWS
    for (unsigned i = 1; i < num_workers; i++) {
        if (pthread_create(&s_workers[i].thread, NULL,
                           s_worker_thread_main, &s_workers[i]) != 0) {
            /* Roll back: stop the helpers we did start. */
            suspenders_atomic_store(s_stop, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
            for (unsigned j = 1; j < i; j++) {
                uint64_t one = 1;
                ssize_t r = write(s_workers[j].wake_wr, &one, sizeof(one));
                (void)r;
                pthread_join(s_workers[j].thread, NULL);
            }
            for (unsigned j = 0; j < num_workers; j++) s_wake_fd_destroy(&s_workers[j]);
            free(s_workers);
            s_workers = NULL;
            s_num_workers = 0;
            s_worker = NULL;
            s_backend_destroy(suspenders_backend);
            suspenders_backend = NULL;
            suspenders_atomic_store(s_stop, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
            suspenders_errno = SUSPENDERS_ERROR;
            return SUSPENDERS_ERROR;
        }
    }
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

    s_worker_loop(true);

    suspenders_main_cr->state = SUSPENDERS_STATE_READY;
}

void suspenders_shutdown(void) {
    if (!suspenders_initialized) return;

    suspenders_atomic_store(s_stop, 1, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
#ifndef SUSPENDERS_PLATFORM_WINDOWS
    for (unsigned i = 1; i < s_num_workers; i++) {
        /* Unconditional kick (skip the parked-flag fast path). */
#if SUSPENDERS_PLATFORM_LINUX
        uint64_t one = 1;
        ssize_t r = write(s_workers[i].wake_wr, &one, sizeof(one));
#else
        char one = 1;
        ssize_t r = write(s_workers[i].wake_wr, &one, 1);
#endif
        (void)r;
    }
    for (unsigned i = 1; i < s_num_workers; i++) {
        pthread_join(s_workers[i].thread, NULL);
    }
#endif

    /* Coroutines that never ran (still in the injector) are reclaimed here. */
    for (int q = 0; q < SUSPENDERS_QOS_COUNT; q++) {
        while (s_injector_heads[q]) {
            suspenders_cr_t *cr = s_injector_heads[q];
            s_injector_heads[q] = cr->next;
            if (cr->arena) memento_arena_destroy(cr->arena);
        }
        s_injector_tails[q] = NULL;
    }
    suspenders_atomic_store(s_injector_count, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);
    s_zombie_reap();
    /* Coroutines still parked on this worker (shutdown-while-busy) are
     * reclaimed from the live list; helpers reclaimed theirs before join.
     * Abandoned coroutines kept the count nonzero; reset for re-init. */
    s_live_reap();
    /* The reaped crs may still be linked into this worker's ready queues
     * (shutdown without run); reset the queues so re-init starts clean. */
    for (int q = 0; q < SUSPENDERS_QOS_COUNT; q++) {
        ready_queue_heads[q] = NULL;
        ready_queue_tails[q] = NULL;
    }
    s_queue_shutdown_reap();
    suspenders_atomic_store(active_coroutines, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);

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
    if (s_workers) {
        for (unsigned i = 0; i < s_num_workers; i++) s_wake_fd_destroy(&s_workers[i]);
        free(s_workers);
        s_workers = NULL;
    }
    s_num_workers = 0;
    s_worker = NULL;
    suspenders_atomic_store(s_stop, 0, SUSPENDERS_MEMORY_ORDER_SEQ_CST);

    /* Reset per-thread scheduler identity so a re-init starts clean. */
    suspenders_main_cr = NULL;
    suspenders_running = NULL;
    memento_shutdown();
    suspenders_initialized = 0;
#ifdef SUSPENDERS_PLATFORM_WINDOWS
    WSACleanup();
#endif
}

#ifdef __cplusplus
  #if defined(__clang__)
    #pragma clang diagnostic pop
  #elif defined(__GNUC__)
    #pragma GCC diagnostic pop
  #endif
#endif

#endif /* SUSPENDERS_IMPLEMENTATION */
