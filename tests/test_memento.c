/* test_memento.c - Memento allocator tests, focused on cross-thread safety.
 *
 * Gate for the suspenders multi-worker scheduler: must be clean under
 * ASan, UBSan, and TSan.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#include "suspenders_test.h"
#include <pthread.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Local alloc/free/realloc round trips (small, cached, large/mmap)           */
/* -------------------------------------------------------------------------- */
static int test_local_alloc_free(void) {
    ASSERT_TRUE(memento_init());
    memento_thread_heap_t *h = memento_thread_heap_get();
    ASSERT_NOT_NULL(h);

    void *p = memento_thread_heap_alloc(h, 100);
    ASSERT_NOT_NULL(p);
    memset(p, 0xAB, 100);
    memento_thread_heap_free(h, p, 100);

    /* Same class comes back from the cache */
    void *p2 = memento_thread_heap_alloc(h, 100);
    ASSERT_NOT_NULL(p2);
    memento_thread_heap_free(h, p2, 100);

    /* Large allocation (> 8192): header + mmap path */
    void *q = memento_thread_heap_alloc(h, 100 * 1024);
    ASSERT_NOT_NULL(q);
    memset(q, 1, 100 * 1024);
    memento_thread_heap_free(h, q, 100 * 1024);

    /* Mid-size large alloc (> 8192 but < mmap threshold) */
    void *m = memento_thread_heap_alloc(h, 10000);
    ASSERT_NOT_NULL(m);
    memset(m, 2, 10000);
    memento_thread_heap_free(h, m, 10000);

    void *r = memento_thread_heap_realloc(h, NULL, 0, 128);
    ASSERT_NOT_NULL(r);
    memset(r, 3, 128);
    r = memento_thread_heap_realloc(h, r, 128, 4096);
    ASSERT_NOT_NULL(r);
    r = memento_thread_heap_realloc(h, r, 4096, 0);
    ASSERT_NULL(r);

    /* Flush on an empty foreign stack is a no-op */
    memento_thread_heap_flush(h);

    memento_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Foreign-free stress: main allocates, N threads free concurrently           */
/* -------------------------------------------------------------------------- */
#define FF_THREADS 4
#define FF_ITEMS   4000

typedef struct {
    void  *ptr;
    size_t size;
} ff_item_t;

typedef struct {
    ff_item_t *items;
    size_t     count;
    memento_thread_heap_t *owner;
} ff_share_t;

static void *ff_free_worker(void *arg) {
    ff_share_t *share = (ff_share_t *)arg;
    for (size_t i = 0; i < share->count; i++) {
        memento_thread_heap_free(share->owner, share->items[i].ptr, share->items[i].size);
    }
    return NULL;
}

static int test_foreign_free_stress(void) {
    ASSERT_TRUE(memento_init());
    memento_thread_heap_t *h = memento_thread_heap_get();
    ASSERT_NOT_NULL(h);

    static ff_item_t items[FF_THREADS][FF_ITEMS];
    /* Mixed sizes: exercises every branch incl. >8192 header blocks */
    static const size_t sizes[] = { 24, 64, 100, 500, 1500, 8000, 9000, 20000 };

    for (int t = 0; t < FF_THREADS; t++) {
        for (int i = 0; i < FF_ITEMS; i++) {
            size_t sz = sizes[(t * FF_ITEMS + i) % (sizeof(sizes) / sizeof(sizes[0]))];
            void *p = memento_thread_heap_alloc(h, sz);
            ASSERT_NOT_NULL(p);
            memset(p, t + 1, sz);
            items[t][i].ptr = p;
            items[t][i].size = sz;
        }
    }

    memento_heap_stats_t before;
    memento_thread_heap_stats(h, &before);

    pthread_t threads[FF_THREADS];
    ff_share_t shares[FF_THREADS];
    for (int t = 0; t < FF_THREADS; t++) {
        shares[t].items = items[t];
        shares[t].count = FF_ITEMS;
        shares[t].owner = h;
        ASSERT_EQ_INT(0, pthread_create(&threads[t], NULL, ff_free_worker, &shares[t]));
    }
    for (int t = 0; t < FF_THREADS; t++) {
        ASSERT_EQ_INT(0, pthread_join(threads[t], NULL));
    }

    /* Owner drains everything that was pushed */
    memento_thread_heap_flush(h);

    memento_heap_stats_t after;
    memento_thread_heap_stats(h, &after);
    ASSERT_EQ_INT(FF_THREADS * FF_ITEMS,
                  (long long)(after.foreign_free_count - before.foreign_free_count));

    /* Heap still functional afterwards */
    void *p = memento_thread_heap_alloc(h, 256);
    ASSERT_NOT_NULL(p);
    memento_thread_heap_free(h, p, 256);

    memento_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Thread death: frees targeting a retired heap park and are reclaimed by     */
/* shutdown (ASan leak check validates reclamation)                           */
/* -------------------------------------------------------------------------- */
#define TD_ITEMS 512

typedef struct {
    ff_item_t items[TD_ITEMS];
    memento_thread_heap_t *heap;
} td_share_t;

static void *td_alloc_worker(void *arg) {
    td_share_t *share = (td_share_t *)arg;
    memento_thread_heap_t *h = memento_thread_heap_get();
    if (!h) return NULL;
    share->heap = h;
    for (int i = 0; i < TD_ITEMS; i++) {
        size_t sz = (i % 2) ? 128 : 12000;
        void *p = memento_thread_heap_alloc(h, sz);
        if (!p) return NULL;
        memset(p, 7, sz);
        share->items[i].ptr = p;
        share->items[i].size = sz;
    }
    return share;   /* thread exits; TLS destructor retires the heap */
}

static int test_thread_death_drain(void) {
    ASSERT_TRUE(memento_init());
    ASSERT_NOT_NULL(memento_thread_heap_get());

    static td_share_t share;
    memset(&share, 0, sizeof(share));

    pthread_t thread;
    ASSERT_EQ_INT(0, pthread_create(&thread, NULL, td_alloc_worker, &share));
    void *ret = NULL;
    ASSERT_EQ_INT(0, pthread_join(thread, &ret));
    ASSERT_NOT_NULL(ret);
    ASSERT_NOT_NULL(share.heap);

    /* The owning thread is gone; these frees park on the retired heap's
     * foreign stack until shutdown reclaims them. */
    for (int i = 0; i < TD_ITEMS; i++) {
        memento_thread_heap_free(share.heap, share.items[i].ptr, share.items[i].size);
    }

    memento_shutdown();   /* reclaims the retired heap + parked frees */
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Alloc/free storm: every thread churns locally and frees a neighbor's       */
/* blocks concurrently                                                        */
/* -------------------------------------------------------------------------- */
#define STORM_THREADS 4
#define STORM_ITEMS   2000
#define STORM_ROUNDS  4

typedef struct {
    int id;
    pthread_barrier_t *barrier;
    ff_item_t handoff[STORM_ITEMS];      /* blocks this thread gives away */
    memento_thread_heap_t *heap;
    int failed;
} storm_ctx_t;

static storm_ctx_t storm_ctx[STORM_THREADS];

static void *storm_worker(void *arg) {
    storm_ctx_t *ctx = (storm_ctx_t *)arg;
    memento_thread_heap_t *h = memento_thread_heap_get();
    if (!h) { ctx->failed = 1; return NULL; }
    ctx->heap = h;

    for (int round = 0; round < STORM_ROUNDS; round++) {
        /* Local churn */
        for (int i = 0; i < 500; i++) {
            size_t sz = 32 + (size_t)(i % 6) * 300;
            void *p = memento_thread_heap_alloc(h, sz);
            if (!p) { ctx->failed = 1; return NULL; }
            memento_thread_heap_free(h, p, sz);
        }
        /* Allocate blocks to hand to the neighbor */
        for (int i = 0; i < STORM_ITEMS; i++) {
            size_t sz = (i % 5 == 0) ? 16000 : (64 + (size_t)(i % 4) * 128);
            void *p = memento_thread_heap_alloc(h, sz);
            if (!p) { ctx->failed = 1; return NULL; }
            ctx->handoff[i].ptr = p;
            ctx->handoff[i].size = sz;
        }
        pthread_barrier_wait(ctx->barrier);
        /* Free the previous neighbor's blocks (foreign) */
        storm_ctx_t *prev = &storm_ctx[(ctx->id + STORM_THREADS - 1) % STORM_THREADS];
        for (int i = 0; i < STORM_ITEMS; i++) {
            memento_thread_heap_free(prev->heap, prev->handoff[i].ptr, prev->handoff[i].size);
        }
        pthread_barrier_wait(ctx->barrier);
        /* Owner reclaims its foreign stack */
        memento_thread_heap_flush(h);
    }
    return NULL;
}

static int test_alloc_free_storm(void) {
    ASSERT_TRUE(memento_init());
    ASSERT_NOT_NULL(memento_thread_heap_get());

    pthread_barrier_t barrier;
    ASSERT_EQ_INT(0, pthread_barrier_init(&barrier, NULL, STORM_THREADS));

    pthread_t threads[STORM_THREADS];
    for (int t = 0; t < STORM_THREADS; t++) {
        memset(&storm_ctx[t], 0, sizeof(storm_ctx[t]));
        storm_ctx[t].id = t;
        storm_ctx[t].barrier = &barrier;
        ASSERT_EQ_INT(0, pthread_create(&threads[t], NULL, storm_worker, &storm_ctx[t]));
    }
    for (int t = 0; t < STORM_THREADS; t++) {
        ASSERT_EQ_INT(0, pthread_join(threads[t], NULL));
        ASSERT_EQ_INT(0, storm_ctx[t].failed);
    }
    pthread_barrier_destroy(&barrier);

    memento_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Pool: available-count semantics, exhaustion, destroy frees the block       */
/* -------------------------------------------------------------------------- */
static int test_pool(void) {
    ASSERT_TRUE(memento_init());
    memento_thread_heap_t *h = memento_thread_heap_get();
    ASSERT_NOT_NULL(h);

    memento_pool_t *pool = memento_pool_create(48, 8, h);
    ASSERT_NOT_NULL(pool);

    void *objs[8];
    for (int i = 0; i < 8; i++) {
        objs[i] = memento_pool_alloc(pool);
        ASSERT_NOT_NULL(objs[i]);
        memset(objs[i], i, 48);
    }
    ASSERT_NULL(memento_pool_alloc(pool));   /* exhausted */

    for (int i = 0; i < 8; i++) {
        memento_pool_free(pool, objs[i]);
    }
    void *again = memento_pool_alloc(pool);
    ASSERT_NOT_NULL(again);
    memento_pool_free(pool, again);

    memento_pool_destroy(pool);   /* must free the backing block (ASan) */
    memento_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Arena: alignment honored; destroy from another thread is foreign-safe      */
/* -------------------------------------------------------------------------- */
static void *arena_destroy_worker(void *arg) {
    memento_arena_destroy((memento_arena_t *)arg);
    return NULL;
}

static int test_arena_aligned_and_foreign_destroy(void) {
    ASSERT_TRUE(memento_init());
    memento_thread_heap_t *h = memento_thread_heap_get();
    ASSERT_NOT_NULL(h);

    memento_arena_t *arena = memento_arena_create(1 << 16, h);
    ASSERT_NOT_NULL(arena);

    void *a = memento_arena_alloc(arena, 1000, 64);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ_INT(0, (long long)((uintptr_t)a & 63));
    void *b = memento_arena_alloc(arena, 33, 16);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_INT(0, (long long)((uintptr_t)b & 15));
    /* Force growth */
    void *c = memento_arena_alloc(arena, 1 << 18, 64);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ_INT(0, (long long)((uintptr_t)c & 63));

    /* Destroy from a different thread: block frees go through the foreign
     * path onto this heap's stack */
    pthread_t thread;
    ASSERT_EQ_INT(0, pthread_create(&thread, NULL, arena_destroy_worker, arena));
    ASSERT_EQ_INT(0, pthread_join(thread, NULL));

    memento_thread_heap_flush(h);   /* reclaim */

    memento_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
static const st_test_t st_tests[] = {
    ST_TEST(test_local_alloc_free),
    ST_TEST(test_foreign_free_stress),
    ST_TEST(test_thread_death_drain),
    ST_TEST(test_alloc_free_storm),
    ST_TEST(test_pool),
    ST_TEST(test_arena_aligned_and_foreign_destroy),
};

int main(int argc, char **argv) {
    printf("\n=== Memento Allocator Test Suite ===\n\n");
    return st_main(argc, argv, st_tests, sizeof(st_tests) / sizeof(st_tests[0]));
}
