/* benchmark_cycles.c — minimal measured phases for cycle accounting.
 *
 * Each mode runs exactly N operations of one primitive between two time
 * stamps (setup excluded), so an external `perf stat -e cycles` divided by
 * N approximates cycles/op. Without perf, the built-in report derives
 * cycles from wall time and the max CPU clock (see suspenders_bench.h).
 *
 *   suspenders-bench-cycles [yield|chan|queue|all] [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"
#include "suspenders_bench.h"

static long g_n = 1000000;

/* --- yield: two coroutines ping-ponging through the scheduler ----------- */
static uint64_t yield_ns;

static void yield_cr(void *arg) {
    long n = (long)(intptr_t)arg;
    for (long i = 0; i < n; i++) suspenders_yield();
}

static void yield_timer_cr(void *arg) {
    long n = (long)(intptr_t)arg;
    uint64_t t0 = sb_now_ns();
    for (long i = 0; i < n; i++) suspenders_yield();
    yield_ns = sb_now_ns() - t0;
}

static void bench_yield(void) {
    suspenders_init(1, 256);
    suspenders_spawn(yield_timer_cr, (void*)(intptr_t)g_n, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(yield_cr, (void*)(intptr_t)g_n, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    /* each loop iteration is one yield -> scheduler -> other cr -> back:
     * two context switches; report per switch. */
    sb_report("yield (per switch)", yield_ns, (uint64_t)g_n * 2);
}

/* --- chan: rendezvous send/recv pair between two coroutines ------------- */
static suspenders_chan_t *bc_ch;
static uint64_t chan_ns;

static void chan_tx_cr(void *arg) {
    long n = (long)(intptr_t)arg;
    for (long i = 0; i < n; i++) {
        long v = i;
        suspenders_chan_send(bc_ch, &v);
    }
}

static void chan_rx_cr(void *arg) {
    long n = (long)(intptr_t)arg;
    long v;
    uint64_t t0 = sb_now_ns();
    for (long i = 0; i < n; i++) suspenders_chan_recv(bc_ch, &v);
    chan_ns = sb_now_ns() - t0;
}

static void bench_chan(void) {
    suspenders_init(1, 256);
    bc_ch = suspenders_chan_create(sizeof(long), 0);
    if (!bc_ch) return;
    suspenders_spawn(chan_rx_cr, (void*)(intptr_t)g_n, SUSPENDERS_QOS_NORMAL);
    suspenders_spawn(chan_tx_cr, (void*)(intptr_t)g_n, SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_chan_destroy(bc_ch);
    bc_ch = NULL;
    suspenders_shutdown();
    /* one send+recv rendezvous per iteration; report per op (2 ops/pair) */
    sb_report("chan rendezvous (per op)", chan_ns, (uint64_t)g_n * 2);
}

/* --- queue: async submit -> serial drainer dispatch --------------------- */
static uint64_t queue_ns;

static void queue_noop_task(void *arg) { (void)arg; }

static void queue_bench_cr(void *arg) {
    long n = (long)(intptr_t)arg;
    suspenders_queue_t *q = suspenders_queue_create("bench", SUSPENDERS_QOS_NORMAL, 1);
    if (!q) return;
    uint64_t t0 = sb_now_ns();
    for (long i = 0; i < n; i++)
        suspenders_queue_async(q, queue_noop_task, NULL);
    suspenders_queue_destroy(q);   /* waits for the drain */
    queue_ns = sb_now_ns() - t0;
}

static void bench_queue(void) {
    suspenders_init(1, 256);
    suspenders_spawn(queue_bench_cr, (void*)(intptr_t)(g_n / 10), SUSPENDERS_QOS_NORMAL);
    suspenders_run();
    suspenders_shutdown();
    sb_report("queue async+dispatch", queue_ns, (uint64_t)(g_n / 10));
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "all";
    if (argc > 2) g_n = atol(argv[2]);
    if (g_n <= 0) g_n = 1000000;

    printf("\n=== Cycle Accounting Benchmark (N=%ld) ===\n\n", g_n);
    if (!strcmp(mode, "yield") || !strcmp(mode, "all")) bench_yield();
    if (!strcmp(mode, "chan")  || !strcmp(mode, "all")) bench_chan();
    if (!strcmp(mode, "queue") || !strcmp(mode, "all")) bench_queue();
    printf("\nTargets: context switch < 150 cycles, chan op < 80 cycles.\n"
           "Use `perf stat -e cycles %s <mode> <N>` for exact counts.\n\n",
           argv[0]);
    return 0;
}
