/* sync_primitives.c - Concurrent data structure with mutex, rwlock, cond
 *
 * Demonstrates: suspenders_mutex, suspenders_rwlock, suspenders_cond,
 * cleanup handlers, trylock, deadline locks, priority boost.
 *
 * A shared "bank" has N accounts protected by a rwlock. Transfer coroutines
 * acquire write locks to move money between accounts. Auditor coroutines
 * acquire read locks to verify the total is conserved. A condition variable
 * signals when any transfer exceeds a threshold. A watchdog with a deadline
 * lock demonstrates timed-out lock acquisition.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define NUM_ACCOUNTS    8
#define INITIAL_BALANCE 1000
#define NUM_TRANSFERS   2000
#define NUM_AUDITORS    2
#define LARGE_THRESHOLD 200

static int accounts[NUM_ACCOUNTS];
static suspenders_rwlock_t bank_lock;
static suspenders_mutex_t alert_mtx;
static suspenders_cond_t  alert_cond;
static int large_transfer_count = 0;
static _Atomic int transfers_done = 0;
static _Atomic int audits_done = 0;
static _Atomic int alerts_received = 0;
static volatile int running = 1;

/* Cleanup handler for write lock — ensures unlock on cancellation */
static void unlock_rwlock_write(void *arg) {
    suspenders_rwlock_unlock((suspenders_rwlock_t *)arg);
}

static void transfer(void *arg) {
    (void)arg;
    suspenders_setname("transfer");

    for (int i = 0; i < NUM_TRANSFERS; i++) {
        int from = rand() % NUM_ACCOUNTS;
        int to   = (from + 1 + rand() % (NUM_ACCOUNTS - 1)) % NUM_ACCOUNTS;
        int amt  = 1 + rand() % 100;

        suspenders_rwlock_wrlock(&bank_lock);

        /* Push a cleanup handler so the lock is released if we're canceled */
        suspenders_cleanup_t cleanup;
        suspenders_cleanup_push(&cleanup, unlock_rwlock_write, &bank_lock);

        if (accounts[from] >= amt) {
            accounts[from] -= amt;
            accounts[to]   += amt;

            if (amt >= LARGE_THRESHOLD) {
                suspenders_mutex_lock(&alert_mtx);
                large_transfer_count++;
                suspenders_cond_signal(&alert_cond);
                suspenders_mutex_unlock(&alert_mtx);
            }
        }

        suspenders_cleanup_pop(0);  /* don't execute — we unlock manually */
        suspenders_rwlock_unlock(&bank_lock);

        atomic_fetch_add(&transfers_done, 1);

        if (i % 500 == 0) suspenders_yield();
    }
}

static void auditor(void *arg) {
    int id = (int)(intptr_t)arg;
    suspenders_setname("auditor");

    int expected_total = NUM_ACCOUNTS * INITIAL_BALANCE;

    while (running) {
        /* Try a read lock first — if busy, yield and retry */
        int rc = suspenders_rwlock_tryrdlock(&bank_lock);
        if (rc == SUSPENDERS_BUSY) {
            suspenders_yield();
            suspenders_rwlock_rdlock(&bank_lock);
        }

        int total = 0;
        for (int i = 0; i < NUM_ACCOUNTS; i++)
            total += accounts[i];

        suspenders_rwlock_unlock(&bank_lock);

        if (total != expected_total) {
            printf("[auditor %d] INVARIANT VIOLATION: total=%d expected=%d\n",
                   id, total, expected_total);
        }

        atomic_fetch_add(&audits_done, 1);
        suspenders_sleep_ns(10ULL * 1000000);
    }
    printf("[auditor %d] performed %d audits\n", id,
           atomic_load(&audits_done));
}

static void alert_monitor(void *arg) {
    (void)arg;
    suspenders_setname("alert-monitor");

    while (running) {
        suspenders_mutex_lock(&alert_mtx);

        /* Wait with a deadline so we can check the running flag */
        int rc = suspenders_cond_wait_dl(&alert_cond, &alert_mtx,
                     suspenders_now_ns() + 500ULL * 1000000);

        if (rc == SUSPENDERS_OK) {
            printf("[alert] large transfer #%d detected\n",
                   large_transfer_count);
            atomic_fetch_add(&alerts_received, 1);
        }

        suspenders_mutex_unlock(&alert_mtx);
    }
}

/* Demonstrate deadline lock: try to acquire write lock with a short timeout */
static void deadline_demo(void *arg) {
    (void)arg;
    suspenders_setname("deadline-demo");

    /* Give the transfers a head start so the lock is likely contended */
    suspenders_sleep_ns(50ULL * 1000000);

    uint64_t deadline = suspenders_now_ns() + 1ULL * 1000000; /* 1 ms */
    int rc = suspenders_rwlock_wrlock_dl(&bank_lock, deadline);
    if (rc == SUSPENDERS_TIMEDOUT) {
        printf("[deadline] wrlock timed out after 1ms (expected under contention)\n");
    } else if (rc == SUSPENDERS_OK) {
        printf("[deadline] wrlock acquired within 1ms\n");
        suspenders_rwlock_unlock(&bank_lock);
    }
}

static void coordinator(void *arg) {
    (void)arg;

    /* Spawn everything */
    suspenders_cr_t *t1 = suspenders_spawn(transfer, NULL, SUSPENDERS_QOS_NORMAL);
    suspenders_cr_t *t2 = suspenders_spawn(transfer, NULL, SUSPENDERS_QOS_NORMAL);

    /* Boost one transfer to HIGH to demonstrate priority inheritance */
    suspenders_boost(t1, SUSPENDERS_QOS_HIGH);

    for (int i = 0; i < NUM_AUDITORS; i++)
        suspenders_spawn(auditor, (void *)(intptr_t)i, SUSPENDERS_QOS_LOW);

    suspenders_go(alert_monitor, NULL);
    suspenders_go(deadline_demo, NULL);

    /* Wait for transfers to complete */
    while (atomic_load(&transfers_done) < 2 * NUM_TRANSFERS)
        suspenders_sleep_ns(50ULL * 1000000);

    running = 0;
    /* Wake the alert monitor so it can exit */
    suspenders_mutex_lock(&alert_mtx);
    suspenders_cond_broadcast(&alert_cond);
    suspenders_mutex_unlock(&alert_mtx);

    /* Let auditors finish their last cycle */
    suspenders_sleep_ns(100ULL * 1000000);

    (void)t2;
}

int main(void) {
    printf("=== Synchronization Primitives Demo ===\n\n");
    srand(42);

    suspenders_init(0, 256);

    suspenders_rwlock_init(&bank_lock);
    suspenders_mutex_init(&alert_mtx);
    suspenders_cond_init(&alert_cond);

    for (int i = 0; i < NUM_ACCOUNTS; i++)
        accounts[i] = INITIAL_BALANCE;

    suspenders_go(coordinator, NULL);
    suspenders_run();

    /* Final audit */
    int total = 0;
    for (int i = 0; i < NUM_ACCOUNTS; i++)
        total += accounts[i];

    int expected = NUM_ACCOUNTS * INITIAL_BALANCE;
    printf("\n=== Results ===\n");
    printf("Transfers: %d  Audits: %d  Alerts: %d\n",
           atomic_load(&transfers_done), atomic_load(&audits_done),
           atomic_load(&alerts_received));
    printf("Final total: %d (expected %d)\n", total, expected);
    printf("%s\n", total == expected ? "SUCCESS" : "FAILURE");

    suspenders_shutdown();
    return total == expected ? 0 : 1;
}
