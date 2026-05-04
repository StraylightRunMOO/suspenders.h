/* channel_demo.c - Demonstrates Suspenders channels for coroutine communication
 *
 * Three producers send integers through a shared channel; one consumer sums them.
 * The channel is unbuffered (rendezvous), so every send synchronizes with a recv.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define NUM_PRODUCERS 3
#define ITEMS_PER_PRODUCER 1000

static suspenders_chan_t *channel = NULL;
static volatile long total_sent = 0;
static volatile long total_received = 0;

void producer(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int val = id * 1000000 + i;
        if (!suspenders_chan_send(channel, &val)) {
            fprintf(stderr, "Producer %d: send failed at item %d\n", id, i);
            return;
        }
        __atomic_fetch_add(&total_sent, 1, __ATOMIC_RELAXED);
    }
}

void consumer(void *arg) {
    (void)arg;
    long sum = 0;
    int expected = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    int count = 0;

    for (int i = 0; i < expected; i++) {
        int val = 0;
        if (!suspenders_chan_recv(channel, &val)) {
            fprintf(stderr, "Consumer: recv failed at item %d\n", i);
            break;
        }
        sum += val;
        count++;
        __atomic_fetch_add(&total_received, 1, __ATOMIC_RELAXED);
    }

    printf("Consumer: received %d items, sum = %ld\n", count, sum);
}

int main(void) {
    printf("=== Suspenders Channel Demo ===\n\n");
    printf("Spawning %d producers, each sending %d integers...\n",
           NUM_PRODUCERS, ITEMS_PER_PRODUCER);

    suspenders_init(0, 256);

    channel = suspenders_chan_create(sizeof(int), 0);  /* Unbuffered rendezvous */
    if (!channel) {
        fprintf(stderr, "Failed to create channel\n");
        return 1;
    }

    suspenders_spawn(consumer, NULL, SUSPENDERS_QOS_NORMAL);
    for (int i = 0; i < NUM_PRODUCERS; i++)
        suspenders_spawn(producer, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);

    suspenders_run();
    suspenders_shutdown();

    printf("\n=== Results ===\n");
    printf("Total sent:     %ld\n", total_sent);
    printf("Total received: %ld\n", total_received);
    printf("Expected:       %d\n", NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    printf("\n%s\n", (total_sent == total_received &&
                      total_received == NUM_PRODUCERS * ITEMS_PER_PRODUCER)
                     ? "SUCCESS" : "FAILURE");

    return (total_sent == total_received &&
            total_received == NUM_PRODUCERS * ITEMS_PER_PRODUCER) ? 0 : 1;
}
