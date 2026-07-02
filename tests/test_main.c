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
/* Test 16: Identity - getid/setname/getname/self/stack_size/go              */
/* -------------------------------------------------------------------------- */
static volatile int ident_ok = 0;
static volatile uint64_t ident_first_id = 0;

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
static volatile int sleep_status = 12345;
static volatile int sleep_cancel_status = 12345;
static volatile uint64_t sleep_elapsed_ns = 0;
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
static volatile int cleanup_log_idx = 0;

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
static volatile int deadline_sleep_status = 12345;
static volatile int deadline_next_status = 12345;
static volatile int deadline_disarm_status = 12345;

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
static volatile int mtx_in_critical = 0;
static volatile int mtx_violations = 0;
static volatile int mtx_order[4];
static volatile int mtx_order_idx = 0;
static volatile int mtx_trylock_busy = 0;
static volatile int mtx_pi_boosted = 0;

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
    suspenders_init(st_workers(), 256);
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
static volatile int mtx_dl_status = 12345;

void mtx_dl_holder(void *arg) {
    (void)arg;
    suspenders_mutex_lock(&test_mtx);
    suspenders_sleep_ns(50 * 1000000ULL);   /* hold for 50ms */
    suspenders_mutex_unlock(&test_mtx);
}

void mtx_dl_waiter(void *arg) {
    (void)arg;
    suspenders_yield();  /* let the holder take the lock */
    mtx_dl_status = suspenders_mutex_lock_dl(&test_mtx,
                                             suspenders_now_ns() + 5 * 1000000ULL);
}

static int test_mutex_deadline(void) {
    suspenders_mutex_init(&test_mtx);
    mtx_dl_status = 12345;
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
static volatile int rw_concurrent_readers = 0;
static volatile int rw_max_readers = 0;
static volatile int rw_writer_alone = 1;
static volatile int rw_try_busy = 0;

void rw_reader(void *arg) {
    (void)arg;
    if (suspenders_rwlock_rdlock(&test_rw) != SUSPENDERS_OK) return;
    rw_concurrent_readers++;
    if (rw_concurrent_readers > rw_max_readers)
        rw_max_readers = rw_concurrent_readers;
    suspenders_yield();
    suspenders_yield();
    rw_concurrent_readers--;
    suspenders_rwlock_unlock(&test_rw);
}

void rw_writer_cr(void *arg) {
    (void)arg;
    if (suspenders_rwlock_trywrlock(&test_rw) == SUSPENDERS_BUSY)
        rw_try_busy = 1;
    if (suspenders_rwlock_wrlock(&test_rw) != SUSPENDERS_OK) return;
    if (rw_concurrent_readers != 0) rw_writer_alone = 0;
    suspenders_yield();
    if (rw_concurrent_readers != 0) rw_writer_alone = 0;
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
    ASSERT_TRUE(rw_max_readers >= 2);      /* readers overlapped */
    ASSERT_EQ_INT(1, rw_writer_alone);     /* writer was exclusive */
    ASSERT_EQ_INT(1, rw_try_busy);         /* trywrlock saw readers */
    ASSERT_EQ_INT(0, rw_concurrent_readers);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test 23: Condition variable - signal and broadcast                        */
/* -------------------------------------------------------------------------- */
static suspenders_mutex_t cond_mtx;
static suspenders_cond_t  test_cond;
static volatile int cond_stage = 0;
static volatile int cond_woken = 0;

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
static volatile int wg_done_count = 0;
static volatile int wg_wait_status = 12345;
static volatile int wg_seen_at_wake = -1;
static volatile int wg_dl_status = 12345;

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
static volatile int chan_cancel_result = 12345;
static suspenders_cr_t *blocked_receiver_cr = NULL;

void blocked_receiver(void *arg) {
    (void)arg;
    int val = 0;
    chan_cancel_result = suspenders_chan_recv(test_ch, &val) ? 1 : 0;
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
    ASSERT_EQ_INT(0, chan_cancel_result);   /* recv failed with cancel */
    ASSERT_EQ_INT(SUSPENDERS_CANCELED, suspenders_errno);
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
};

int main(int argc, char **argv) {
    printf("\n=== Suspenders Test Suite ===\n\n");
    return st_main(argc, argv, st_tests, sizeof(st_tests) / sizeof(st_tests[0]));
}
