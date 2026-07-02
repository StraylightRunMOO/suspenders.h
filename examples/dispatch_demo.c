#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include <stdio.h>

static int counter = 0;

static void inc(void *arg) {
    (void)arg;
    counter++;
    printf("[dispatch] counter incremented to %d\n", counter);
}

static void barrier_task(void *arg) {
    (void)arg;
    printf("[dispatch] barrier reached, counter=%d\n", counter);
}

static void coordinator(void *arg) {
    (void)arg;
    suspenders_dispatch_queue_t *q = suspenders_dispatch_queue_create("serial", SUSPENDERS_QOS_NORMAL);
    if (!q) return;

    for (int i = 0; i < 5; i++)
        suspenders_dispatch_async(q, inc, NULL);

    /* On a serial queue the barrier simply runs after the preceding tasks. */
    suspenders_dispatch_barrier_async(q, barrier_task, NULL);

    suspenders_sleep_ns(100000000); /* 100 ms */
    suspenders_dispatch_queue_destroy(q);
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    suspenders_spawn(coordinator, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    printf("final counter=%d\n", counter);
    return (counter == 5) ? 0 : 1;
}
