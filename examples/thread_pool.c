#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include <stdio.h>
#include <stdint.h>

static int processed = 0;

static void worker_task(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("[pool] processing task %d on coroutine\n", id);
    processed++;
}

static void coordinator(void *arg) {
    (void)arg;
    suspenders_pool_t *pool = suspenders_pool_create(4, SUSPENDERS_QOS_NORMAL);
    if (!pool) return;

    for (int i = 0; i < 10; i++)
        suspenders_pool_submit(pool, worker_task, (void*)(intptr_t)i);

    /* Wait for tasks to drain then shut the pool down so suspenders_run() can end. */
    suspenders_sleep_ns(100000000); /* 100 ms */
    suspenders_pool_destroy(pool);
}

int main(void) {
    memento_init();
    suspenders_init(0, 256);
    suspenders_spawn(coordinator, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    printf("processed %d tasks\n", processed);
    return (processed == 10) ? 0 : 1;
}
