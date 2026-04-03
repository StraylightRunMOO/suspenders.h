
#ifndef LIBSUSPENDERS_H
#define LIBSUSPENDERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/syscall.h>

#if defined(__linux__)
#include <liburing.h>
#include <sys/uio.h>
#endif

/* ============================================================================
 * MEMENTO INTEGRATION
 * Define MEMENTO_IMPLEMENTATION in ONE .c file before including this header.
 * ============================================================================ */
#ifndef MEMENTO_IMPLEMENTATION
typedef struct memento_arena_s memento_arena_t;
#endif

/* ============================================================================
 * BUFFER - Memento-backed dynamic buffer
 * ============================================================================ */
#ifndef BUF_MALLOC
#define BUF_MALLOC(size) memento_thread_heap_alloc(memento_thread_heap_get(), size)
#endif
#ifndef BUF_FREE
#define BUF_FREE(ptr, size) memento_thread_heap_free(memento_thread_heap_get(), ptr, size)
#endif

struct buf {
    char  *data;
    size_t len;
    size_t cap;
};

bool buf_append(struct buf *buf, const char *data, ssize_t len);
bool buf_append_byte(struct buf *buf, char ch);
void buf_clear(struct buf *buf);

/* ============================================================================
 * SUSPENDERS CORE - Ultra-fast stackful coroutines
 * ============================================================================ */
#define SUSPENDERS_ALIGNAS(x) __attribute__((aligned(x)))
#define SUSPENDERS_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SUSPENDERS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SUSPENDERS_CACHELINE   64
#define SUSPENDERS_STACK_SIZE  (1UL << 20)  /* 1MB default stack */

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

/* Context structure - packed for minimal cache footprint */
#if defined(__x86_64__)
typedef struct {
    uintptr_t rsp, rbx, rbp, r12, r13, r14, r15;
} suspenders_ctx_t SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE);

#elif defined(__aarch64__)
typedef struct {
    uintptr_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uintptr_t fp, lr, sp;
} suspenders_ctx_t SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE);

#else
#error "libsuspenders: only x86_64 and aarch64 supported"
#endif

typedef struct suspenders_cr_s suspenders_cr_t;

/* Work queue node for channel operations */
typedef struct suspenders_wq_node_s {
    suspenders_cr_t *cr;
    void       *data_ptr;
    struct suspenders_wq_node_s *next;
} suspenders_wq_node_t;

/* Coroutine structure - cache line aligned */
struct suspenders_cr_s {
    suspenders_ctx_t      ctx;
    suspenders_state_t    state;
    _Atomic int      effective_qos;
    int              base_qos;
    memento_arena_t *arena;       /* Stack arena */
    suspenders_wq_node_t  wq_node;
    int32_t          io_result;   /* CQE result from last I/O op */
    struct suspenders_cr_s *next;      /* Ready queue linkage */
} SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE);

/* Ticket lock - strict FIFO ordering */
typedef struct {
    _Atomic uint32_t next_ticket;
    _Atomic uint32_t now_serving;
} SUSPENDERS_ALIGNAS(SUSPENDERS_CACHELINE) suspenders_ticket_lock_t;

static inline void suspenders_ticket_init(suspenders_ticket_lock_t *l);
static inline void suspenders_ticket_lock(suspenders_ticket_lock_t *l);
static inline void suspenders_ticket_unlock(suspenders_ticket_lock_t *l);

/* Channel - zero-copy rendezvous communication */
typedef struct {
    size_t elem_sz;
    size_t buf_sz;
    _Atomic size_t count;
    suspenders_wq_node_t *senders;
    suspenders_wq_node_t *receivers;
    suspenders_ticket_lock_t lock;
} suspenders_chan_t;

/* Hose - async I/O abstraction */
#define HOSE_ROLE_READER   (1 << 0)
#define HOSE_ROLE_WRITER   (1 << 1)
#define HOSE_ROLE_DIALER   (1 << 2)
#define HOSE_ROLE_LISTENER (1 << 3)

typedef enum {
    HOSE_PROTO_UNKNOWN,
    HOSE_PROTO_FILE,
    HOSE_PROTO_TCP,
    HOSE_PROTO_UDP,
} hose_protocol_t;

typedef struct hose_s {
    uint8_t          roles;
    hose_protocol_t  protocol;
    int              fd;
    struct buf      *buffer;
    struct io_uring *ring;
    suspenders_cr_t      *owner;
} hose_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

void suspenders_init(unsigned num_workers, unsigned io_uring_entries);
void suspenders_run(void);
void suspenders_shutdown(void);

suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos);
void suspenders_yield(void);
void suspenders_suspend(void);
void suspenders_resume(suspenders_cr_t *cr);
void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos);
struct io_uring* suspenders_ring(void);

suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz);
bool suspenders_chan_send(suspenders_chan_t *ch, void *val);
bool suspenders_chan_recv(suspenders_chan_t *ch, void *out);

void  hose_init(hose_t *d, struct io_uring *ring, struct buf *b);
bool  hose_dial(hose_t *d, const char *uri);
bool  hose_listen(hose_t *d, const char *uri);
bool  hose_accept(hose_t *d, hose_t *client);
ssize_t hose_read(hose_t *d, void *dest, size_t len);
ssize_t hose_write(hose_t *d, const void *src, size_t len);
void  hose_close(hose_t *d);

#if defined(__linux__)
void suspenders_writev_async(int fd, struct iovec *iovs, int count);
#endif

#endif /* LIBSUSPENDERS_H */

/* ============================================================================
 * IMPLEMENTATION - Define SUSPENDERS_IMPLEMENTATION in ONE .c file
 * ============================================================================ */
#ifdef SUSPENDERS_IMPLEMENTATION

#include <stdlib.h>
#include <stdio.h>

/* Thread-local state */
static __thread suspenders_cr_t *suspenders_running = NULL;
static suspenders_cr_t *suspenders_main_cr = NULL;
static struct io_uring suspenders_ring_instance;
static __thread suspenders_cr_t *ready_queue_head = NULL;
static __thread suspenders_cr_t *ready_queue_tail = NULL;
static __thread int suspenders_initialized = 0;
static _Atomic int active_coroutines = 0;   /* Total spawned - completed */
static suspenders_cr_t *zombie_head = NULL;      /* Completed coroutines to free */

/* ============================================================================
 * BUFFER IMPLEMENTATION
 * ============================================================================ */
bool buf_append(struct buf *buf, const char *data, ssize_t len) {
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

bool buf_append_byte(struct buf *buf, char ch) {
    if (SUSPENDERS_LIKELY(buf->len < buf->cap)) {
        buf->data[buf->len++] = ch;
        buf->data[buf->len] = '\0';
        return true;
    }
    return buf_append(buf, &ch, 1);
}

void buf_clear(struct buf *buf) {
    if (buf->data) BUF_FREE(buf->data, buf->cap + 1);
    memset(buf, 0, sizeof(*buf));
}

/* ============================================================================
 * COROUTINE EXIT HANDLER - Forward declaration for assembly
 * ============================================================================ */
void suspenders_cr_exit(void);

/* ============================================================================
 * ULTRA-FAST CONTEXT SWITCH - Optimized hand-rolled assembly
 * 
 * Design goals:
 * - Minimal instruction count
 * - Direct register-to-memory operations
 * - No stack frame setup (naked functions)
 * - No red-zone clobber on x86_64
 * ============================================================================ */

#if defined(__x86_64__)

/* Save: rsp, rbx, rbp, r12-r15 (callee-saved per System V AMD64 ABI)
 * Stack is already 16-byte aligned at call site
 * Context layout: rsp@0, rbx@8, rbp@16, r12@24, r13@32, r14@40, r15@48
 */
__attribute__((naked, noinline))
void suspenders_ctx_switch(suspenders_ctx_t *old, suspenders_ctx_t *new) {
    __asm__ volatile (
        "movq %%rsp,  0(%0)\n\t"   /* save old */
        "movq %%rbx,  8(%0)\n\t"
        "movq %%rbp, 16(%0)\n\t"
        "movq %%r12, 24(%0)\n\t"
        "movq %%r13, 32(%0)\n\t"
        "movq %%r14, 40(%0)\n\t"
        "movq %%r15, 48(%0)\n\t"
        "movq  0(%1), %%rsp\n\t"   /* load new */
        "movq  8(%1), %%rbx\n\t"
        "movq 16(%1), %%rbp\n\t"
        "movq 24(%1), %%r12\n\t"
        "movq 32(%1), %%r13\n\t"
        "movq 40(%1), %%r14\n\t"
        "movq 48(%1), %%r15\n\t"
        "ret\n\t"
        : : "r"(old), "r"(new) : "memory"
    );
}

/* Trampoline: called when coroutine starts. 
 * r12 = arg, r13 = func (set in suspenders_make_context)
 */
/* Coroutine exit handler - called when coroutine function returns */
static void suspenders_cr_exit(void);

__attribute__((naked, noinline))
static void suspenders_ctx_trampoline(void) {
    __asm__ volatile (
        "movq %%r12, %%rdi\n\t"    /* arg -> first parameter (rdi) */
        "call *%%r13\n\t"          /* call func(arg) */
        /* When func returns, fall through to exit handler */
        "jmp suspenders_cr_exit\n\t"
    );
}

void suspenders_make_context(suspenders_ctx_t *ctx, void *stack_base, size_t stack_size,
                        void (*func)(void*), void *arg) {
    uintptr_t sp = (uintptr_t)stack_base + stack_size;
    sp &= ~15UL;
    sp -= 8; 
    *(uintptr_t*)sp = (uintptr_t)suspenders_cr_exit;   /* func return address */
    ctx->rsp = sp;
    ctx->r12 = (uintptr_t)arg;
    ctx->r13 = (uintptr_t)func;
    /* everything else zeroed */
    ctx->rbx = ctx->rbp = ctx->r14 = ctx->r15 = 0;
}

#elif defined(__aarch64__)

/* Save: x19-x28, fp (x29), lr (x30), sp
 * Context layout: x19-x20@0, x21-x22@16, x23-x24@32, x25-x26@48,
 *                 x27-x28@64, fp-lr@80, sp@96 (104 bytes total)
 */
__attribute__((naked, noinline))
void suspenders_ctx_switch(suspenders_ctx_t *old __attribute__((unused)), 
                       suspenders_ctx_t *new __attribute__((unused))) {
    __asm__ volatile (
        "stp x19, x20, [x0, #0]\n\t"
        "stp x21, x22, [x0, #16]\n\t"
        "stp x23, x24, [x0, #32]\n\t"
        "stp x25, x26, [x0, #48]\n\t"
        "stp x27, x28, [x0, #64]\n\t"
        "stp x29, x30, [x0, #80]\n\t"
        "mov x9, sp\n\t"
        "str x9, [x0, #96]\n\t"
        "ldp x19, x20, [x1, #0]\n\t"
        "ldp x21, x22, [x1, #16]\n\t"
        "ldp x23, x24, [x1, #32]\n\t"
        "ldp x25, x26, [x1, #48]\n\t"
        "ldp x27, x28, [x1, #64]\n\t"
        "ldp x29, x30, [x1, #80]\n\t"
        "ldr x9, [x1, #96]\n\t"
        "mov sp, x9\n\t"
        "ret\n\t"
    );
}

/* Entry trampoline: x19 = arg, x20 = func
 * Uses blr to set lr, then when func returns, it goes to exit handler
 */
__attribute__((naked, noinline))
static void suspenders_ctx_trampoline(void) {
    __asm__ volatile (
        "mov x0, x19\n\t"           /* arg -> x0 (first parameter) */
        "blr x20\n\t"              /* call func(arg), lr = return address */
        "b suspenders_cr_exit\n\t"      /* func returned, go to exit handler */
    );
}

void suspenders_make_context(suspenders_ctx_t *ctx, void *stack_base, size_t stack_size,
                        void (*func)(void*), void *arg) {
    uintptr_t sp = (uintptr_t)stack_base + stack_size;
    sp &= ~15UL;
    /* Note: ret on aarch64 uses lr, not stack, so no push needed */
    ctx->sp = sp;
    ctx->fp = 0;
    ctx->lr = (uintptr_t)suspenders_ctx_trampoline;  /* Entry point */
    ctx->x19 = (uintptr_t)arg;
    ctx->x20 = (uintptr_t)func;
    ctx->x21 = ctx->x22 = ctx->x23 = ctx->x24 = 0;
    ctx->x25 = ctx->x26 = ctx->x27 = ctx->x28 = 0;
}

#endif /* __aarch64__ */

/* ============================================================================
 * COROUTINE EXIT HANDLER - Called when coroutine function returns
 * ============================================================================ */
void suspenders_zombie_enqueue(suspenders_cr_t *cr) {
    /* Simple lock-free push to zombie list using CAS */
    suspenders_cr_t *old_head;
    do {
        old_head = zombie_head;
        cr->next = old_head;
    } while (!atomic_compare_exchange_weak((atomic_uintptr_t*)&zombie_head, 
                                            (uintptr_t*)&old_head, (uintptr_t)cr));
}

void suspenders_cr_exit(void) {
    if (suspenders_running) {
        suspenders_running->state = SUSPENDERS_STATE_DONE;
        atomic_fetch_sub(&active_coroutines, 1);
        /* Add to zombie queue for cleanup (can't free own stack) */
        suspenders_zombie_enqueue(suspenders_running);
        /* Return to main context */
        if (suspenders_main_cr) {
            suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
        }
    }
    __builtin_unreachable();
}

/* ============================================================================
 * TICKET LOCK - FIFO strict ordering
 * ============================================================================ */
static inline void suspenders_ticket_init(suspenders_ticket_lock_t *l) {
    atomic_init(&l->next_ticket, 0);
    atomic_init(&l->now_serving, 0);
}

static inline void suspenders_ticket_lock(suspenders_ticket_lock_t *l) {
    uint32_t my_ticket = atomic_fetch_add_explicit(&l->next_ticket, 1, 
                                                    memory_order_relaxed);
    while (atomic_load_explicit(&l->now_serving, memory_order_acquire) != my_ticket) {
#if defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
}

static inline void suspenders_ticket_unlock(suspenders_ticket_lock_t *l) {
    uint32_t current = atomic_load_explicit(&l->now_serving, memory_order_relaxed);
    atomic_store_explicit(&l->now_serving, current + 1, memory_order_release);
}

/* ============================================================================
 * SCHEDULER - Ready queue and context switching
 * ============================================================================ */
static void suspenders_ready_enqueue(suspenders_cr_t *cr) {
    cr->next = NULL;
    cr->state = SUSPENDERS_STATE_READY;
    if (ready_queue_tail) {
        ready_queue_tail->next = cr;
    } else {
        ready_queue_head = cr;
    }
    ready_queue_tail = cr;
}

static suspenders_cr_t* suspenders_ready_dequeue(void) {
    suspenders_cr_t *cr = ready_queue_head;
    if (cr) {
        ready_queue_head = cr->next;
        if (!ready_queue_head) {
            ready_queue_tail = NULL;
        }
        cr->next = NULL;
    }
    return cr;
}

static void suspenders_switch_to(suspenders_cr_t *next) {
    suspenders_cr_t *prev = suspenders_running;
    suspenders_running = next;
    next->state = SUSPENDERS_STATE_RUNNING;
    suspenders_ctx_switch(&prev->ctx, &next->ctx);
}

/* ============================================================================
 * COROUTINE LIFECYCLE
 * ============================================================================ */
void suspenders_yield(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_running || suspenders_running == suspenders_main_cr)) return;
    suspenders_ready_enqueue(suspenders_running);
    /* Return to main context - it will schedule next coroutine */
    suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
}

void suspenders_suspend(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_running || suspenders_running == suspenders_main_cr)) return;
    if (suspenders_running->state == SUSPENDERS_STATE_RUNNING) {
        suspenders_running->state = SUSPENDERS_STATE_SUSPENDED;
    }
    /* Always return to main context */
    suspenders_ctx_switch(&suspenders_running->ctx, &suspenders_main_cr->ctx);
}

void suspenders_resume(suspenders_cr_t *cr) {
    if (SUSPENDERS_UNLIKELY(!cr || cr->state == SUSPENDERS_STATE_RUNNING)) return;
    suspenders_ready_enqueue(cr);
    /* If called from running context, we may want to switch */
    if (suspenders_running && suspenders_running->effective_qos > cr->effective_qos) {
        suspenders_ready_enqueue(suspenders_running);
        suspenders_switch_to(cr);
    }
}

void suspenders_boost(suspenders_cr_t *target, suspenders_qos_t new_qos) {
    if (SUSPENDERS_UNLIKELY(!target)) return;
    int current = atomic_load(&target->effective_qos);
    while ((int)new_qos < current) {
        if (atomic_compare_exchange_weak(&target->effective_qos, &current, (int)new_qos))
            break;
    }
}

suspenders_cr_t* suspenders_spawn(void (*func)(void*), void *arg, suspenders_qos_t qos) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) return NULL;
    if (SUSPENDERS_UNLIKELY(!func)) return NULL;
    
    /* Create main coroutine on first spawn */
    if (SUSPENDERS_UNLIKELY(!suspenders_main_cr)) {
        suspenders_main_cr = memento_thread_heap_alloc(memento_thread_heap_get(), 
                                                   sizeof(suspenders_cr_t));
        if (SUSPENDERS_UNLIKELY(!suspenders_main_cr)) return NULL;
        memset(suspenders_main_cr, 0, sizeof(suspenders_cr_t));
        suspenders_main_cr->state = SUSPENDERS_STATE_RUNNING;
        suspenders_main_cr->effective_qos = SUSPENDERS_QOS_NORMAL;
        suspenders_main_cr->base_qos = SUSPENDERS_QOS_NORMAL;
        suspenders_running = suspenders_main_cr;
    }
    
    /* Allocate coroutine structure */
    suspenders_cr_t *cr = memento_thread_heap_alloc(memento_thread_heap_get(),
                                                sizeof(suspenders_cr_t));
    if (SUSPENDERS_UNLIKELY(!cr)) return NULL;
    memset(cr, 0, sizeof(suspenders_cr_t));
    
    /* Create arena for this coroutine's stack */
    cr->arena = memento_arena_create(SUSPENDERS_STACK_SIZE, NULL);
    if (SUSPENDERS_UNLIKELY(!cr->arena)) {
        memento_thread_heap_free(memento_thread_heap_get(), cr, sizeof(suspenders_cr_t));
        return NULL;
    }
    
    /* Allocate stack from arena */
    void *stack = memento_arena_alloc(cr->arena, SUSPENDERS_STACK_SIZE, 16);
    if (SUSPENDERS_UNLIKELY(!stack)) {
        memento_arena_destroy(cr->arena);
        memento_thread_heap_free(memento_thread_heap_get(), cr, sizeof(suspenders_cr_t));
        return NULL;
    }
    
    /* Initialize context */
    cr->state = SUSPENDERS_STATE_READY;
    cr->base_qos = (int)qos;
    cr->effective_qos = (int)qos;
    
    /* Set up context with user function and arg */
    suspenders_make_context(&cr->ctx, stack, SUSPENDERS_STACK_SIZE, func, arg);
    
    /* Add to ready queue */
    suspenders_ready_enqueue(cr);
    
    /* Track active coroutine */
    atomic_fetch_add(&active_coroutines, 1);
    
    return cr;
}

/* ============================================================================
 * CHANNEL - Zero-copy rendezvous
 * ============================================================================ */
suspenders_chan_t* suspenders_chan_create(size_t elem_sz, size_t buf_sz) {
    suspenders_chan_t *ch = memento_thread_heap_alloc(memento_thread_heap_get(),
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
    
    suspenders_ticket_lock(&ch->lock);
    
    /* Try to match with waiting receiver */
    if (ch->receivers) {
        suspenders_wq_node_t *rx = ch->receivers;
        ch->receivers = rx->next;
        memcpy(rx->data_ptr, val, ch->elem_sz);
        suspenders_ticket_unlock(&ch->lock);
        suspenders_resume(rx->cr);
        return true;
    }
    
    /* No buffered channels yet - rendezvous only */
    if (ch->buf_sz == 0) {
        suspenders_wq_node_t *node = &suspenders_running->wq_node;
        node->cr = suspenders_running;
        node->data_ptr = val;
        node->next = ch->senders;
        ch->senders = node;
        suspenders_ticket_unlock(&ch->lock);
        suspenders_suspend();
        return true;
    }
    
    suspenders_ticket_unlock(&ch->lock);
    return false;
}

bool suspenders_chan_recv(suspenders_chan_t *ch, void *out) {
    if (SUSPENDERS_UNLIKELY(!ch || !out)) return false;
    
    suspenders_ticket_lock(&ch->lock);
    
    /* Try to match with waiting sender */
    if (ch->senders) {
        suspenders_wq_node_t *tx = ch->senders;
        ch->senders = tx->next;
        memcpy(out, tx->data_ptr, ch->elem_sz);
        suspenders_ticket_unlock(&ch->lock);
        suspenders_resume(tx->cr);
        return true;
    }
    
    /* No buffered channels yet - rendezvous only */
    if (ch->buf_sz == 0) {
        suspenders_wq_node_t *node = &suspenders_running->wq_node;
        node->cr = suspenders_running;
        node->data_ptr = out;
        node->next = ch->receivers;
        ch->receivers = node;
        suspenders_ticket_unlock(&ch->lock);
        suspenders_suspend();
        return true;
    }
    
    suspenders_ticket_unlock(&ch->lock);
    return false;
}

/* ============================================================================
 * SCHEDULER INITIALIZATION
 * ============================================================================ */
void suspenders_init(unsigned num_workers, unsigned io_uring_entries) {
    (void)num_workers;  /* TODO: worker thread support */
    
    /* Initialize Memento first */
    memento_init();
    
    /* Get thread-local heap (creates if needed) */
    memento_thread_heap_t *heap = memento_thread_heap_get();
    if (SUSPENDERS_UNLIKELY(!heap)) {
        fprintf(stderr, "suspenders: failed to initialize thread heap\n");
        abort();
    }
    
    /* Initialize io_uring */
    int ret = io_uring_queue_init(io_uring_entries, &suspenders_ring_instance, 0);
    if (ret < 0) {
        fprintf(stderr, "suspenders: io_uring init failed: %d\n", ret);
        abort();
    }
    
    suspenders_initialized = 1;
}

struct io_uring* suspenders_ring(void) {
    return &suspenders_ring_instance;
}

void suspenders_run(void) {
    if (SUSPENDERS_UNLIKELY(!suspenders_initialized)) {
        fprintf(stderr, "suspenders: not initialized\n");
        return;
    }
    
    struct io_uring_cqe *cqe;
    unsigned head;
    
    /* Ensure main coroutine exists and is set up */
    if (SUSPENDERS_UNLIKELY(!suspenders_main_cr)) {
        suspenders_main_cr = memento_thread_heap_alloc(memento_thread_heap_get(), 
                                                   sizeof(suspenders_cr_t));
        if (SUSPENDERS_UNLIKELY(!suspenders_main_cr)) {
            fprintf(stderr, "suspenders: failed to create main coroutine\n");
            return;
        }
        memset(suspenders_main_cr, 0, sizeof(suspenders_cr_t));
        suspenders_main_cr->state = SUSPENDERS_STATE_RUNNING;
        suspenders_main_cr->effective_qos = SUSPENDERS_QOS_NORMAL;
        suspenders_main_cr->base_qos = SUSPENDERS_QOS_NORMAL;
    }
    
    /* Mark main as running */
    suspenders_running = suspenders_main_cr;
    suspenders_main_cr->state = SUSPENDERS_STATE_RUNNING;
    
    while (1) {
        /* Process ready queue */
        suspenders_cr_t *cr = suspenders_ready_dequeue();
        if (cr) {
            /* Switch from main to the ready coroutine */
            suspenders_running = cr;
            cr->state = SUSPENDERS_STATE_RUNNING;
            suspenders_ctx_switch(&suspenders_main_cr->ctx, &cr->ctx);
            /* Back from coroutine - restore main as running */
            suspenders_running = suspenders_main_cr;
            continue;
        }
        
        /* Process zombie queue - free completed coroutines */
        while (zombie_head) {
            suspenders_cr_t *zombie = zombie_head;
            zombie_head = zombie->next;
            /* Free the arena (which contains the stack) */
            if (zombie->arena) {
                memento_arena_destroy(zombie->arena);
            }
            /* Free the coroutine struct itself */
            memento_thread_heap_free(memento_thread_heap_get(), zombie, sizeof(suspenders_cr_t));
        }
        
        /* Exit if no work remaining and no active coroutines */
        if (!ready_queue_head && atomic_load(&active_coroutines) == 0) {
            break;
        }
        
        /* If no ready coroutines but we have active ones, they may be waiting on I/O */
        if (!ready_queue_head) {
            /* Submit any pending SQEs before checking for completions */
            io_uring_submit(&suspenders_ring_instance);

            /* Check for I/O completions without blocking first */
            int found = 0;
            io_uring_for_each_cqe(&suspenders_ring_instance, head, cqe) {
                suspenders_cr_t *cr_io = (suspenders_cr_t*)io_uring_cqe_get_data(cqe);
                if (cr_io && cr_io->state == SUSPENDERS_STATE_SUSPENDED) {
                    cr_io->io_result = cqe->res;
                    suspenders_ready_enqueue(cr_io);
                    found = 1;
                }
                io_uring_cqe_seen(&suspenders_ring_instance, cqe);
            }
            
            if (!found) {
                /* Block until at least one CQE arrives */
                int ret = io_uring_wait_cqe(&suspenders_ring_instance, &cqe);
                if (ret == 0) {
                    suspenders_cr_t *cr_io = (suspenders_cr_t*)io_uring_cqe_get_data(cqe);
                    if (cr_io && cr_io->state == SUSPENDERS_STATE_SUSPENDED) {
                        cr_io->io_result = cqe->res;
                        suspenders_ready_enqueue(cr_io);
                    }
                    io_uring_cqe_seen(&suspenders_ring_instance, cqe);
                }
            }
        }
    }
    
    suspenders_main_cr->state = SUSPENDERS_STATE_READY;
}

void suspenders_shutdown(void) {
    if (suspenders_initialized) {
        io_uring_queue_exit(&suspenders_ring_instance);
        memento_shutdown();
        suspenders_initialized = 0;
    }
}

/* ============================================================================
 * HOSE - Async I/O
 * ============================================================================ */
static bool hose_parse_uri(const char *uri, hose_t *d, char *host, int *port) {
    if (strncmp(uri, "tcp://", 6) == 0) {
        d->protocol = HOSE_PROTO_TCP;
        return sscanf(uri + 6, "%255[^:]:%d", host, port) == 2;
    }
    return false;
}

void hose_init(hose_t *d, struct io_uring *ring, struct buf *b) {
    memset(d, 0, sizeof(*d));
    d->fd = -1;
    d->ring = ring ? ring : &suspenders_ring_instance;
    d->buffer = b;
    d->owner = suspenders_running;
}

bool hose_dial(hose_t *d, const char *uri) {
    char host[256] = {0};
    int port = 0;
    if (!hose_parse_uri(uri, d, host, &port)) return false;
    
    if (d->protocol == HOSE_PROTO_TCP) {
        d->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (d->fd < 0) return false;
        
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port)
        };
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
            close(d->fd);
            d->fd = -1;
            return false;
        }
        
        /* Start async connect */
        int ret = connect(d->fd, (struct sockaddr*)&addr, sizeof(addr));
        if (ret < 0 && errno != EINPROGRESS) {
            close(d->fd);
            d->fd = -1;
            return false;
        }
        
        if (ret < 0) {
            /* Async connect in progress - wait via io_uring */
            struct io_uring_sqe *sqe = io_uring_get_sqe(d->ring);
            if (!sqe) {
                close(d->fd);
                d->fd = -1;
                return false;
            }
            
            /* Poll for writability = connected */
            io_uring_prep_poll_add(sqe, d->fd, POLLOUT | POLLERR);
            io_uring_sqe_set_data(sqe, suspenders_running);
            suspenders_suspend();
            
            /* Check if connected */
            int sock_err = 0;
            socklen_t errlen = sizeof(sock_err);
            getsockopt(d->fd, SOL_SOCKET, SO_ERROR, &sock_err, &errlen);
            if (sock_err != 0) {
                close(d->fd);
                d->fd = -1;
                return false;
            }
        }
        
        d->roles |= HOSE_ROLE_DIALER | HOSE_ROLE_READER | HOSE_ROLE_WRITER;
        return true;
    }
    return false;
}

bool hose_listen(hose_t *d, const char *uri) {
    char host[256] = {0};
    int port = 0;
    if (!hose_parse_uri(uri, d, host, &port)) return false;
    
    if (d->protocol == HOSE_PROTO_TCP) {
        d->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (d->fd < 0) return false;
        
        int opt = 1;
        setsockopt(d->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr.s_addr = INADDR_ANY
        };
        
        if (bind(d->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(d->fd);
            d->fd = -1;
            return false;
        }
        
        if (listen(d->fd, 128) < 0) {
            close(d->fd);
            d->fd = -1;
            return false;
        }
        
        d->roles |= HOSE_ROLE_LISTENER;
        return true;
    }
    return false;
}

bool hose_accept(hose_t *d, hose_t *client) {
    if (!(d->roles & HOSE_ROLE_LISTENER) || d->fd < 0) return false;

    struct io_uring_sqe *sqe = io_uring_get_sqe(d->ring);
    if (!sqe) return false;

    suspenders_cr_t *cr = suspenders_running;
    io_uring_prep_accept(sqe, d->fd, NULL, NULL, SOCK_NONBLOCK);
    io_uring_sqe_set_data(sqe, cr);
    suspenders_suspend();

    int fd = cr->io_result;
    if (fd < 0) return false;

    hose_init(client, d->ring, NULL);
    client->fd = fd;
    client->protocol = d->protocol;
    client->roles = HOSE_ROLE_READER | HOSE_ROLE_WRITER;
    client->owner = NULL;  /* Handler coroutine will use suspenders_running */
    return true;
}

ssize_t hose_read(hose_t *d, void *dest, size_t len) {
    if (!(d->roles & HOSE_ROLE_READER) || d->fd < 0) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(d->ring);
    if (!sqe) return -1;

    suspenders_cr_t *cr = d->owner ? d->owner : suspenders_running;
    io_uring_prep_read(sqe, d->fd, dest, len, 0);
    io_uring_sqe_set_data(sqe, cr);
    suspenders_suspend();

    return (ssize_t)cr->io_result;
}

ssize_t hose_write(hose_t *d, const void *src, size_t len) {
    if (!(d->roles & HOSE_ROLE_WRITER) || d->fd < 0) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(d->ring);
    if (!sqe) return -1;

    suspenders_cr_t *cr = d->owner ? d->owner : suspenders_running;
    io_uring_prep_write(sqe, d->fd, src, len, 0);
    io_uring_sqe_set_data(sqe, cr);
    suspenders_suspend();

    return (ssize_t)cr->io_result;
}

void hose_close(hose_t *d) {
    if (d->fd >= 0) {
        close(d->fd);
        d->fd = -1;
    }
    d->roles = 0;
}

#if defined(__linux__)
void suspenders_writev_async(int fd, struct iovec *iovs, int count) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&suspenders_ring_instance);
    if (!sqe) return;
    io_uring_prep_writev(sqe, fd, iovs, count, 0);
    io_uring_sqe_set_data(sqe, suspenders_running);
    suspenders_suspend();
}
#endif

#endif /* SUSPENDERS_IMPLEMENTATION */
