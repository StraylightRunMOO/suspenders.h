#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

/* Test coroutines */
static int counter1 = 0;
static int counter2 = 0;

void worker1(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        counter1++;
        printf("Worker 1: count = %d\n", counter1);
        suspenders_yield();
    }
}

void worker2(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        counter2++;
        printf("Worker 2: count = %d\n", counter2);
        suspenders_yield();
    }
}

int main(void) {
    printf("Suspenders coroutine demo starting...\n\n");
    
    /* Initialize Suspenders */
    suspenders_init(0, 256);
    
    /* Spawn coroutines */
    suspenders_spawn(worker1, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(worker2, NULL, SUSPENDERS_QOS_NORMAL);
    
    printf("Running coroutines...\n\n");
    
    /* Run the scheduler */
    suspenders_run();
    
    /* Cleanup */
    suspenders_shutdown();
    
    printf("\nResults: counter1=%d, counter2=%d\n", counter1, counter2);
    
    if (counter1 == 5 && counter2 == 5) {
        printf("SUCCESS!\n");
        return 0;
    } else {
        printf("FAILURE!\n");
        return 1;
    }
}
