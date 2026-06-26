/* suspenders_bench.h — shared timing helpers for the benchmark suite.
 *
 * aarch64: the generic timer (cntvct_el0 / cntfrq_el0) gives a fixed-rate,
 * userspace-readable clock (~31 MHz on Tegra) — great for wall time, but it
 * is NOT a cycle counter. Cycle figures below are derived from wall time and
 * the CPU's max clock (sysfs), which is exact when the governor pins the max
 * frequency and conservative otherwise. Where `perf stat -e cycles` is
 * available it remains the gold standard:
 *
 *     perf stat -e cycles ./suspenders-bench-cycles yield 2000000
 */
#ifndef SUSPENDERS_BENCH_H
#define SUSPENDERS_BENCH_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if defined(__aarch64__)
static inline uint64_t sb_ticks(void) {
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
static inline uint64_t sb_tick_hz(void) {
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
}
static inline uint64_t sb_now_ns(void) {
    return (uint64_t)((__uint128_t)sb_ticks() * 1000000000ULL / sb_tick_hz());
}
#else
static inline uint64_t sb_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static inline uint64_t sb_ticks(void) { return sb_now_ns(); }
static inline uint64_t sb_tick_hz(void) { return 1000000000ULL; }
#endif

/* Max CPU clock in Hz from sysfs (Linux), 0 if unknown. */
static inline uint64_t sb_cpu_max_hz(void) {
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (!f) return 0;
    unsigned long long khz = 0;
    int ok = fscanf(f, "%llu", &khz);
    fclose(f);
    return ok == 1 ? (uint64_t)khz * 1000ULL : 0;
}

/* Print "<label>: X ns/op (~Y cycles @ Z GHz)" for a measured phase. */
static inline void sb_report(const char *label, uint64_t total_ns, uint64_t ops) {
    double ns_op = ops ? (double)total_ns / (double)ops : 0.0;
    uint64_t hz = sb_cpu_max_hz();
    if (hz) {
        printf("  %-24s %8.2f ns/op  (~%.0f cycles @ %.2f GHz max)\n",
               label, ns_op, ns_op * (double)hz / 1e9, (double)hz / 1e9);
    } else {
        printf("  %-24s %8.2f ns/op\n", label, ns_op);
    }
}

#endif /* SUSPENDERS_BENCH_H */
