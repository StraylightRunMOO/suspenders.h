/* benchmark_channels.c - Channel throughput benchmark
 *
 * Measures round-trip latency and throughput of unbuffered channel
 * operations between pairs of coroutines.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define NUM_PAIRS   4
#define ITERATIONS  100000

static suspenders_chan_t *channels[NUM_PAIRS];
static volatile uint64_t messages_sent = 0;

void sender(void *arg) {
    int id = (int)(intptr_t)arg;
    suspenders_chan_t *ch = channels[id];
    int val = id;
    for (int i = 0; i < ITERATIONS; i++) {
        suspenders_chan_send(ch, &val);
        __atomic_fetch_add(&messages_sent, 1, __ATOMIC_RELAXED);
    }
}

void receiver(void *arg) {
    int id = (int)(intptr_t)arg;
    suspenders_chan_t *ch = channels[id];
    int val = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        suspenders_chan_recv(ch, &val);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("\n=== Channel Throughput Benchmark ===\n\n");
    printf("Pairs:      %d\n", NUM_PAIRS);
    printf("Iterations: %d per pair\n", ITERATIONS);
    printf("Total ops:  %d send/recv pairs\n\n", NUM_PAIRS * ITERATIONS);

    suspenders_init(0, 256);

    for (int i = 0; i < NUM_PAIRS; i++) {
        channels[i] = suspenders_chan_create(sizeof(int), 0);
        suspenders_spawn(sender, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);
        suspenders_spawn(receiver, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);
    }

    uint64_t start = get_time_ns();
    suspenders_run();
    uint64_t end = get_time_ns();

    suspenders_shutdown();

    uint64_t total_ns = end - start;
    uint64_t total_pairs = (uint64_t)NUM_PAIRS * ITERATIONS;
    double ns_per_op = (double)total_ns / total_pairs;
    double ops_per_sec = 1e9 / ns_per_op;

    printf("Results:\n");
    printf("  Total time:   %.3f ms\n", total_ns / 1e6);
    printf("  Time/op:      %.2f ns\n", ns_per_op);
    printf("  Ops/sec:      %.2f million\n", ops_per_sec / 1e6);
    printf("  Messages:     %lu\n", (unsigned long)messages_sent);
    printf("\n");

    return 0;
}
