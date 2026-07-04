/* timeout_retry.c - Deadline-driven retry pattern with exponential backoff
 *
 * Demonstrates: per-call deadlines (_dl variants), timers, cancellation,
 * cleanup handlers, channels as result delivery, waitgroup.
 *
 * A "requester" coroutine issues work to a pool of "flaky workers" that
 * randomly fail or take too long. The requester retries with exponential
 * backoff, using per-call deadlines to bound each attempt. Results are
 * delivered through per-request reply channels.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define NUM_REQUESTS    20
#define MAX_RETRIES     4
#define BASE_TIMEOUT_MS 50
#define NUM_WORKERS     4

typedef struct {
    int request_id;
    int attempt;
    int reply_idx;   /* index into shared reply_chs array */
} request_t;

typedef struct {
    int  request_id;
    int  result;
    bool success;
} response_t;

static suspenders_chan_t *work_ch = NULL;
static suspenders_chan_t *reply_chs[NUM_REQUESTS];  /* one per requester */
static suspenders_waitgroup_t req_wg;

static _Atomic int attempts_total = 0;
static _Atomic int successes = 0;
static _Atomic int failures = 0;
static _Atomic int timeouts_total = 0;

/* Flaky worker: ~30% chance of being "slow" (sleeps past deadline),
 * ~10% chance of outright failure. */
static void flaky_worker(void *arg) {
    (void)arg;
    for (;;) {
        request_t req;
        int rc = suspenders_chan_recv(work_ch, &req);
        if (rc != SUSPENDERS_OK) break;

        atomic_fetch_add(&attempts_total, 1);
        int fate = rand() % 100;

        response_t resp = { req.request_id, 0, false };

        if (fate < 10) {
            /* Outright failure — reply immediately */
            resp.result = -1;
            resp.success = false;
        } else if (fate < 40) {
            /* Slow — will likely exceed the caller's per-call deadline */
            suspenders_sleep_ns(200ULL * 1000000);
            resp.result = req.request_id * 10;
            resp.success = true;
        } else {
            /* Quick success */
            suspenders_sleep_ns(5ULL * 1000000);
            resp.result = req.request_id * 10;
            resp.success = true;
        }

        /* try_send: if requester already timed out and moved on,
         * the buffered channel absorbs this or drops it. */
        suspenders_chan_try_send(reply_chs[req.reply_idx], &resp);
    }
}

/* One request with retry logic */
static void do_request(int id) {
    uint64_t timeout_ms = BASE_TIMEOUT_MS;

    for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
        /* Drain any stale replies from a prior slow worker */
        response_t stale;
        while (suspenders_chan_try_recv(reply_chs[id], &stale) == SUSPENDERS_OK)
            ;

        request_t req = { id, attempt, id };
        int rc = suspenders_chan_send(work_ch, &req);
        if (rc != SUSPENDERS_OK) {
            printf("[req %2d] send failed: %s\n", id, suspenders_strerror(rc));
            atomic_fetch_add(&failures, 1);
            return;
        }

        /* Wait for response with a per-call deadline */
        response_t resp;
        uint64_t deadline = suspenders_now_ns() + timeout_ms * 1000000ULL;
        rc = suspenders_chan_recv_dl(reply_chs[id], &resp, deadline);

        if (rc == SUSPENDERS_TIMEDOUT) {
            atomic_fetch_add(&timeouts_total, 1);
            if (attempt < MAX_RETRIES) {
                printf("[req %2d] attempt %d timed out (%llums), retrying...\n",
                       id, attempt, (unsigned long long)timeout_ms);
                timeout_ms *= 2;
                continue;
            }
            printf("[req %2d] exhausted retries\n", id);
            atomic_fetch_add(&failures, 1);
            return;
        }

        if (rc != SUSPENDERS_OK) {
            printf("[req %2d] recv error: %s\n", id, suspenders_strerror(rc));
            atomic_fetch_add(&failures, 1);
            return;
        }

        if (resp.success) {
            atomic_fetch_add(&successes, 1);
            if (attempt > 0)
                printf("[req %2d] succeeded on attempt %d, result=%d\n",
                       id, attempt, resp.result);
            return;
        }

        /* Worker reported failure — retry */
        if (attempt < MAX_RETRIES) {
            printf("[req %2d] worker failed, retrying...\n", id);
            timeout_ms *= 2;
        } else {
            printf("[req %2d] worker failed, no retries left\n", id);
            atomic_fetch_add(&failures, 1);
        }
    }
}

static void requester(void *arg) {
    int id = (int)(intptr_t)arg;
    do_request(id);
    suspenders_waitgroup_done(&req_wg);
}

static void coordinator(void *arg) {
    (void)arg;

    for (int i = 0; i < NUM_WORKERS; i++)
        suspenders_spawn(flaky_worker, NULL, SUSPENDERS_QOS_NORMAL);

    for (int i = 0; i < NUM_REQUESTS; i++)
        suspenders_spawn(requester, (void *)(intptr_t)i,
                         SUSPENDERS_QOS_NORMAL);

    /* Wait for all requesters to finish */
    suspenders_waitgroup_wait(&req_wg);

    /* Now close work channel — workers exit */
    suspenders_chan_close(work_ch);

    /* Give workers time to exit before reply channels are destroyed */
    suspenders_sleep_ns(50ULL * 1000000);
}

int main(void) {
    printf("=== Timeout & Retry Pattern ===\n\n");
    srand(42);

    suspenders_init(0, 256);
    work_ch = suspenders_chan_create(sizeof(request_t), 8);
    suspenders_waitgroup_init(&req_wg);
    suspenders_waitgroup_add(&req_wg, NUM_REQUESTS);

    /* Pre-create all reply channels (buffered 4 — absorbs late replies) */
    for (int i = 0; i < NUM_REQUESTS; i++)
        reply_chs[i] = suspenders_chan_create(sizeof(response_t), 4);

    suspenders_spawn(coordinator, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_run();

    printf("\n=== Results ===\n");
    printf("Attempts: %d  Successes: %d  Failures: %d  Timeouts: %d\n",
           atomic_load(&attempts_total), atomic_load(&successes),
           atomic_load(&failures), atomic_load(&timeouts_total));

    int total = atomic_load(&successes) + atomic_load(&failures);
    printf("Completed: %d/%d requests\n", total, NUM_REQUESTS);
    printf("%s\n", total == NUM_REQUESTS ? "SUCCESS" : "FAILURE");

    /* Cleanup after run — all coroutines are done */
    for (int i = 0; i < NUM_REQUESTS; i++)
        suspenders_chan_destroy(reply_chs[i]);
    suspenders_chan_destroy(work_ch);
    suspenders_shutdown();
    return total == NUM_REQUESTS ? 0 : 1;
}
