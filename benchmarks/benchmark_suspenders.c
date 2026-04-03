#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static volatile uint64_t switch_count = 0;

void ping_pong_cr(void *arg) {
    volatile uint64_t *count = (volatile uint64_t*)arg;
    for (int i = 0; i < 500000; i++) {
        (*count)++;
        suspenders_yield();
    }
}

int main(int argc, char *argv[]) {
    printf("\n=== Context Switch Benchmark ===\n\n");
    
    int iterations = 1000000;
    if (argc > 1) iterations = atoi(argv[1]);
    
    switch_count = 0;
    
    /* Initialize suspenders */
    suspenders_init(0, 256);
    
    /* Create two coroutines that ping-pong */
    suspenders_spawn(ping_pong_cr, (void*)&switch_count, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(ping_pong_cr, (void*)&switch_count, SUSPENDERS_QOS_NORMAL);
    
    printf("Running %d context switches...\n", iterations);
    
    uint64_t start = get_time_ns();
    
    /* Run iterations - each iteration is one context switch */
    for (int i = 0; i < iterations; i++) {
        if (switch_count >= (uint64_t)iterations) break;
        suspenders_run();
    }
    
    uint64_t end = get_time_ns();
    
    suspenders_shutdown();
    
    uint64_t total_ns = end - start;
    double ns_per_switch = (double)total_ns / iterations;
    double switches_per_sec = 1000000000.0 / ns_per_switch;
    
    printf("\nResults:\n");
    printf("  Total time:      %.3f ms\n", total_ns / 1000000.0);
    printf("  Time/switch:     %.2f ns\n", ns_per_switch);
    printf("  Switches/sec:    %.2f million\n", switches_per_sec / 1000000.0);
    printf("  Total switches:  %lu\n", (unsigned long)switch_count);
    printf("\n");
    
    return 0;
}
