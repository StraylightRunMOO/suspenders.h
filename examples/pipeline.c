/* pipeline.c - Multi-stage processing pipeline with backpressure
 *
 * Demonstrates: buffered channels, waitgroup, cancellation, deadlines,
 * QoS priorities, suspenders_strerror.
 *
 * Architecture:
 *   [generators] --ch_raw(rendezvous)--> [filters] --ch_filtered(buf 64)--> [aggregator]
 *
 * Three generators produce integers. Two filters (HIGH QoS) discard odds
 * and forward evens through a buffered channel. One aggregator sums them.
 * A closer coroutine watches the generators via a waitgroup, then tears
 * down the pipeline in order. A watchdog at REALTIME cancels everything
 * if the pipeline hasn't finished in 2 seconds.
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

#define NUM_GENERATORS 3
#define NUM_FILTERS    2
#define ITEMS_PER_GEN  500

static suspenders_chan_t *ch_raw      = NULL;
static suspenders_chan_t *ch_filtered = NULL;

static suspenders_waitgroup_t gen_wg;
static suspenders_waitgroup_t filter_wg;
static suspenders_cr_t *watchdog_cr = NULL;

static _Atomic long filtered_count = 0;

static void generator(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < ITEMS_PER_GEN; i++) {
        int val = id * ITEMS_PER_GEN + i;
        int rc = suspenders_chan_send(ch_raw, &val);
        if (rc != SUSPENDERS_OK) {
            printf("[gen %d] send failed at %d: %s\n",
                   id, i, suspenders_strerror(rc));
            break;
        }
    }
    suspenders_waitgroup_done(&gen_wg);
}

static void filter(void *arg) {
    int id = (int)(intptr_t)arg;
    for (;;) {
        int val;
        int rc = suspenders_chan_recv(ch_raw, &val);
        if (rc == SUSPENDERS_CLOSED) break;
        if (rc != SUSPENDERS_OK) {
            printf("[filter %d] recv: %s\n", id, suspenders_strerror(rc));
            break;
        }
        if (val % 2 == 0) {
            rc = suspenders_chan_send(ch_filtered, &val);
            if (rc != SUSPENDERS_OK) break;
            atomic_fetch_add(&filtered_count, 1);
        }
    }
    suspenders_waitgroup_done(&filter_wg);
}

static void aggregator(void *arg) {
    long *result = (long *)arg;
    long sum = 0;
    int count = 0;
    for (;;) {
        int val;
        int rc = suspenders_chan_recv(ch_filtered, &val);
        if (rc == SUSPENDERS_CLOSED) break;
        if (rc != SUSPENDERS_OK) break;
        sum += val;
        count++;
    }
    *result = sum;
    printf("[agg] summed %d values, total = %ld\n", count, sum);
}

static void closer(void *arg) {
    (void)arg;
    /* Wait for all generators to finish, then close the raw channel. */
    suspenders_waitgroup_wait(&gen_wg);
    suspenders_chan_close(ch_raw);

    /* Wait for all filters to drain ch_raw and exit. */
    suspenders_waitgroup_wait(&filter_wg);
    suspenders_chan_close(ch_filtered);

    /* Cancel the watchdog — pipeline finished in time. */
    if (watchdog_cr)
        suspenders_cancel(watchdog_cr);
}

static void watchdog(void *arg) {
    suspenders_cr_t **pipeline_crs = (suspenders_cr_t **)arg;
    int rc = suspenders_sleep_ns(2000ULL * 1000000);
    if (rc == SUSPENDERS_CANCELED) return;

    printf("[watchdog] TIMEOUT — tearing down pipeline\n");
    int n = (int)(intptr_t)pipeline_crs[0];
    for (int i = 1; i <= n; i++)
        suspenders_cancel(pipeline_crs[i]);
    suspenders_chan_close(ch_raw);
    suspenders_chan_close(ch_filtered);
}

int main(void) {
    printf("=== Pipeline: generators -> filters -> aggregator ===\n\n");

    suspenders_init(0, 256);

    ch_raw      = suspenders_chan_create(sizeof(int), 0);
    ch_filtered = suspenders_chan_create(sizeof(int), 64);

    suspenders_waitgroup_init(&gen_wg);
    suspenders_waitgroup_init(&filter_wg);
    suspenders_waitgroup_add(&gen_wg, NUM_GENERATORS);
    suspenders_waitgroup_add(&filter_wg, NUM_FILTERS);

    long result = 0;
    int total = NUM_GENERATORS + NUM_FILTERS + 1;
    suspenders_cr_t *crs[1 + NUM_GENERATORS + NUM_FILTERS + 1];
    crs[0] = (suspenders_cr_t *)(intptr_t)total;
    int ci = 1;

    crs[ci++] = suspenders_spawn(aggregator, &result, SUSPENDERS_QOS_NORMAL);
    for (int i = 0; i < NUM_FILTERS; i++)
        crs[ci++] = suspenders_spawn(filter, (void *)(intptr_t)i,
                                     SUSPENDERS_QOS_HIGH);
    for (int i = 0; i < NUM_GENERATORS; i++)
        crs[ci++] = suspenders_spawn(generator, (void *)(intptr_t)i,
                                      SUSPENDERS_QOS_NORMAL);

    watchdog_cr = suspenders_spawn(watchdog, crs, SUSPENDERS_QOS_REALTIME);
    suspenders_go(closer, NULL);

    suspenders_run();

    long expected = 0;
    for (int g = 0; g < NUM_GENERATORS; g++)
        for (int i = 0; i < ITEMS_PER_GEN; i++)
            if ((g * ITEMS_PER_GEN + i) % 2 == 0)
                expected += g * ITEMS_PER_GEN + i;

    printf("\nGenerated: %d  Filtered: %ld  Sum: %ld (expected %ld)\n",
           NUM_GENERATORS * ITEMS_PER_GEN, atomic_load(&filtered_count),
           result, expected);
    printf("%s\n", result == expected ? "SUCCESS" : "FAILURE");

    suspenders_chan_destroy(ch_raw);
    suspenders_chan_destroy(ch_filtered);
    suspenders_shutdown();
    return result == expected ? 0 : 1;
}
