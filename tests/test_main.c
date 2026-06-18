#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#include "suspenders_test.h"

static _Atomic int test_counter = 0;
static _Atomic int count1 = 0, count2 = 0, count3 = 0;

/* -------------------------------------------------------------------------- */
/* Test 1: Basic spawn and completion                                         */
/* -------------------------------------------------------------------------- */
void test_basic_cr(void *arg) {
    _Atomic int *c = (_Atomic int*)arg;
    (*c)++;
    suspenders_yield();
    (*c)++;
}

static int test_basic_spawn(void) {
    test_counter = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(test_basic_cr, (void*)&test_counter, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (test_counter == 2) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 2: Multiple interleaving coroutines                                   */
/* -------------------------------------------------------------------------- */
void counter_cr(void *arg) {
    _Atomic int *c = (_Atomic int*)arg;
    for (int i = 0; i < 5; i++) {
        (*c)++;
        suspenders_yield();
    }
}

static int test_multiple_coroutines(void) {
    count1 = 0; count2 = 0; count3 = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(counter_cr, (void*)&count1, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(counter_cr, (void*)&count2, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(counter_cr, (void*)&count3, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (count1 == 5 && count2 == 5 && count3 == 5) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 3: Yield gives up control                                             */
/* -------------------------------------------------------------------------- */
static _Atomic int yield_order[6];
static _Atomic int yield_idx = 0;

void yield_a(void *arg) {
    (void)arg;
    yield_order[atomic_fetch_add(&yield_idx, 1)] = 1;
    suspenders_yield();
    yield_order[atomic_fetch_add(&yield_idx, 1)] = 2;
}

void yield_b(void *arg) {
    (void)arg;
    yield_order[atomic_fetch_add(&yield_idx, 1)] = 3;
    suspenders_yield();
    yield_order[atomic_fetch_add(&yield_idx, 1)] = 4;
}

static int test_yield(void) {
    yield_idx = 0;
    memset((void*)yield_order, 0, sizeof(yield_order));
    suspenders_init(st_workers(), 256);
    suspenders_spawn(yield_a, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(yield_b, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    if (st_workers() <= 1) {
        /* Single worker: both start, then both resume: 1,3,2,4 or 3,1,4,2 */
        int ok = (yield_order[0] == 1 && yield_order[1] == 3 &&
                  yield_order[2] == 2 && yield_order[3] == 4) ||
                 (yield_order[0] == 3 && yield_order[1] == 1 &&
                  yield_order[2] == 4 && yield_order[3] == 2);
        return ok ? 0 : 1;
    }
    /* Parallel workers interleave freely; just require all four events. */
    int seen[5] = {0};
    for (int i = 0; i < 4; i++) {
        if (yield_order[i] < 1 || yield_order[i] > 4) return 1;
        seen[yield_order[i]]++;
    }
    return (seen[1] == 1 && seen[2] == 1 && seen[3] == 1 && seen[4] == 1) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 4: Explicit suspend and resume                                        */
/* -------------------------------------------------------------------------- */
static _Atomic int suspend_count = 0;
static suspenders_cr_t *worker_cr = NULL;

void suspend_worker(void *arg) {
    (void)arg;
    suspend_count++;
    suspenders_suspend();
    suspend_count++;
    suspenders_suspend();
    suspend_count++;
}

void suspend_controller(void *arg) {
    (void)arg;
    worker_cr = suspenders_spawn(suspend_worker, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_yield();  /* Let worker start and suspend */
    suspenders_resume(worker_cr);
    suspenders_yield();  /* Let worker run and suspend again */
    suspenders_resume(worker_cr);
}

static int test_suspend_resume(void) {
    suspend_count = 0;
    worker_cr = NULL;
    /* Manual suspend/resume choreography (yield to let the peer suspend,
     * then resume it by pointer) is a single-scheduler idiom: with parallel
     * workers a resume can race ahead of the suspend and is discarded as a
     * stray wake. Always run this test on one worker. */
    suspenders_init(1, 256);
    suspenders_spawn(suspend_controller, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (suspend_count == 3) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 5: Channel basic send/recv                                            */
/* -------------------------------------------------------------------------- */
static suspenders_chan_t *test_ch = NULL;
static _Atomic int ch_sent = 0;
static _Atomic int ch_recv = 0;

void ch_sender(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        int val = i * 10;
        if (suspenders_chan_send(test_ch, &val) == SUSPENDERS_OK)
            ch_sent++;
    }
}

void ch_receiver(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        int val = -1;
        if (suspenders_chan_recv(test_ch, &val) == SUSPENDERS_OK && val == i * 10)
            ch_recv++;
    }
}

static int test_channel_basic(void) {
    ch_sent = 0; ch_recv = 0;
    suspenders_init(st_workers(), 256);
    test_ch = suspenders_chan_create(sizeof(int), 0);
    if (!test_ch) return 1;
    suspenders_spawn(ch_receiver, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(ch_sender, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(10, ch_sent);
    ASSERT_EQ_INT(10, ch_recv);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 6: Channel rendezvous with multiple producers                         */
/* -------------------------------------------------------------------------- */
static _Atomic int ch_total_sent = 0;
static _Atomic int ch_total_recv = 0;

void ch_producer(void *arg) {
    int base = (int)(intptr_t)arg;
    for (int i = 0; i < 50; i++) {
        int val = base + i;
        if (suspenders_chan_send(test_ch, &val) == SUSPENDERS_OK)
            atomic_fetch_add(&ch_total_sent, 1);
    }
}

void ch_consumer(void *arg) {
    (void)arg;
    int expected = 3 * 50;
    for (int i = 0; i < expected; i++) {
        int val = 0;
        if (suspenders_chan_recv(test_ch, &val) == SUSPENDERS_OK)
            atomic_fetch_add(&ch_total_recv, 1);
    }
}

static int test_channel_rendezvous(void) {
    ch_total_sent = 0; ch_total_recv = 0;
    suspenders_init(st_workers(), 256);
    test_ch = suspenders_chan_create(sizeof(int), 0);
    if (!test_ch) return 1;
    suspenders_spawn(ch_consumer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(ch_producer, (void*)0, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(ch_producer, (void*)1000, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(ch_producer, (void*)2000, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(150, ch_total_sent);
    ASSERT_EQ_INT(150, ch_total_recv);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 7: Buffer append and clear                                            */
/* -------------------------------------------------------------------------- */
static int test_buffer_ops(void) {
    struct buf b = {0};
    int ok = 1;

    if (!buf_append(&b, "hello", 5)) ok = 0;
    if (!buf_append(&b, " world", 6)) ok = 0;
    if (b.len != 11 || memcmp(b.data, "hello world", 11) != 0) ok = 0;

    if (!buf_append_byte(&b, '!')) ok = 0;
    if (b.len != 12 || memcmp(b.data, "hello world!", 12) != 0) ok = 0;

    buf_clear(&b);
    if (b.len != 0) ok = 0;

    BUF_FREE(b.data, b.cap + 1);
    return ok ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 8: TCP hose dial/listen/accept/read/write                             */
/* -------------------------------------------------------------------------- */
#define TEST_PORT 54321

static _Atomic int suspenders_hose_test_pass = 0;
static _Atomic int suspenders_hose_server_ready = 0;

void suspenders_hose_server_cr(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", TEST_PORT);
    if (!suspenders_hose_listen(&listener, uri)) {
        fprintf(stderr, "[hose test] listen failed\n");
        return;
    }

    suspenders_hose_server_ready = 1;

    suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
    if (!client) { suspenders_hose_close(&listener); return; }

    if (!suspenders_hose_accept(&listener, client)) {
        memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
        suspenders_hose_close(&listener);
        return;
    }

    char buf[64];
    ssize_t n = suspenders_hose_read(client, buf, sizeof(buf));
    if (n > 0) {
        suspenders_hose_write(client, buf, n);  /* Echo back */
    }

    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
    suspenders_hose_close(&listener);
}

void suspenders_hose_client_cr(void *arg) {
    (void)arg;
    /* Wait for server to be listening (sleep: yields give no wall time
     * to a server coroutine running on another worker) */
    for (int i = 0; i < 2000 && !suspenders_hose_server_ready; i++)
        suspenders_sleep_ns(1000000ULL);

    suspenders_hose_t conn;
    struct buf b = {0};
    suspenders_hose_init(&conn, &b);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", TEST_PORT);
    if (!suspenders_hose_dial(&conn, uri)) {
        fprintf(stderr, "[hose test] dial failed\n");
        return;
    }

    const char *msg = "HELLO HOSE";
    if (suspenders_hose_write(&conn, msg, strlen(msg)) <= 0) {
        suspenders_hose_close(&conn);
        return;
    }

    char rbuf[64] = {0};
    ssize_t n = suspenders_hose_read(&conn, rbuf, sizeof(rbuf) - 1);
    if (n > 0 && strncmp(rbuf, msg, n) == 0)
        suspenders_hose_test_pass = 1;

    suspenders_hose_close(&conn);
}

static int test_hose_tcp(void) {
    suspenders_hose_test_pass = 0;
    suspenders_hose_server_ready = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(suspenders_hose_server_cr, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(suspenders_hose_client_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return suspenders_hose_test_pass ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 9: QoS ordering - high priority runs first                            */
/* -------------------------------------------------------------------------- */
static _Atomic int qos_order[3];
static _Atomic int qos_idx = 0;

void qos_low(void *arg) {
    (void)arg;
    qos_order[qos_idx++] = 3;
}

void qos_normal(void *arg) {
    (void)arg;
    qos_order[qos_idx++] = 2;
}

void qos_high(void *arg) {
    (void)arg;
    qos_order[qos_idx++] = 1;
}

static int test_qos_ordering(void) {
    qos_idx = 0;
    memset((void*)qos_order, 0, sizeof(qos_order));
    /* Strict QoS dispatch order is only observable on a single worker;
     * parallel workers legitimately run all three at once. */
    suspenders_init(1, 256);
    /* Spawn in reverse priority order */
    suspenders_spawn(qos_low, NULL, SUSPENDERS_QOS_LOW);
    suspenders_spawn(qos_normal, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(qos_high, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_run();
    suspenders_shutdown();
    /* High should run first, then normal, then low */
    return (qos_order[0] == 1 && qos_order[1] == 2 && qos_order[2] == 3) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 10: Cancel wakes a suspended coroutine                                */
/* -------------------------------------------------------------------------- */
static _Atomic int cancel_resumed = 0;
static _Atomic int cancel_done = 0;
static suspenders_cr_t *cancel_target_cr = NULL;

void cancel_target(void *arg) {
    (void)arg;
    suspenders_suspend();
    cancel_resumed = 1;
}

void cancel_trigger(void *arg) {
    (void)arg;
    suspenders_yield();  /* Let target start and suspend */
    suspenders_cancel(cancel_target_cr);
    cancel_done = 1;
}

static int test_cancel(void) {
    cancel_resumed = 0;
    cancel_done = 0;
    suspenders_init(st_workers(), 256);
    cancel_target_cr = suspenders_spawn(cancel_target, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(cancel_trigger, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (cancel_resumed == 1 && cancel_done == 1) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 11: Boost moves a suspended coroutine to higher priority              */
/* -------------------------------------------------------------------------- */
static _Atomic int boost_step = 0;
static suspenders_cr_t *boost_target_cr = NULL;

void boost_target(void *arg) {
    (void)arg;
    boost_step = 1;
    suspenders_suspend();
    boost_step = 2;
}

void boost_controller(void *arg) {
    (void)arg;
    boost_target_cr = suspenders_spawn(boost_target, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_yield();  /* Let target start and suspend */
    suspenders_boost(boost_target_cr, SUSPENDERS_QOS_HIGH);
    suspenders_resume(boost_target_cr);
    /* Controller (NORMAL=2) > target (HIGH=1), so controller preempts to target */
    /* After target completes, controller resumes here */
    boost_step = 3;
}

static int test_boost(void) {
    boost_step = 0;
    /* Same manual suspend/resume choreography as test_suspend_resume:
     * only well-defined on a single worker. */
    suspenders_init(1, 256);
    suspenders_spawn(boost_controller, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (boost_step == 3) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 12: Reentrant init/run/shutdown                                       */
/* -------------------------------------------------------------------------- */
static int test_reentrant_init(void) {
    for (int i = 0; i < 3; i++) {
        test_counter = 0;
        suspenders_init(st_workers(), 256);
        suspenders_spawn(test_basic_cr, (void*)&test_counter, SUSPENDERS_QOS_NORMAL);
        suspenders_run();
        suspenders_shutdown();
        if (test_counter != 2) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 13: Spawn chain (coroutine spawning coroutines)                       */
/* -------------------------------------------------------------------------- */
static _Atomic int chain_depth = 0;

void chain_worker(void *arg) {
    int depth = (int)(intptr_t)arg;
    if (depth < 3) {
        suspenders_spawn(chain_worker, (void*)(intptr_t)(depth + 1), SUSPENDERS_QOS_NORMAL);
    }
    chain_depth = depth;
}

static int test_spawn_chain(void) {
    chain_depth = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(chain_worker, (void*)0, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (chain_depth == 3) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 14: Hose dial failure returns false                                   */
/* -------------------------------------------------------------------------- */
static _Atomic int dial_failed = 0;

void dial_failure_cr(void *arg) {
    (void)arg;
    suspenders_hose_t conn;
    struct buf b = {0};
    suspenders_hose_init(&conn, &b);
    if (!suspenders_hose_dial(&conn, "tcp://127.0.0.1:1")) {
        dial_failed = 1;
    }
    suspenders_hose_close(&conn);
}

static int test_hose_dial_failure(void) {
    dial_failed = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(dial_failure_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return dial_failed ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 15: Hose used by coroutine other than creator                         */
/* -------------------------------------------------------------------------- */
#define CROSS_OWNER_PORT 54322

static _Atomic int cross_owner_server_ready = 0;
static _Atomic int cross_owner_hose_ready = 0;
static suspenders_hose_t *_Atomic cross_owner_conn = NULL;
static _Atomic int cross_owner_pass = 0;

void cross_owner_user(void *arg);

void cross_owner_server(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", CROSS_OWNER_PORT);
    if (!suspenders_hose_listen(&listener, uri)) return;

    cross_owner_server_ready = 1;

    suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
    if (!client) { suspenders_hose_close(&listener); return; }

    if (!suspenders_hose_accept(&listener, client)) {
        memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
        suspenders_hose_close(&listener);
        return;
    }

    /* Yield to let the client block on I/O before we echo back. */
    suspenders_yield();

    char buf[64];
    ssize_t n = suspenders_hose_read(client, buf, sizeof(buf));
    if (n > 0) {
        suspenders_hose_write(client, buf, n);
    }

    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
    suspenders_hose_close(&listener);
}

void cross_owner_creator(void *arg) {
    (void)arg;
    /* Wait for server */
    while (!cross_owner_server_ready)
        suspenders_yield();

    suspenders_hose_t *conn = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
    if (!conn) return;
    suspenders_hose_init(conn, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", CROSS_OWNER_PORT);
    if (!suspenders_hose_dial(conn, uri)) {
        memento_thread_heap_free(memento_thread_heap_get(), conn, sizeof(suspenders_hose_t));
        return;
    }

    cross_owner_conn = conn;
    cross_owner_hose_ready = 1;

    /* Spawn the user only after the hose is ready so it does not spin-wait. */
    suspenders_spawn(cross_owner_user, NULL, SUSPENDERS_QOS_NORMAL);
    /* Return without using the hose. The user coroutine will perform I/O. */
}

void cross_owner_user(void *arg) {
    (void)arg;
    if (!cross_owner_conn) return;

    const char *msg = "CROSS OWNER";
    if (suspenders_hose_write(cross_owner_conn, msg, strlen(msg)) <= 0) {
        suspenders_hose_close(cross_owner_conn);
        return;
    }

    char rbuf[64] = {0};
    ssize_t n = suspenders_hose_read(cross_owner_conn, rbuf, sizeof(rbuf) - 1);
    if (n > 0 && strncmp(rbuf, msg, n) == 0)
        cross_owner_pass = 1;

    suspenders_hose_close(cross_owner_conn);
    memento_thread_heap_free(memento_thread_heap_get(), cross_owner_conn, sizeof(suspenders_hose_t));
}

static int test_hose_cross_owner(void) {
    cross_owner_pass = 0;
    cross_owner_server_ready = 0;
    cross_owner_hose_ready = 0;
    cross_owner_conn = NULL;

    suspenders_init(st_workers(), 256);
    suspenders_spawn(cross_owner_server, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(cross_owner_creator, NULL, SUSPENDERS_QOS_NORMAL);
    /* user is spawned by creator once the hose is ready */
    suspenders_run();
    suspenders_shutdown();
    return cross_owner_pass ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 16: Identity - getid/setname/getname/self/stack_size/go              */
/* -------------------------------------------------------------------------- */
static _Atomic int ident_ok = 0;
static _Atomic uint64_t ident_first_id = 0;

void ident_second(void *arg) {
    (void)arg;
    if (suspenders_getid() != 0 && suspenders_getid() != ident_first_id)
        ident_ok++;
}

void ident_cr(void *arg) {
    (void)arg;
    ident_first_id = suspenders_getid();
    if (ident_first_id == 0) return;
    if (suspenders_self() == NULL) return;
    if (suspenders_stack_size() != SUSPENDERS_STACK_SIZE) return;
    if (suspenders_setname("ident-worker") != SUSPENDERS_OK) return;
    if (strcmp(suspenders_getname(), "ident-worker") != 0) return;
    ident_ok++;
    suspenders_go(ident_second, NULL);
}

static int test_identity(void) {
    ident_ok = 0;
    ident_first_id = 0;
    suspenders_init(st_workers(), 256);
    /* Outside a coroutine there is no identity. */
    ASSERT_EQ_INT(0, (int)suspenders_getid());
    ASSERT_NULL(suspenders_self());
    suspenders_spawn(ident_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(2, ident_ok);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 17: Sleep and cancel-while-sleeping                                  */
/* -------------------------------------------------------------------------- */
static _Atomic int sleep_status = 12345;
static _Atomic int sleep_cancel_status = 12345;
static _Atomic uint64_t sleep_elapsed_ns = 0;
static suspenders_cr_t *sleeping_cr = NULL;

void sleeper(void *arg) {
    (void)arg;
    uint64_t start = suspenders_now_ns();
    sleep_status = suspenders_sleep_ns(20 * 1000000ULL);  /* 20ms */
    sleep_elapsed_ns = suspenders_now_ns() - start;
}

void canceled_sleeper(void *arg) {
    (void)arg;
    sleep_cancel_status = suspenders_sleep_ns(10ULL * 1000000000ULL); /* 10s */
}

void sleep_canceler(void *arg) {
    (void)arg;
    suspenders_yield();  /* let the sleeper park */
    suspenders_cancel(sleeping_cr);
}

static int test_sleep_and_cancel(void) {
    sleep_status = sleep_cancel_status = 12345;
    sleep_elapsed_ns = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(sleeper, NULL, SUSPENDERS_QOS_NORMAL);
    sleeping_cr = suspenders_spawn(canceled_sleeper, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(sleep_canceler, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_OK, sleep_status);
    ASSERT_TRUE(sleep_elapsed_ns >= 15 * 1000000ULL);  /* slept most of 20ms */
    ASSERT_EQ_INT(SUSPENDERS_CANCELED, sleep_cancel_status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 18: Cleanup handlers - LIFO, pop with/without execute, run at exit   */
/* -------------------------------------------------------------------------- */
static char cleanup_log[8];
static _Atomic int cleanup_log_idx = 0;

static void cleanup_append(void *arg) {
    cleanup_log[cleanup_log_idx++] = (char)(intptr_t)arg;
}

void cleanup_cr(void *arg) {
    (void)arg;
    suspenders_cleanup_t a, b, c, d;
    suspenders_cleanup_push(&a, cleanup_append, (void*)(intptr_t)'A');
    suspenders_cleanup_push(&b, cleanup_append, (void*)(intptr_t)'B');
    suspenders_cleanup_push(&c, cleanup_append, (void*)(intptr_t)'C');
    suspenders_cleanup_push(&d, cleanup_append, (void*)(intptr_t)'D');
    suspenders_cleanup_pop(1);   /* runs D */
    suspenders_cleanup_pop(0);   /* discards C */
    /* B then A run now (LIFO), while this frame is still live. */
    suspenders_exit();
    cleanup_log[cleanup_log_idx++] = 'X';   /* must not be reached */
}

static int test_cleanup_handlers(void) {
    cleanup_log_idx = 0;
    memset(cleanup_log, 0, sizeof(cleanup_log));
    suspenders_init(st_workers(), 256);
    suspenders_spawn(cleanup_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(3, cleanup_log_idx);
    ASSERT_TRUE(cleanup_log[0] == 'D' && cleanup_log[1] == 'B' && cleanup_log[2] == 'A');
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 19: Deadline - sleeping past the deadline fails with TIMEDOUT        */
/* -------------------------------------------------------------------------- */
static _Atomic int deadline_sleep_status = 12345;
static _Atomic int deadline_next_status = 12345;
static _Atomic int deadline_disarm_status = 12345;

void deadline_cr(void *arg) {
    (void)arg;
    suspenders_deadline(suspenders_now_ns() + 10 * 1000000ULL);   /* 10ms */
    deadline_sleep_status = suspenders_sleep_ns(10ULL * 1000000000ULL);
    /* Deadline stays expired for subsequent blocking calls... */
    deadline_next_status = suspenders_sleep_ns(1000000ULL);
    /* ...until disarmed. */
    suspenders_deadline(0);
    deadline_disarm_status = suspenders_sleep_ns(1000000ULL);
}

static int test_deadline(void) {
    deadline_sleep_status = deadline_next_status = deadline_disarm_status = 12345;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(deadline_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, deadline_sleep_status);
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, deadline_next_status);
    ASSERT_EQ_INT(SUSPENDERS_OK, deadline_disarm_status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 20: Mutex - exclusion, FIFO handoff, trylock, priority inheritance   */
/* -------------------------------------------------------------------------- */
static suspenders_mutex_t test_mtx;
static _Atomic int mtx_in_critical = 0;
static _Atomic int mtx_violations = 0;
static _Atomic int mtx_order[4];
static _Atomic int mtx_order_idx = 0;
static _Atomic int mtx_trylock_busy = 0;
static _Atomic int mtx_pi_boosted = 0;

void mtx_high_contender(void *arg);

void mtx_holder(void *arg) {
    (void)arg;
    suspenders_mutex_lock(&test_mtx);
    mtx_in_critical = 1;
    /* Spawn the HIGH-qos contender only after the lock is held so it
     * actually contends (and boosts us). */
    suspenders_spawn(mtx_high_contender, NULL, SUSPENDERS_QOS_HIGH);
    /* Yield a few times so contenders queue up (and trylock fails). */
    for (int i = 0; i < 4; i++) suspenders_yield();
    if (suspenders_mutex_trylock(&test_mtx) == SUSPENDERS_BUSY)
        mtx_trylock_busy = 1;
    /* A HIGH-qos contender should have boosted us by now. */
    if (suspenders_atomic_load(suspenders_self()->effective_qos,
                               SUSPENDERS_MEMORY_ORDER_RELAXED) == SUSPENDERS_QOS_HIGH)
        mtx_pi_boosted = 1;
    mtx_in_critical = 0;
    suspenders_mutex_unlock(&test_mtx);
}

void mtx_contender(void *arg) {
    int tag = (int)(intptr_t)arg;
    if (suspenders_mutex_lock(&test_mtx) != SUSPENDERS_OK) { mtx_violations++; return; }
    if (mtx_in_critical) mtx_violations++;
    mtx_in_critical = 1;
    mtx_order[mtx_order_idx++] = tag;
    suspenders_yield();
    mtx_in_critical = 0;
    suspenders_mutex_unlock(&test_mtx);
}

void mtx_high_contender(void *arg) {
    (void)arg;
    if (suspenders_mutex_lock(&test_mtx) != SUSPENDERS_OK) { mtx_violations++; return; }
    if (mtx_in_critical) mtx_violations++;
    suspenders_mutex_unlock(&test_mtx);
}

static int test_mutex(void) {
    suspenders_mutex_init(&test_mtx);
    mtx_in_critical = 0;
    mtx_violations = 0;
    mtx_order_idx = 0;
    mtx_trylock_busy = 0;
    mtx_pi_boosted = 0;
    /* FIFO handoff order and the boost-observed-within-N-yields timing are
     * single-scheduler idioms; cross-worker mutex exclusion is covered by
     * the multiworker suite. */
    suspenders_init(1, 256);
    suspenders_spawn(mtx_holder, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mtx_contender, (void*)1, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mtx_contender, (void*)2, SUSPENDERS_QOS_NORMAL);
    /* the HIGH contender is spawned by the holder once it owns the lock */
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(0, mtx_violations);
    ASSERT_EQ_INT(1, mtx_trylock_busy);
    ASSERT_EQ_INT(1, mtx_pi_boosted);
    /* FIFO: contender 1 queued before contender 2 */
    ASSERT_EQ_INT(1, mtx_order[0]);
    ASSERT_EQ_INT(2, mtx_order[1]);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 21: Mutex lock with deadline times out                               */
/* -------------------------------------------------------------------------- */
static _Atomic int mtx_dl_status = 12345;
static _Atomic int mtx_dl_held = 0;

void mtx_dl_holder(void *arg) {
    (void)arg;
    suspenders_mutex_lock(&test_mtx);
    mtx_dl_held = 1;
    suspenders_sleep_ns(50 * 1000000ULL);   /* hold for 50ms */
    suspenders_mutex_unlock(&test_mtx);
}

void mtx_dl_waiter(void *arg) {
    (void)arg;
    /* Wait until the holder actually owns the lock — a single yield is not
     * enough with parallel workers, and winning the lock here would leave
     * it held forever. */
    while (!mtx_dl_held) suspenders_yield();
    mtx_dl_status = suspenders_mutex_lock_dl(&test_mtx,
                                             suspenders_now_ns() + 5 * 1000000ULL);
}

static int test_mutex_deadline(void) {
    suspenders_mutex_init(&test_mtx);
    mtx_dl_status = 12345;
    mtx_dl_held = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(mtx_dl_holder, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mtx_dl_waiter, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, mtx_dl_status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 22: RWLock - shared readers, exclusive writer, try variants          */
/* -------------------------------------------------------------------------- */
static suspenders_rwlock_t test_rw;
static _Atomic int rw_concurrent_readers = 0;
static _Atomic int rw_max_readers = 0;
static _Atomic int rw_writer_alone = 1;
static _Atomic int rw_try_busy = 0;

void rw_reader(void *arg) {
    (void)arg;
    if (suspenders_rwlock_rdlock(&test_rw) != SUSPENDERS_OK) return;
    int cur = atomic_fetch_add(&rw_concurrent_readers, 1) + 1;
    int max = atomic_load(&rw_max_readers);
    while (cur > max &&
           !atomic_compare_exchange_weak(&rw_max_readers, &max, cur)) {}
    suspenders_yield();
    suspenders_yield();
    atomic_fetch_sub(&rw_concurrent_readers, 1);
    suspenders_rwlock_unlock(&test_rw);
}

void rw_writer_cr(void *arg) {
    (void)arg;
    /* With parallel workers the writer can arrive first and win the
     * trylock; release it so wrlock below doesn't self-deadlock. */
    if (suspenders_rwlock_trywrlock(&test_rw) == SUSPENDERS_BUSY)
        rw_try_busy = 1;
    else
        suspenders_rwlock_unlock(&test_rw);
    if (suspenders_rwlock_wrlock(&test_rw) != SUSPENDERS_OK) return;
    if (atomic_load(&rw_concurrent_readers) != 0) rw_writer_alone = 0;
    suspenders_yield();
    if (atomic_load(&rw_concurrent_readers) != 0) rw_writer_alone = 0;
    suspenders_rwlock_unlock(&test_rw);
}

static int test_rwlock(void) {
    suspenders_rwlock_init(&test_rw);
    rw_concurrent_readers = 0;
    rw_max_readers = 0;
    rw_writer_alone = 1;
    rw_try_busy = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(rw_reader, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(rw_reader, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(rw_reader, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(rw_writer_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(rw_reader, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(1, rw_writer_alone);     /* writer was exclusive */
    ASSERT_EQ_INT(0, rw_concurrent_readers);
    if (st_workers() <= 1) {
        /* Deterministic only under a single scheduler: readers overlap via
         * yields, and the writer always finds readers holding the lock. */
        ASSERT_TRUE(rw_max_readers >= 2);
        ASSERT_EQ_INT(1, rw_try_busy);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 23: Condition variable - signal and broadcast                        */
/* -------------------------------------------------------------------------- */
static suspenders_mutex_t cond_mtx;
static suspenders_cond_t  test_cond;
static _Atomic int cond_stage = 0;
static _Atomic int cond_woken = 0;

void cond_waiter(void *arg) {
    (void)arg;
    suspenders_mutex_lock(&cond_mtx);
    while (cond_stage == 0) {
        if (suspenders_cond_wait(&test_cond, &cond_mtx) != SUSPENDERS_OK) break;
    }
    cond_woken++;
    suspenders_mutex_unlock(&cond_mtx);
}

void cond_signaler(void *arg) {
    (void)arg;
    /* Let all waiters park. */
    for (int i = 0; i < 4; i++) suspenders_yield();
    suspenders_mutex_lock(&cond_mtx);
    cond_stage = 1;
    suspenders_mutex_unlock(&cond_mtx);
    suspenders_cond_signal(&test_cond);      /* wakes one */
    for (int i = 0; i < 4; i++) suspenders_yield();
    suspenders_cond_broadcast(&test_cond);   /* wakes the rest */
}

static int test_cond_var(void) {
    suspenders_mutex_init(&cond_mtx);
    suspenders_cond_init(&test_cond);
    cond_stage = 0;
    cond_woken = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(cond_waiter, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(cond_waiter, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(cond_waiter, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(cond_signaler, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(3, cond_woken);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 24: Waitgroup - wait until N workers finish, and _dl timeout         */
/* -------------------------------------------------------------------------- */
static suspenders_waitgroup_t test_wg;
static _Atomic int wg_done_count = 0;
static _Atomic int wg_wait_status = 12345;
static _Atomic int wg_seen_at_wake = -1;
static _Atomic int wg_dl_status = 12345;

void wg_worker(void *arg) {
    (void)arg;
    suspenders_sleep_ns(2 * 1000000ULL);
    wg_done_count++;
    suspenders_waitgroup_done(&test_wg);
}

void wg_waiter(void *arg) {
    (void)arg;
    wg_wait_status = suspenders_waitgroup_wait(&test_wg);
    wg_seen_at_wake = wg_done_count;
}

void wg_dl_waiter(void *arg) {
    suspenders_waitgroup_t *never = (suspenders_waitgroup_t*)arg;
    wg_dl_status = suspenders_waitgroup_wait_dl(never,
                                                suspenders_now_ns() + 5 * 1000000ULL);
}

static int test_waitgroup(void) {
    static suspenders_waitgroup_t never_wg;
    suspenders_waitgroup_init(&test_wg);
    suspenders_waitgroup_init(&never_wg);
    suspenders_waitgroup_add(&never_wg, 1);
    wg_done_count = 0;
    wg_wait_status = 12345;
    wg_seen_at_wake = -1;
    wg_dl_status = 12345;
    suspenders_init(st_workers(), 256);
    suspenders_waitgroup_add(&test_wg, 3);
    suspenders_spawn(wg_waiter, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(wg_worker, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(wg_worker, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(wg_worker, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(wg_dl_waiter, &never_wg, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_OK, wg_wait_status);
    ASSERT_EQ_INT(3, wg_seen_at_wake);
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, wg_dl_status);
    /* never_wg still holds count 1; draining past zero is rejected. */
    ASSERT_EQ_INT(SUSPENDERS_OK, suspenders_waitgroup_done(&never_wg));
    ASSERT_EQ_INT(SUSPENDERS_INVAL, suspenders_waitgroup_done(&never_wg));
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 25: Cancel a coroutine blocked on a channel                          */
/* -------------------------------------------------------------------------- */
static _Atomic int chan_cancel_result = 12345;
static suspenders_cr_t *blocked_receiver_cr = NULL;

void blocked_receiver(void *arg) {
    (void)arg;
    int val = 0;
    chan_cancel_result = suspenders_chan_recv(test_ch, &val);
}

void chan_canceler(void *arg) {
    (void)arg;
    suspenders_yield();   /* let the receiver park */
    suspenders_cancel(blocked_receiver_cr);
}

static int test_channel_cancel(void) {
    chan_cancel_result = 12345;
    suspenders_init(st_workers(), 256);
    test_ch = suspenders_chan_create(sizeof(int), 0);
    ASSERT_NOT_NULL(test_ch);
    blocked_receiver_cr = suspenders_spawn(blocked_receiver, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(chan_canceler, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_CANCELED, chan_cancel_result);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 26: Buffered channel - FIFO ordering, non-blocking until full        */
/* -------------------------------------------------------------------------- */
static _Atomic int buf_order_ok = 1;
static _Atomic int buf_recv_count = 0;
static _Atomic int buf_send_blocked_at = -1;

void buf_producer(void *arg) {
    (void)arg;
    for (int i = 0; i < 8; i++) {
        /* Capacity is 4: sends 0..3 complete without a receiver; send 4
         * must block until the consumer starts draining. */
        if (i == 4) buf_send_blocked_at = buf_recv_count;
        if (suspenders_chan_send(test_ch, &i) != SUSPENDERS_OK) buf_order_ok = 0;
    }
}

void buf_consumer(void *arg) {
    (void)arg;
    for (int i = 0; i < 8; i++) {
        int val = -1;
        if (suspenders_chan_recv(test_ch, &val) != SUSPENDERS_OK || val != i)
            buf_order_ok = 0;
        buf_recv_count++;
    }
}

static int test_channel_buffered(void) {
    buf_order_ok = 1;
    buf_recv_count = 0;
    buf_send_blocked_at = -1;
    suspenders_init(st_workers(), 256);
    test_ch = suspenders_chan_create(sizeof(int), 4);
    ASSERT_NOT_NULL(test_ch);
    /* Producer first: it fills the buffer before the consumer ever runs. */
    suspenders_spawn(buf_producer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(buf_consumer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(1, buf_order_ok);
    ASSERT_EQ_INT(8, buf_recv_count);
    if (st_workers() <= 1) {
        /* Single scheduler: the producer fills the buffer before the
         * consumer ever runs. With parallel workers the consumer may
         * legally drain concurrently, so only FIFO order is asserted. */
        ASSERT_EQ_INT(0, buf_send_blocked_at);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 27: try_send / try_recv - FULL and EMPTY without blocking            */
/* -------------------------------------------------------------------------- */
static int test_channel_try_ops(void) {
    suspenders_init(st_workers(), 256);
    suspenders_chan_t *ch = suspenders_chan_create(sizeof(int), 2);
    ASSERT_NOT_NULL(ch);
    int v = 7, out = 0;

    /* try ops work from the main thread (no coroutine needed) */
    ASSERT_EQ_INT(SUSPENDERS_EMPTY, suspenders_chan_try_recv(ch, &out));
    ASSERT_EQ_INT(SUSPENDERS_OK, suspenders_chan_try_send(ch, &v));
    v = 8;
    ASSERT_EQ_INT(SUSPENDERS_OK, suspenders_chan_try_send(ch, &v));
    v = 9;
    ASSERT_EQ_INT(SUSPENDERS_FULL, suspenders_chan_try_send(ch, &v));
    ASSERT_EQ_INT(SUSPENDERS_OK, suspenders_chan_try_recv(ch, &out));
    ASSERT_EQ_INT(7, out);
    ASSERT_EQ_INT(SUSPENDERS_OK, suspenders_chan_try_recv(ch, &out));
    ASSERT_EQ_INT(8, out);
    ASSERT_EQ_INT(SUSPENDERS_EMPTY, suspenders_chan_try_recv(ch, &out));

    /* rendezvous channel: try ops fail without a waiting peer */
    suspenders_chan_t *rz = suspenders_chan_create(sizeof(int), 0);
    ASSERT_NOT_NULL(rz);
    ASSERT_EQ_INT(SUSPENDERS_FULL, suspenders_chan_try_send(rz, &v));
    ASSERT_EQ_INT(SUSPENDERS_EMPTY, suspenders_chan_try_recv(rz, &out));

    suspenders_chan_destroy(ch);
    suspenders_chan_destroy(rz);
    suspenders_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 28: close - drain semantics, blocked waiters fail, send-on-closed    */
/* -------------------------------------------------------------------------- */
static _Atomic int close_drained = 0;
static _Atomic int close_recv_status = 12345;
static _Atomic int close_blocked_recv_status = 12345;
static _Atomic int close_send_status = 12345;

void close_blocked_receiver(void *arg) {
    (void)arg;
    int v = 0;
    close_blocked_recv_status = suspenders_chan_recv(test_ch, &v);
}

void close_driver(void *arg) {
    suspenders_chan_t *buffered = (suspenders_chan_t*)arg;
    /* Fill the buffered channel, close it, then drain past the end. */
    for (int i = 0; i < 3; i++) suspenders_chan_send(buffered, &i);
    suspenders_chan_close(buffered);
    int v = -1;
    while (suspenders_chan_recv(buffered, &v) == SUSPENDERS_OK) close_drained++;
    close_recv_status = suspenders_errno;
    close_send_status = suspenders_chan_send(buffered, &v);

    /* Close the rendezvous channel out from under a blocked receiver. */
    suspenders_yield();   /* let close_blocked_receiver park */
    suspenders_chan_close(test_ch);
}

static int test_channel_close(void) {
    close_drained = 0;
    close_recv_status = close_blocked_recv_status = close_send_status = 12345;
    suspenders_init(st_workers(), 256);
    suspenders_chan_t *buffered = suspenders_chan_create(sizeof(int), 4);
    test_ch = suspenders_chan_create(sizeof(int), 0);
    ASSERT_NOT_NULL(buffered);
    ASSERT_NOT_NULL(test_ch);
    suspenders_spawn(close_blocked_receiver, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(close_driver, buffered, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(buffered);
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(3, close_drained);                        /* buffer drained after close */
    ASSERT_EQ_INT(SUSPENDERS_CLOSED, close_recv_status);    /* then CLOSED */
    ASSERT_EQ_INT(SUSPENDERS_CLOSED, close_send_status);    /* send on closed */
    ASSERT_EQ_INT(SUSPENDERS_CLOSED, close_blocked_recv_status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 29: select - readiness, statistical fairness, blocking wake          */
/* -------------------------------------------------------------------------- */
static suspenders_chan_t *sel_a, *sel_b;
static _Atomic int sel_pick_a = 0, sel_pick_b = 0;
static _Atomic int sel_block_idx = -2;
static _Atomic int sel_block_val = 0;

void select_fairness_cr(void *arg) {
    (void)arg;
    /* Both channels always ready: distribution should hit both. */
    for (int i = 0; i < 200; i++) {
        int out = 0;
        suspenders_chan_op_t ops[2] = {
            { sel_a, &out, false },
            { sel_b, &out, false },
        };
        int idx = suspenders_select(ops, 2);
        if (idx == 0) { sel_pick_a++; suspenders_chan_send(sel_a, &out); }
        else if (idx == 1) { sel_pick_b++; suspenders_chan_send(sel_b, &out); }
    }
}

void select_blocker_cr(void *arg) {
    (void)arg;
    int out_a = 0, out_b = 0;
    suspenders_chan_op_t ops[2] = {
        { sel_a, &out_a, false },
        { sel_b, &out_b, false },
    };
    sel_block_idx = suspenders_select(ops, 2);    /* nothing ready: parks */
    sel_block_val = (sel_block_idx == 1) ? out_b : out_a;
}

void select_waker_cr(void *arg) {
    (void)arg;
    suspenders_yield();  /* let the blocker park on both channels */
    int v = 42;
    suspenders_chan_send(sel_b, &v);
}

static int test_select(void) {
    sel_pick_a = sel_pick_b = 0;
    sel_block_idx = -2;
    sel_block_val = 0;
    suspenders_init(st_workers(), 256);

    /* Fairness: both buffered channels pre-loaded and refilled each pick. */
    sel_a = suspenders_chan_create(sizeof(int), 1);
    sel_b = suspenders_chan_create(sizeof(int), 1);
    ASSERT_NOT_NULL(sel_a);
    ASSERT_NOT_NULL(sel_b);
    int seed_val = 1;
    suspenders_chan_try_send(sel_a, &seed_val);
    suspenders_chan_try_send(sel_b, &seed_val);
    suspenders_spawn(select_fairness_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    ASSERT_EQ_INT(200, sel_pick_a + sel_pick_b);
    ASSERT_TRUE(sel_pick_a >= 40 && sel_pick_b >= 40);   /* both sides chosen */

    /* Drain the leftovers so the blocking select actually parks. */
    int drain;
    while (suspenders_chan_try_recv(sel_a, &drain) == SUSPENDERS_OK) {}
    while (suspenders_chan_try_recv(sel_b, &drain) == SUSPENDERS_OK) {}

    /* Blocking select woken by a send on the second channel. */
    suspenders_spawn(select_blocker_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(select_waker_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    ASSERT_EQ_INT(1, sel_block_idx);
    ASSERT_EQ_INT(42, sel_block_val);

    suspenders_chan_destroy(sel_a);
    suspenders_chan_destroy(sel_b);
    suspenders_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 30: select - deadline, cancel, and close delivery                    */
/* -------------------------------------------------------------------------- */
static _Atomic int sel_dl_status = 12345;
static _Atomic int sel_cancel_status = 12345;
static _Atomic int sel_close_idx = -2;
static _Atomic int sel_close_errno = 12345;
static suspenders_cr_t *sel_cancel_cr = NULL;

void select_deadline_cr(void *arg) {
    (void)arg;
    int out = 0;
    suspenders_chan_op_t ops[1] = { { sel_a, &out, false } };
    sel_dl_status = suspenders_select_dl(ops, 1, suspenders_now_ns() + 5 * 1000000ULL);
}

void select_cancel_victim(void *arg) {
    (void)arg;
    int out = 0;
    suspenders_chan_op_t ops[2] = {
        { sel_a, &out, false },
        { sel_b, &out, false },
    };
    sel_cancel_status = suspenders_select(ops, 2);
}

void select_canceler(void *arg) {
    (void)arg;
    suspenders_yield();
    suspenders_cancel(sel_cancel_cr);
}

void select_close_cr(void *arg) {
    (void)arg;
    int out = 0;
    suspenders_chan_op_t ops[2] = {
        { sel_a, &out, false },
        { sel_b, &out, false },
    };
    sel_close_idx = suspenders_select(ops, 2);
    sel_close_errno = suspenders_errno;
}

void select_closer(void *arg) {
    (void)arg;
    suspenders_yield();
    suspenders_chan_close(sel_b);
}

static int test_select_edge_cases(void) {
    sel_dl_status = sel_cancel_status = sel_close_errno = 12345;
    sel_close_idx = -2;
    suspenders_init(st_workers(), 256);
    sel_a = suspenders_chan_create(sizeof(int), 0);
    sel_b = suspenders_chan_create(sizeof(int), 0);
    ASSERT_NOT_NULL(sel_a);
    ASSERT_NOT_NULL(sel_b);

    suspenders_spawn(select_deadline_cr, NULL, SUSPENDERS_QOS_NORMAL);
    sel_cancel_cr = suspenders_spawn(select_cancel_victim, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(select_canceler, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, sel_dl_status);
    ASSERT_EQ_INT(SUSPENDERS_CANCELED, sel_cancel_status);

    /* Close wakes a parked select with the closed case's index. */
    suspenders_spawn(select_close_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(select_closer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    ASSERT_EQ_INT(1, sel_close_idx);
    ASSERT_EQ_INT(SUSPENDERS_CLOSED, sel_close_errno);

    suspenders_chan_destroy(sel_a);
    suspenders_chan_destroy(sel_b);
    suspenders_shutdown();
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 31: chan recv with deadline times out                                */
/* -------------------------------------------------------------------------- */
static _Atomic int chan_dl_status = 12345;

void chan_dl_cr(void *arg) {
    (void)arg;
    int out = 0;
    chan_dl_status = suspenders_chan_recv_dl(test_ch, &out,
                                             suspenders_now_ns() + 5 * 1000000ULL);
}

static int test_channel_deadline(void) {
    chan_dl_status = 12345;
    suspenders_init(st_workers(), 256);
    test_ch = suspenders_chan_create(sizeof(int), 0);
    ASSERT_NOT_NULL(test_ch);
    suspenders_spawn(chan_dl_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(test_ch);
    test_ch = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(SUSPENDERS_TIMEDOUT, chan_dl_status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 32: hose readv/writev round-trip                                      */
/* -------------------------------------------------------------------------- */
#define IOVEC_TEST_PORT 54322

static _Atomic int iovec_test_pass = 0;
static _Atomic int iovec_server_ready = 0;

void iovec_server_cr(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", IOVEC_TEST_PORT);
    if (!suspenders_hose_listen(&listener, uri)) return;
    iovec_server_ready = 1;

    suspenders_hose_t client;
    if (!suspenders_hose_accept(&listener, &client)) {
        suspenders_hose_close(&listener);
        return;
    }
    char buf[64];
    ssize_t n = suspenders_hose_read(&client, buf, sizeof(buf));
    if (n > 0) suspenders_hose_write(&client, buf, (size_t)n);
    suspenders_hose_close(&client);
    suspenders_hose_close(&listener);
}

void iovec_client_cr(void *arg) {
    (void)arg;
    for (int i = 0; i < 2000 && !iovec_server_ready; i++) suspenders_sleep_ns(1000000ULL);

    suspenders_hose_t conn;
    suspenders_hose_init(&conn, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", IOVEC_TEST_PORT);
    if (!suspenders_hose_dial(&conn, uri)) return;

    char part1[] = "hello, ";
    char part2[] = "world";
    struct iovec wv[2] = { { part1, 7 }, { part2, 5 } };
    if (suspenders_hose_writev(&conn, wv, 2) != 12) {
        suspenders_hose_close(&conn);
        return;
    }

    char r1[7], r2[8];
    struct iovec rv[2] = { { r1, sizeof(r1) }, { r2, sizeof(r2) } };
    ssize_t n = suspenders_hose_readv(&conn, rv, 2);
    if (n == 12 && memcmp(r1, "hello, ", 7) == 0 && memcmp(r2, "world", 5) == 0) {
        iovec_test_pass = 1;
    }
    suspenders_hose_close(&conn);
}

static int test_hose_readv_writev(void) {
    iovec_test_pass = 0;
    iovec_server_ready = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(iovec_server_cr, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(iovec_client_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(1, iovec_test_pass);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 33: hose read deadline fires (silent peer)                            */
/* -------------------------------------------------------------------------- */
#define RDDL_TEST_PORT 54323

static _Atomic int rddl_test_pass = 0;
static _Atomic int rddl_server_ready = 0;

void rddl_server_cr(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", RDDL_TEST_PORT);
    if (!suspenders_hose_listen(&listener, uri)) return;
    rddl_server_ready = 1;

    suspenders_hose_t client;
    if (!suspenders_hose_accept(&listener, &client)) {
        suspenders_hose_close(&listener);
        return;
    }
    /* Send nothing; hold the connection until the client hangs up. */
    char buf[8];
    (void)suspenders_hose_read(&client, buf, sizeof(buf));
    suspenders_hose_close(&client);
    suspenders_hose_close(&listener);
}

void rddl_client_cr(void *arg) {
    (void)arg;
    for (int i = 0; i < 2000 && !rddl_server_ready; i++) suspenders_sleep_ns(1000000ULL);

    suspenders_hose_t conn;
    suspenders_hose_init(&conn, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", RDDL_TEST_PORT);
    if (!suspenders_hose_dial(&conn, uri)) return;

    uint64_t start = suspenders_now_ns();
    char buf[8];
    ssize_t n = suspenders_hose_read_dl(&conn, buf, sizeof(buf),
                                        start + 30 * 1000000ULL);
    int err = suspenders_errno;
    uint64_t elapsed = suspenders_now_ns() - start;
    if (n == -1 && err == SUSPENDERS_TIMEDOUT && elapsed >= 25 * 1000000ULL) {
        rddl_test_pass = 1;
    }
    suspenders_hose_close(&conn);
}

static int test_hose_read_deadline(void) {
    rddl_test_pass = 0;
    rddl_server_ready = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(rddl_server_cr, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(rddl_client_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(1, rddl_test_pass);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 34: hose accept deadline fires (no client)                            */
/* -------------------------------------------------------------------------- */
#define ACDL_TEST_PORT 54324

static _Atomic int acdl_test_pass = 0;

void acdl_server_cr(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", ACDL_TEST_PORT);
    if (!suspenders_hose_listen(&listener, uri)) return;

    uint64_t start = suspenders_now_ns();
    suspenders_hose_t client;
    bool ok = suspenders_hose_accept_dl(&listener, &client,
                                        start + 30 * 1000000ULL);
    int err = suspenders_errno;
    uint64_t elapsed = suspenders_now_ns() - start;
    if (!ok && err == SUSPENDERS_TIMEDOUT && elapsed >= 25 * 1000000ULL) {
        acdl_test_pass = 1;
    }
    suspenders_hose_close(&listener);
}

static int test_hose_accept_deadline(void) {
    acdl_test_pass = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(acdl_server_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(1, acdl_test_pass);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 35: cancel a coroutine blocked in hose read                           */
/* -------------------------------------------------------------------------- */
#define CXL_TEST_PORT 54325

static _Atomic int cxl_test_pass = 0;
static _Atomic int cxl_server_ready = 0;
static _Atomic int cxl_reading = 0;
static suspenders_cr_t *_Atomic cxl_victim = NULL;

void cxl_server_cr(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", CXL_TEST_PORT);
    if (!suspenders_hose_listen(&listener, uri)) return;
    cxl_server_ready = 1;

    suspenders_hose_t client;
    if (!suspenders_hose_accept(&listener, &client)) {
        suspenders_hose_close(&listener);
        return;
    }
    char buf[8];
    (void)suspenders_hose_read(&client, buf, sizeof(buf));
    suspenders_hose_close(&client);
    suspenders_hose_close(&listener);
}

void cxl_victim_cr(void *arg) {
    (void)arg;
    for (int i = 0; i < 2000 && !cxl_server_ready; i++) suspenders_sleep_ns(1000000ULL);

    suspenders_hose_t conn;
    suspenders_hose_init(&conn, NULL);
    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", CXL_TEST_PORT);
    if (!suspenders_hose_dial(&conn, uri)) return;

    cxl_reading = 1;
    char buf[8];
    ssize_t n = suspenders_hose_read(&conn, buf, sizeof(buf));
    if (n == -1 && suspenders_errno == SUSPENDERS_CANCELED) cxl_test_pass = 1;
    suspenders_hose_close(&conn);
}

void cxl_canceler_cr(void *arg) {
    (void)arg;
    for (int i = 0; i < 200 && !cxl_reading; i++) suspenders_yield();
    suspenders_sleep_ns(10 * 1000000ULL);  /* let the victim park in the read */
    suspenders_cancel(cxl_victim);
}

static int test_hose_cancel_read(void) {
    cxl_test_pass = 0;
    cxl_server_ready = 0;
    cxl_reading = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(cxl_server_cr, NULL, SUSPENDERS_QOS_HIGH);
    cxl_victim = suspenders_spawn(cxl_victim_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(cxl_canceler_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(1, cxl_test_pass);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Multiworker suite - these always run with 4 workers regardless of         */
/* SUSPENDERS_TEST_WORKERS so the default run exercises the parallel paths.  */
/* -------------------------------------------------------------------------- */

/* Test 36: 10k coroutines across 4 workers */
static _Atomic int mw_spawn_done = 0;

static void mw_spawn_worker(void *arg) {
    (void)arg;
    suspenders_yield();
    atomic_fetch_add(&mw_spawn_done, 1);
}

static int test_mw_spawn_storm(void) {
    mw_spawn_done = 0;
    suspenders_init(4, 256);
    for (int i = 0; i < 10000; i++) {
        suspenders_qos_t q = (suspenders_qos_t)(i % 4);
        ASSERT_TRUE(suspenders_spawn(mw_spawn_worker, NULL, q) != NULL);
    }
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(10000, mw_spawn_done);
    return 0;
}

/* Test 37: cross-worker channel producers/consumers (rendezvous+buffered) */
static suspenders_chan_t *mw_chan = NULL;
static _Atomic long mw_chan_sum = 0;
static _Atomic int mw_chan_recv_n = 0;
#define MW_CHAN_PRODUCERS 8
#define MW_CHAN_PER_PRODUCER 500

static void mw_chan_producer(void *arg) {
    int base = (int)(intptr_t)arg;
    for (int i = 0; i < MW_CHAN_PER_PRODUCER; i++) {
        int v = base + i;
        if (suspenders_chan_send(mw_chan, &v) != SUSPENDERS_OK) return;
    }
}

static void mw_chan_consumer(void *arg) {
    (void)arg;
    int v;
    while (suspenders_chan_recv(mw_chan, &v) == SUSPENDERS_OK) {
        atomic_fetch_add(&mw_chan_sum, (long)v);
        if (atomic_fetch_add(&mw_chan_recv_n, 1) + 1 ==
            MW_CHAN_PRODUCERS * MW_CHAN_PER_PRODUCER) {
            suspenders_chan_close(mw_chan);
        }
    }
}

static int mw_chan_run(size_t buf_sz) {
    mw_chan_sum = 0;
    mw_chan_recv_n = 0;
    suspenders_init(4, 256);
    mw_chan = suspenders_chan_create(sizeof(int), buf_sz);
    if (!mw_chan) return 1;
    long expect = 0;
    for (int p = 0; p < MW_CHAN_PRODUCERS; p++) {
        int base = p * 10000;
        for (int i = 0; i < MW_CHAN_PER_PRODUCER; i++) expect += base + i;
        suspenders_spawn(mw_chan_producer, (void*)(intptr_t)base, SUSPENDERS_QOS_NORMAL);
    }
    for (int c = 0; c < 4; c++)
        suspenders_spawn(mw_chan_consumer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(mw_chan);
    mw_chan = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(MW_CHAN_PRODUCERS * MW_CHAN_PER_PRODUCER, mw_chan_recv_n);
    ASSERT_TRUE(mw_chan_sum == expect);
    return 0;
}

static int test_mw_channel_rendezvous(void) { return mw_chan_run(0); }
static int test_mw_channel_buffered(void)   { return mw_chan_run(16); }

/* Test 38: cross-worker sleep/cancel ping-pong via channels               */
static suspenders_chan_t *mw_ping = NULL;
static suspenders_chan_t *mw_pong = NULL;
static _Atomic int mw_pp_rounds = 0;

static void mw_pinger(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        int v = i;
        if (suspenders_chan_send(mw_ping, &v) != SUSPENDERS_OK) return;
        if (suspenders_chan_recv(mw_pong, &v) != SUSPENDERS_OK) return;
        if (v != i * 2) return;
        atomic_fetch_add(&mw_pp_rounds, 1);
    }
}

static void mw_ponger(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        int v;
        if (suspenders_chan_recv(mw_ping, &v) != SUSPENDERS_OK) return;
        v *= 2;
        if (suspenders_chan_send(mw_pong, &v) != SUSPENDERS_OK) return;
    }
}

static int test_mw_pingpong(void) {
    mw_pp_rounds = 0;
    suspenders_init(4, 256);
    mw_ping = suspenders_chan_create(sizeof(int), 0);
    mw_pong = suspenders_chan_create(sizeof(int), 0);
    if (!mw_ping || !mw_pong) return 1;
    suspenders_spawn(mw_pinger, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mw_ponger, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_run();
    suspenders_chan_destroy(mw_ping);
    suspenders_chan_destroy(mw_pong);
    mw_ping = mw_pong = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(1000, mw_pp_rounds);
    return 0;
}

/* Test 39: cross-worker mutex exclusion under contention */
static suspenders_mutex_t mw_mtx;
static _Atomic int mw_mtx_in = 0;
static _Atomic int mw_mtx_violations = 0;
static _Atomic long mw_mtx_acquired = 0;

static void mw_mtx_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 200; i++) {
        if (suspenders_mutex_lock(&mw_mtx) != SUSPENDERS_OK) return;
        if (atomic_fetch_add(&mw_mtx_in, 1) != 0)
            atomic_fetch_add(&mw_mtx_violations, 1);
        if (i % 3 == 0) suspenders_yield();
        atomic_fetch_sub(&mw_mtx_in, 1);
        atomic_fetch_add(&mw_mtx_acquired, 1);
        suspenders_mutex_unlock(&mw_mtx);
    }
}

static int test_mw_mutex(void) {
    suspenders_mutex_init(&mw_mtx);
    mw_mtx_in = 0;
    mw_mtx_violations = 0;
    mw_mtx_acquired = 0;
    suspenders_init(4, 256);
    for (int i = 0; i < 8; i++)
        suspenders_spawn(mw_mtx_worker, NULL,
                         (suspenders_qos_t)(i % 4));
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(0, mw_mtx_violations);
    ASSERT_TRUE(mw_mtx_acquired == 8 * 200);
    return 0;
}

/* Test 40: select across workers (registration re-check race) */
static suspenders_chan_t *mw_sel_a = NULL;
static suspenders_chan_t *mw_sel_b = NULL;
static _Atomic int mw_sel_recv = 0;

static void mw_sel_feeder_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 300; i++) {
        int v = 1;
        if (suspenders_chan_send(mw_sel_a, &v) != SUSPENDERS_OK) return;
    }
}

static void mw_sel_feeder_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 300; i++) {
        int v = 2;
        if (suspenders_chan_send(mw_sel_b, &v) != SUSPENDERS_OK) return;
    }
}

static void mw_sel_receiver(void *arg) {
    (void)arg;
    int va = 0, vb = 0;
    for (int i = 0; i < 300; i++) {
        suspenders_chan_op_t ops[2] = {
            { mw_sel_a, &va, false },
            { mw_sel_b, &vb, false },
        };
        int r = suspenders_select(ops, 2);
        if (r < 0) return;
        atomic_fetch_add(&mw_sel_recv, 1);
    }
}

static int test_mw_select(void) {
    mw_sel_recv = 0;
    suspenders_init(4, 256);
    mw_sel_a = suspenders_chan_create(sizeof(int), 0);
    mw_sel_b = suspenders_chan_create(sizeof(int), 0);
    if (!mw_sel_a || !mw_sel_b) return 1;
    suspenders_spawn(mw_sel_feeder_a, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mw_sel_feeder_b, NULL, SUSPENDERS_QOS_NORMAL);
    /* two receivers so both feeders always drain: 600 sends, 600 selects */
    suspenders_spawn(mw_sel_receiver, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(mw_sel_receiver, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_run();
    suspenders_chan_destroy(mw_sel_a);
    suspenders_chan_destroy(mw_sel_b);
    mw_sel_a = mw_sel_b = NULL;
    suspenders_shutdown();
    ASSERT_EQ_INT(600, mw_sel_recv);
    return 0;
}

/* Test 41: shutdown while helpers are still busy (no run() drain) */
static _Atomic int mw_sd_started = 0;

static void mw_sd_sleeper(void *arg) {
    (void)arg;
    atomic_fetch_add(&mw_sd_started, 1);
    suspenders_sleep_ns(10ULL * 1000000000ULL);   /* 10s: outlives the test */
}

static int test_mw_shutdown_busy(void) {
    mw_sd_started = 0;
    suspenders_init(4, 256);
    for (int i = 0; i < 64; i++)
        suspenders_spawn(mw_sd_sleeper, NULL, SUSPENDERS_QOS_NORMAL);
    /* Helper workers pull from the injector without suspenders_run();
     * give them a moment to park in the long sleep, then tear down. */
    struct timespec ts = { 0, 50 * 1000000 };
    nanosleep(&ts, NULL);
    suspenders_shutdown();
    /* No hang, no crash; the next init/run cycle must still work. */
    test_counter = 0;
    suspenders_init(4, 256);
    suspenders_spawn(test_basic_cr, (void*)&test_counter, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(2, test_counter);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 42: serial queue runs tasks in strict submission order               */
/* -------------------------------------------------------------------------- */
#define Q_SERIAL_N 32
static _Atomic int q_order_idx = 0;
static int q_order_log[Q_SERIAL_N];

static void q_order_task(void *arg) {
    int slot = atomic_fetch_add(&q_order_idx, 1);
    if (slot < Q_SERIAL_N) q_order_log[slot] = (int)(intptr_t)arg;
    suspenders_yield();   /* give reordering every chance to happen */
}

static void q_serial_cr(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("serial", SUSPENDERS_QOS_NORMAL, 1);
    if (!q) return;
    for (int i = 0; i < Q_SERIAL_N; i++)
        suspenders_queue_async(q, q_order_task, (void*)(intptr_t)i);
    suspenders_queue_destroy(q);   /* waits for the drain */
}

static int test_queue_serial_order(void) {
    q_order_idx = 0;
    memset(q_order_log, -1, sizeof(q_order_log));
    suspenders_init(st_workers(), 256);
    suspenders_spawn(q_serial_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(Q_SERIAL_N, q_order_idx);
    for (int i = 0; i < Q_SERIAL_N; i++) ASSERT_EQ_INT(i, q_order_log[i]);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 43: concurrent queue overlaps tasks                                   */
/* -------------------------------------------------------------------------- */
static _Atomic int q_conc_running = 0;
static _Atomic int q_conc_peak = 0;
static _Atomic int q_conc_done = 0;

static void q_conc_task(void *arg) {
    (void)arg;
    int now = atomic_fetch_add(&q_conc_running, 1) + 1;
    int peak = atomic_load(&q_conc_peak);
    while (now > peak &&
           !atomic_compare_exchange_weak(&q_conc_peak, &peak, now)) {}
    suspenders_sleep_ns(5 * 1000000ULL);   /* 5ms: force overlap */
    atomic_fetch_sub(&q_conc_running, 1);
    atomic_fetch_add(&q_conc_done, 1);
}

static void q_conc_cr(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("conc", SUSPENDERS_QOS_NORMAL, 4);
    if (!q) return;
    for (int i = 0; i < 12; i++)
        suspenders_queue_async(q, q_conc_task, NULL);
    suspenders_queue_destroy(q);
}

static int test_queue_concurrent(void) {
    q_conc_running = 0; q_conc_peak = 0; q_conc_done = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(q_conc_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(12, q_conc_done);
    /* Drainers sleep concurrently even on one scheduler worker (they are
     * separate coroutines), so overlap must appear regardless. */
    ASSERT_TRUE(q_conc_peak >= 2);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 44: barrier runs alone, after earlier tasks, before later ones        */
/* -------------------------------------------------------------------------- */
static _Atomic int qb_pre_done = 0;
static _Atomic int qb_running = 0;
static _Atomic int qb_barrier_ok = -1;
static _Atomic int qb_barrier_done = 0;
static _Atomic int qb_post_early = 0;
static _Atomic int qb_post_done = 0;

static void qb_pre_task(void *arg) {
    (void)arg;
    atomic_fetch_add(&qb_running, 1);
    suspenders_sleep_ns(2 * 1000000ULL);
    atomic_fetch_sub(&qb_running, 1);
    atomic_fetch_add(&qb_pre_done, 1);
}

static void qb_barrier_task(void *arg) {
    (void)arg;
    /* Alone: nothing running, every earlier task complete. */
    atomic_store(&qb_barrier_ok,
                 (atomic_load(&qb_running) == 0 &&
                  atomic_load(&qb_pre_done) == 8) ? 1 : 0);
    suspenders_sleep_ns(2 * 1000000ULL);
    atomic_store(&qb_barrier_done, 1);
}

static void qb_post_task(void *arg) {
    (void)arg;
    if (!atomic_load(&qb_barrier_done)) atomic_fetch_add(&qb_post_early, 1);
    atomic_fetch_add(&qb_post_done, 1);
}

static void qb_cr(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("barrier", SUSPENDERS_QOS_NORMAL, 4);
    if (!q) return;
    for (int i = 0; i < 8; i++) suspenders_queue_async(q, qb_pre_task, NULL);
    suspenders_queue_barrier_async(q, qb_barrier_task, NULL);
    for (int i = 0; i < 8; i++) suspenders_queue_async(q, qb_post_task, NULL);
    suspenders_queue_destroy(q);
}

static int test_queue_barrier(void) {
    qb_pre_done = 0; qb_running = 0; qb_barrier_ok = -1;
    qb_barrier_done = 0; qb_post_early = 0; qb_post_done = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(qb_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(8, qb_pre_done);
    ASSERT_EQ_INT(1, qb_barrier_ok);
    ASSERT_EQ_INT(0, qb_post_early);
    ASSERT_EQ_INT(8, qb_post_done);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 45: queue_sync completes before returning; queue_after fires on time  */
/* -------------------------------------------------------------------------- */
static _Atomic int q_sync_val = 0;
static _Atomic int q_after_fired = 0;
static _Atomic int q_after_early = 0;
static uint64_t q_after_armed_ns = 0;

static void q_sync_task(void *arg) {
    suspenders_sleep_ns(2 * 1000000ULL);
    atomic_store((_Atomic int*)arg, 77);
}

static void q_after_task(void *arg) {
    (void)arg;
    if (suspenders_now_ns() - q_after_armed_ns < 10 * 1000000ULL)
        atomic_store(&q_after_early, 1);
    atomic_store(&q_after_fired, 1);
}

static void q_sync_cr(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("sync", SUSPENDERS_QOS_NORMAL, 2);
    if (!q) return;
    q_after_armed_ns = suspenders_now_ns();
    suspenders_queue_after(q, 10 * 1000000ULL /* 10ms */, q_after_task, NULL);
    if (suspenders_queue_sync(q, q_sync_task, (void*)&q_sync_val) != SUSPENDERS_OK) {
        suspenders_queue_destroy(q);
        return;
    }
    /* sync returned: the task body must have completed */
    if (atomic_load(&q_sync_val) != 77) atomic_store(&q_sync_val, -1);
    while (!atomic_load(&q_after_fired)) suspenders_sleep_ns(1000000ULL);
    suspenders_queue_destroy(q);
}

static int test_queue_sync_after(void) {
    q_sync_val = 0; q_after_fired = 0; q_after_early = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(q_sync_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(77, q_sync_val);
    ASSERT_EQ_INT(1, q_after_fired);
    ASSERT_EQ_INT(0, q_after_early);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 46: destroy with pending tasks drains them; global queues just work   */
/* -------------------------------------------------------------------------- */
static _Atomic int q_drain_done = 0;
static _Atomic int q_global_done = 0;

static void q_drain_task(void *arg) {
    (void)arg;
    suspenders_yield();
    atomic_fetch_add(&q_drain_done, 1);
}

static void q_global_task(void *arg) {
    (void)arg;
    atomic_fetch_add(&q_global_done, 1);
}

static void q_destroy_cr(void *arg) {
    (void)arg;
    suspenders_queue_t *q = suspenders_queue_create("drain", SUSPENDERS_QOS_NORMAL, 2);
    if (!q) return;
    for (int i = 0; i < 50; i++)
        suspenders_queue_async(q, q_drain_task, NULL);
    suspenders_queue_destroy(q);   /* immediately: all 50 must still run */

    suspenders_queue_t *g1 = suspenders_get_global_queue(SUSPENDERS_QOS_NORMAL);
    suspenders_queue_t *g2 = suspenders_get_global_queue(SUSPENDERS_QOS_NORMAL);
    if (g1 != g2 || !g1) return;   /* same shared instance required */
    for (int i = 0; i < 10; i++)
        suspenders_queue_async(g1, q_global_task, NULL);
    /* never destroyed: suspenders_shutdown reclaims global queues */
}

static int test_queue_destroy_global(void) {
    q_drain_done = 0; q_global_done = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(q_destroy_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(50, q_drain_done);
    ASSERT_EQ_INT(10, q_global_done);
    /* Re-init after global-queue teardown must start clean. */
    q_drain_done = 0; q_global_done = 0;
    suspenders_init(st_workers(), 256);
    suspenders_spawn(q_destroy_cr, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    ASSERT_EQ_INT(50, q_drain_done);
    ASSERT_EQ_INT(10, q_global_done);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */
static const st_test_t st_tests[] = {
    ST_TEST(test_basic_spawn),
    ST_TEST(test_multiple_coroutines),
    ST_TEST(test_yield),
    ST_TEST(test_suspend_resume),
    ST_TEST(test_channel_basic),
    ST_TEST(test_channel_rendezvous),
    ST_TEST(test_buffer_ops),
    ST_TEST(test_hose_tcp),
    ST_TEST(test_qos_ordering),
    ST_TEST(test_cancel),
    ST_TEST(test_boost),
    ST_TEST(test_reentrant_init),
    ST_TEST(test_spawn_chain),
    ST_TEST(test_hose_dial_failure),
    ST_TEST(test_hose_cross_owner),
    ST_TEST(test_identity),
    ST_TEST(test_sleep_and_cancel),
    ST_TEST(test_cleanup_handlers),
    ST_TEST(test_deadline),
    ST_TEST(test_mutex),
    ST_TEST(test_mutex_deadline),
    ST_TEST(test_rwlock),
    ST_TEST(test_cond_var),
    ST_TEST(test_waitgroup),
    ST_TEST(test_channel_cancel),
    ST_TEST(test_channel_buffered),
    ST_TEST(test_channel_try_ops),
    ST_TEST(test_channel_close),
    ST_TEST(test_select),
    ST_TEST(test_select_edge_cases),
    ST_TEST(test_channel_deadline),
    ST_TEST(test_hose_readv_writev),
    ST_TEST(test_hose_read_deadline),
    ST_TEST(test_hose_accept_deadline),
    ST_TEST(test_hose_cancel_read),
    ST_TEST(test_mw_spawn_storm),
    ST_TEST(test_mw_channel_rendezvous),
    ST_TEST(test_mw_channel_buffered),
    ST_TEST(test_mw_pingpong),
    ST_TEST(test_mw_mutex),
    ST_TEST(test_mw_select),
    ST_TEST(test_mw_shutdown_busy),
    ST_TEST(test_queue_serial_order),
    ST_TEST(test_queue_concurrent),
    ST_TEST(test_queue_barrier),
    ST_TEST(test_queue_sync_after),
    ST_TEST(test_queue_destroy_global),
};

int main(int argc, char **argv) {
    printf("\n=== Suspenders Test Suite ===\n\n");
    return st_main(argc, argv, st_tests, sizeof(st_tests) / sizeof(st_tests[0]));
}
