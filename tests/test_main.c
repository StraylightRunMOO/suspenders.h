#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"

#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(test_func) do { \
    printf("  Running %s... ", #test_func); \
    tests_run++; \
    if (test_func() == 0) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
        tests_failed++; \
    } \
} while(0)

static volatile int test_counter = 0;
static volatile int count1 = 0, count2 = 0, count3 = 0;

void test_basic_cr(void *arg) {
    volatile int *c = (volatile int*)arg;
    (*c)++;
    suspenders_yield();
    (*c)++;
}

static int test_basic_spawn(void) {
    test_counter = 0;
    
    suspenders_init(0, 256);
    suspenders_spawn(test_basic_cr, (void*)&test_counter, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    
    return (test_counter == 2) ? 0 : 1;
}

void counter_cr(void *arg) {
    volatile int *c = (volatile int*)arg;
    for (int i = 0; i < 5; i++) {
        (*c)++;
        suspenders_yield();
    }
}

static int test_multiple_coroutines(void) {
    count1 = 0; count2 = 0; count3 = 0;
    
    suspenders_init(0, 256);
    suspenders_spawn(counter_cr, (void*)&count1, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(counter_cr, (void*)&count2, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(counter_cr, (void*)&count3, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    
    return (count1 == 5 && count2 == 5 && count3 == 5) ? 0 : 1;
}

int main(void) {
    printf("\n=== Coroutine Tests ===\n\n");
    
    RUN_TEST(test_basic_spawn);
    RUN_TEST(test_multiple_coroutines);
    
    printf("\n=== Test Summary ===\n");
    printf("  Tests run:    %d\n", tests_run);
    printf("  Tests passed: %d\n", tests_passed);
    printf("  Tests failed: %d\n", tests_failed);
    printf("\n");
    
    return tests_failed > 0 ? 1 : 0;
}
