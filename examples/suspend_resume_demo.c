/* suspend_resume_demo.c - Demonstrates explicit suspenders_suspend/resume
 *
 * A controller coroutine spawns worker coroutines, suspends them at will,
 * and resumes them in a specific order to show manual scheduling control.
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

#define NUM_WORKERS 4
#define WORK_STEPS  5

static suspenders_cr_t *workers[NUM_WORKERS];
static volatile int step_counts[NUM_WORKERS];
static volatile int controller_done = 0;

void worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < WORK_STEPS; i++) {
        step_counts[id]++;
        printf("  Worker %d: step %d/%d\n", id, i + 1, WORK_STEPS);
        suspenders_suspend();  /* Wait for controller to resume us */
    }
    printf("  Worker %d: finished\n", id);
}

void controller(void *arg) {
    (void)arg;
    printf("Controller: spawning %d workers\n", NUM_WORKERS);

    for (int i = 0; i < NUM_WORKERS; i++)
        workers[i] = suspenders_spawn(worker, (void*)(intptr_t)i, SUSPENDERS_QOS_NORMAL);

    /* Let the workers run their first step and suspend. The controller runs
     * at LOW QoS so a yield always lets every worker go first. */
    suspenders_yield();

    /* Round-robin resume each worker for each remaining step */
    for (int step = 0; step < WORK_STEPS; step++) {
        printf("Controller: round %d\n", step + 1);
        for (int i = 0; i < NUM_WORKERS; i++) {
            suspenders_resume(workers[i]);
        }
        suspenders_yield();  /* let the resumed workers run this step */
    }

    printf("Controller: all workers should be done\n");
    controller_done = 1;
}

int main(void) {
    printf("=== Suspenders Suspend/Resume Demo ===\n\n");

    suspenders_init(0, 256);
    suspenders_spawn(controller, NULL, SUSPENDERS_QOS_LOW);

    suspenders_run();
    suspenders_shutdown();

    printf("\n=== Results ===\n");
    int ok = 1;
    for (int i = 0; i < NUM_WORKERS; i++) {
        printf("Worker %d completed %d/%d steps\n", i, step_counts[i], WORK_STEPS);
        if (step_counts[i] != WORK_STEPS) ok = 0;
    }

    printf("Controller finished: %s\n", controller_done ? "yes" : "no");
    printf("%s\n", ok ? "SUCCESS" : "FAILURE");

    return ok ? 0 : 1;
}
