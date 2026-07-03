#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include <stdio.h>
#include <stdatomic.h>

static _Atomic int counter = 0;
static _Atomic int after_fired = 0;

static void inc(void *arg) {
    (void)arg;
    int n = atomic_fetch_add(&counter, 1) + 1;
    printf("[queue] counter incremented to %d\n", n);
}

static void barrier_task(void *arg) {
    (void)arg;
    printf("[queue] barrier reached, counter=%d\n", atomic_load(&counter));
}

static void sync_task(void *arg) {
    *(int*)arg = atomic_load(&counter);
}

static void after_task(void *arg) {
    (void)arg;
    atomic_store(&after_fired, 1);
    printf("[queue] after() task fired\n");
}

static void coordinator(void *arg) {
    (void)arg;

    /* Serial queue: strict submission order, barrier == plain async. */
    suspenders_queue_t *serial = suspenders_queue_create("serial", SUSPENDERS_QOS_NORMAL, 1);
    if (!serial) return;
    for (int i = 0; i < 5; i++)
        suspenders_queue_async(serial, inc, NULL);
    suspenders_queue_barrier_async(serial, barrier_task, NULL);

    /* queue_sync: submit and wait for the result. */
    int seen = -1;
    suspenders_queue_sync(serial, sync_task, &seen);
    printf("[queue] sync observed counter=%d\n", seen);

    /* Concurrent queue (the shared global one) + a delayed task. */
    suspenders_queue_t *global = suspenders_get_global_queue(SUSPENDERS_QOS_NORMAL);
    suspenders_queue_after(global, 20 * 1000000ULL /* 20 ms */, after_task, NULL);
    for (int i = 0; i < 3; i++)
        suspenders_queue_async(global, inc, NULL);

    suspenders_queue_destroy(serial);   /* waits for the drain */
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    suspenders_spawn(coordinator, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();   /* returns once every task (incl. after) completed */
    suspenders_shutdown();
    printf("final counter=%d after_fired=%d\n",
           atomic_load(&counter), atomic_load(&after_fired));
    return (atomic_load(&counter) == 8 && atomic_load(&after_fired)) ? 0 : 1;
}
