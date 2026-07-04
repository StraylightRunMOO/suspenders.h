/* pipeline.cc - Fan-out/fan-in pipeline using C++ API
 *
 * Demonstrates: Channel<T>, WaitGroup, select, spawn with lambdas,
 * QoS, sleep_for with chrono literals, cancellation, CleanupGuard.
 *
 * A source produces work items. N workers process them in parallel and
 * send results through individual channels. A collector uses select to
 * gather results from all workers as they arrive.
 */

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

#include <cstdio>
#include <atomic>
#include <vector>
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

static constexpr int NUM_ITEMS   = 200;
static constexpr int NUM_WORKERS = 4;

struct WorkItem {
    int id;
    int value;
};

struct Result {
    int id;
    double value;
};

int main() {
    suspenders::Context ctx;

    suspenders::Channel<WorkItem> work_ch(16);
    std::vector<suspenders::Channel<Result>> result_chs;
    for (int i = 0; i < NUM_WORKERS; i++)
        result_chs.emplace_back(8);

    suspenders::WaitGroup worker_wg;
    worker_wg.add(NUM_WORKERS);

    std::atomic<int> items_processed{0};
    double final_sum = 0.0;

    /* Source: produces work items */
    suspenders::spawn([&work_ch] {
        for (int i = 0; i < NUM_ITEMS; i++) {
            WorkItem item = { i, i * 7 + 3 };
            if (!work_ch.send(item)) break;
        }
        work_ch.close();
    });

    /* Workers: process items, send results to their own channel */
    for (int w = 0; w < NUM_WORKERS; w++) {
        suspenders::spawn([w, &work_ch, &result_chs, &worker_wg,
                           &items_processed] {

            /* CleanupGuard ensures waitgroup.done() even on cancellation */
            suspenders::CleanupGuard guard([&worker_wg] {
                worker_wg.done();
            });

            WorkItem item;
            while (work_ch.recv(item)) {
                Result r = {
                    item.id,
                    std::sqrt(static_cast<double>(item.value))
                };
                (void)result_chs[w].send(r);
                items_processed.fetch_add(1);
            }

            result_chs[w].close();
            guard.release();   /* disarm — we'll call done() ourselves */
            worker_wg.done();
        }, suspenders::QoS::High);
    }

    /* Collector: select across all worker result channels */
    suspenders::spawn([&result_chs, &final_sum] {
        int collected = 0;
        int channels_open = NUM_WORKERS;

        while (channels_open > 0) {
            Result results[NUM_WORKERS];
            suspenders_chan_op_t ops[NUM_WORKERS];
            for (int i = 0; i < NUM_WORKERS; i++)
                ops[i] = result_chs[i].recv_op(results[i]);

            int idx = suspenders::select(
                { ops[0], ops[1], ops[2], ops[3] },
                suspenders::now_ns() + 500'000'000ULL);

            if (idx == SUSPENDERS_TIMEDOUT) continue;
            if (idx < 0) break;

            if (suspenders_errno == SUSPENDERS_CLOSED) {
                channels_open--;
                continue;
            }

            final_sum += results[idx].value;
            collected++;

            if (collected % 50 == 0)
                std::printf("[collector] %d results gathered\n", collected);
        }

        std::printf("[collector] total: %d results, sum=%.2f\n",
                    collected, final_sum);
    });

    ctx.run();

    /* Verify */
    double expected = 0.0;
    for (int i = 0; i < NUM_ITEMS; i++)
        expected += std::sqrt(static_cast<double>(i * 7 + 3));

    std::printf("\nProcessed: %d  Sum: %.2f (expected %.2f)\n",
                items_processed.load(), final_sum, expected);

    double diff = std::fabs(final_sum - expected);
    std::printf("%s\n", diff < 0.01 ? "SUCCESS" : "FAILURE");
    return diff < 0.01 ? 0 : 1;
}
