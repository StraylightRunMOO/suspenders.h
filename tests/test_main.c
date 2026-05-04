#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#include "suspenders_test.h"

static volatile int test_counter = 0;
static volatile int count1 = 0, count2 = 0, count3 = 0;

/* -------------------------------------------------------------------------- */
/* Test 1: Basic spawn and completion                                         */
/* -------------------------------------------------------------------------- */
void test_basic_cr(void *arg) {
    volatile int *c = (volatile int*)arg;
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
    volatile int *c = (volatile int*)arg;
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
static volatile int yield_order[6];
static volatile int yield_idx = 0;

void yield_a(void *arg) {
    (void)arg;
    yield_order[yield_idx++] = 1;
    suspenders_yield();
    yield_order[yield_idx++] = 2;
}

void yield_b(void *arg) {
    (void)arg;
    yield_order[yield_idx++] = 3;
    suspenders_yield();
    yield_order[yield_idx++] = 4;
}

static int test_yield(void) {
    yield_idx = 0;
    memset((void*)yield_order, 0, sizeof(yield_order));
    suspenders_init(st_workers(), 256);
    suspenders_spawn(yield_a, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(yield_b, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    /* Both start, then both resume: order is 1,3,2,4 or 3,1,4,2 */
    int ok = (yield_order[0] == 1 && yield_order[1] == 3 &&
              yield_order[2] == 2 && yield_order[3] == 4) ||
             (yield_order[0] == 3 && yield_order[1] == 1 &&
              yield_order[2] == 4 && yield_order[3] == 2);
    return ok ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 4: Explicit suspend and resume                                        */
/* -------------------------------------------------------------------------- */
static volatile int suspend_count = 0;
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
    suspenders_init(st_workers(), 256);
    suspenders_spawn(suspend_controller, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    return (suspend_count == 3) ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test 5: Channel basic send/recv                                            */
/* -------------------------------------------------------------------------- */
static suspenders_chan_t *test_ch = NULL;
static volatile int ch_sent = 0;
static volatile int ch_recv = 0;

void ch_sender(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        int val = i * 10;
        if (suspenders_chan_send(test_ch, &val))
            ch_sent++;
    }
}

void ch_receiver(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        int val = -1;
        if (suspenders_chan_recv(test_ch, &val) && val == i * 10)
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
static volatile int ch_total_sent = 0;
static volatile int ch_total_recv = 0;

void ch_producer(void *arg) {
    int base = (int)(intptr_t)arg;
    for (int i = 0; i < 50; i++) {
        int val = base + i;
        if (suspenders_chan_send(test_ch, &val))
            __atomic_fetch_add(&ch_total_sent, 1, __ATOMIC_RELAXED);
    }
}

void ch_consumer(void *arg) {
    (void)arg;
    int expected = 3 * 50;
    for (int i = 0; i < expected; i++) {
        int val = 0;
        if (suspenders_chan_recv(test_ch, &val))
            __atomic_fetch_add(&ch_total_recv, 1, __ATOMIC_RELAXED);
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

static volatile int suspenders_hose_test_pass = 0;
static volatile int suspenders_hose_server_ready = 0;

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
    /* Wait for server to be listening */
    for (int i = 0; i < 100 && !suspenders_hose_server_ready; i++)
        suspenders_yield();

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
static volatile int qos_order[3];
static volatile int qos_idx = 0;

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
    suspenders_init(st_workers(), 256);
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
static volatile int cancel_resumed = 0;
static volatile int cancel_done = 0;
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
static volatile int boost_step = 0;
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
    suspenders_init(st_workers(), 256);
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
static volatile int chain_depth = 0;

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
static volatile int dial_failed = 0;

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

static volatile int cross_owner_server_ready = 0;
static volatile int cross_owner_hose_ready = 0;
static suspenders_hose_t *volatile cross_owner_conn = NULL;
static volatile int cross_owner_pass = 0;

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
};

int main(int argc, char **argv) {
    printf("\n=== Suspenders Test Suite ===\n\n");
    return st_main(argc, argv, st_tests, sizeof(st_tests) / sizeof(st_tests[0]));
}
