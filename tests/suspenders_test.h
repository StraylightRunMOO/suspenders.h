/* suspenders_test.h - minimal zero-dependency test harness (C11)
 *
 * Usage:
 *   static int test_something(void) {
 *       ASSERT_EQ_INT(2, 1 + 1);
 *       return 0;
 *   }
 *   static const st_test_t tests[] = {
 *       ST_TEST(test_something),
 *   };
 *   int main(int argc, char **argv) {
 *       return st_main(argc, argv, tests, sizeof(tests)/sizeof(tests[0]));
 *   }
 *
 * Runner options:
 *   --list            print test names and exit
 *   --filter=SUBSTR   run only tests whose name contains SUBSTR
 *   --timeout=SECS    per-test watchdog (default 30, 0 disables)
 *
 * Environment:
 *   SUSPENDERS_TEST_WORKERS   worker count tests should pass to
 *                             suspenders_init (default 0)
 */
#ifndef SUSPENDERS_TEST_H
#define SUSPENDERS_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

typedef struct {
    const char *name;
    int (*fn)(void);
} st_test_t;

#define ST_TEST(fn) { #fn, fn }

/* ---- assertions (return nonzero from the test on failure) ---- */

#define ST_FAIL_LOC() fprintf(stderr, "\n    %s:%d: ", __FILE__, __LINE__)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_TRUE(%s) failed", #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if (cond) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_FALSE(%s) failed", #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    long long st_e = (long long)(expected), st_a = (long long)(actual); \
    if (st_e != st_a) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_EQ_INT(%s, %s): expected %lld, got %lld", \
                #expected, #actual, st_e, st_a); \
        return 1; \
    } \
} while (0)

#define ASSERT_STREQ(expected, actual) do { \
    const char *st_e = (expected), *st_a = (actual); \
    if (!st_e || !st_a || strcmp(st_e, st_a) != 0) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_STREQ(%s, %s): expected \"%s\", got \"%s\"", \
                #expected, #actual, st_e ? st_e : "(null)", st_a ? st_a : "(null)"); \
        return 1; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_NOT_NULL(%s) failed", #ptr); \
        return 1; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        ST_FAIL_LOC(); \
        fprintf(stderr, "ASSERT_NULL(%s) failed", #ptr); \
        return 1; \
    } \
} while (0)

/* ---- worker-count switch (multi-worker suites re-run via env) ---- */

static unsigned st_workers(void) {
    const char *env = getenv("SUSPENDERS_TEST_WORKERS");
    if (!env || !*env) return 0;
    long v = strtol(env, NULL, 10);
    return v > 0 ? (unsigned)v : 0;
}

/* ---- runner ---- */

static const char *st_current_test_name = NULL;

static void st_alarm_handler(int sig) {
    (void)sig;
    const char *name = st_current_test_name ? st_current_test_name : "?";
    /* async-signal-safe output */
    (void)!write(STDERR_FILENO, "\nTIMEOUT in test: ", 18);
    (void)!write(STDERR_FILENO, name, strlen(name));
    (void)!write(STDERR_FILENO, "\n", 1);
    _exit(124);
}

static int st_main(int argc, char **argv, const st_test_t *tests, size_t ntests) {
    const char *filter = NULL;
    unsigned timeout_s = 30;
    bool list_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
            timeout_s = (unsigned)strtoul(argv[i] + 10, NULL, 10);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            fprintf(stderr, "usage: %s [--list] [--filter=SUBSTR] [--timeout=SECS]\n", argv[0]);
            return 2;
        }
    }

    if (list_only) {
        for (size_t i = 0; i < ntests; i++) printf("%s\n", tests[i].name);
        return 0;
    }

    signal(SIGALRM, st_alarm_handler);

    int run = 0, passed = 0, failed = 0;
    for (size_t i = 0; i < ntests; i++) {
        if (filter && !strstr(tests[i].name, filter)) continue;
        printf("  Running %-40s ", tests[i].name);
        fflush(stdout);
        run++;
        st_current_test_name = tests[i].name;
        if (timeout_s) alarm(timeout_s);
        int rc = tests[i].fn();
        if (timeout_s) alarm(0);
        st_current_test_name = NULL;
        if (rc == 0) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("\nFAILED\n");
            failed++;
        }
    }

    printf("\n=== Test Summary ===\n");
    printf("  Tests run:    %d\n", run);
    printf("  Tests passed: %d\n", passed);
    printf("  Tests failed: %d\n", failed);
    if (st_workers() > 0) printf("  Workers:      %u\n", st_workers());

    return failed == 0 ? 0 : 1;
}

#endif /* SUSPENDERS_TEST_H */
