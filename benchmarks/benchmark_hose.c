/* benchmark_hose.c - Hose (TCP) latency benchmark
 *
 * Measures round-trip latency of suspenders_hose_read/suspenders_hose_write over loopback TCP.
 * One server handler and one client exchange messages as fast as possible.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define SERVER_PORT  54322
#define ITERATIONS   10000
#define MSG_SIZE     64

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static volatile int server_ready = 0;
static volatile int client_done = 0;

void suspenders_hose_server_handler(void *arg) {
    suspenders_hose_t *client = (suspenders_hose_t*)arg;
    char buf[MSG_SIZE];

    while (1) {
        ssize_t n = suspenders_hose_read(client, buf, sizeof(buf));
        if (n <= 0) break;
        if (suspenders_hose_write(client, buf, n) <= 0) break;
    }

    suspenders_hose_close(client);
    memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
}

void suspenders_hose_server_listener(void *arg) {
    (void)arg;
    suspenders_hose_t listener;
    suspenders_hose_init(&listener, NULL);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", SERVER_PORT);
    if (!suspenders_hose_listen(&listener, uri)) {
        fprintf(stderr, "[bench] listen failed\n");
        return;
    }

    server_ready = 1;

    suspenders_hose_t *client = memento_thread_heap_alloc(memento_thread_heap_get(), sizeof(suspenders_hose_t));
    if (!client) { suspenders_hose_close(&listener); return; }

    if (!suspenders_hose_accept(&listener, client)) {
        memento_thread_heap_free(memento_thread_heap_get(), client, sizeof(suspenders_hose_t));
        suspenders_hose_close(&listener);
        return;
    }

    suspenders_spawn(suspenders_hose_server_handler, client, SUSPENDERS_QOS_NORMAL);
    suspenders_hose_close(&listener);
}

void suspenders_hose_client(void *arg) {
    (void)arg;
    while (!server_ready)
        suspenders_yield();

    suspenders_hose_t conn;
    struct buf b = {0};
    suspenders_hose_init(&conn, &b);

    char uri[64];
    snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%d", SERVER_PORT);
    if (!suspenders_hose_dial(&conn, uri)) {
        fprintf(stderr, "[bench] dial failed\n");
        return;
    }

    char send_buf[MSG_SIZE];
    char recv_buf[MSG_SIZE];
    memset(send_buf, 'x', sizeof(send_buf));

    for (int i = 0; i < ITERATIONS; i++) {
        if (suspenders_hose_write(&conn, send_buf, sizeof(send_buf)) <= 0) break;
        ssize_t n = suspenders_hose_read(&conn, recv_buf, sizeof(recv_buf));
        if (n <= 0) break;
    }

    suspenders_hose_close(&conn);
    client_done = 1;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("\n=== Hose (TCP) Latency Benchmark ===\n\n");
    printf("Iterations: %d\n", ITERATIONS);
    printf("Message:    %d bytes\n", MSG_SIZE);
    printf("Transport:  loopback TCP\n\n");

    suspenders_init(0, 256);
    suspenders_spawn(suspenders_hose_server_listener, NULL, SUSPENDERS_QOS_HIGH);
    suspenders_spawn(suspenders_hose_client, NULL, SUSPENDERS_QOS_NORMAL);

    uint64_t start = get_time_ns();
    suspenders_run();
    uint64_t end = get_time_ns();

    suspenders_shutdown();

    uint64_t total_ns = end - start;
    double ns_per_rtt = (double)total_ns / ITERATIONS;
    double rtt_per_sec = 1e9 / ns_per_rtt;

    printf("Results:\n");
    printf("  Total time:   %.3f ms\n", total_ns / 1e6);
    printf("  RTT:          %.2f ns\n", ns_per_rtt);
    printf("  RTTs/sec:     %.2f thousand\n", rtt_per_sec / 1000.0);
    printf("  Throughput:   %.2f MB/s\n",
           (double)(ITERATIONS * MSG_SIZE * 2) / (total_ns / 1e9) / (1024.0 * 1024.0));
    printf("\n");

    return 0;
}
