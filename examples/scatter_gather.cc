/* scatter_gather.cc - Parallel scatter/gather with ranked results
 *
 * Demonstrates: Channel<T>, Queue (barrier_async), Mutex, select,
 * deadline recv, spawn with lambdas, sleep_for with chrono, CleanupGuard.
 *
 * Simulates querying N "backends" in parallel with a deadline. Each
 * backend has a different latency; we gather as many replies as arrive
 * before the deadline, rank them by score, and use a serial queue to
 * print ordered results.
 */

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>

using namespace std::chrono_literals;

static constexpr int NUM_BACKENDS  = 6;
static constexpr int NUM_QUERIES   = 10;
static constexpr int QUERY_DEADLINE_MS = 100;

struct Query {
    int id;
    char text[64];
};

struct Reply {
    int query_id;
    int backend_id;
    int score;
    int latency_ms;
};

static const int backend_latency[NUM_BACKENDS] = {
    5, 10, 20, 40, 150, 300
};

int main() {
    std::printf("=== Scatter/Gather with Ranked Results ===\n\n");
    std::srand(42);
    suspenders::Context ctx;

    std::atomic<int> total_replies{0};
    std::atomic<int> queries_completed{0};

    suspenders::spawn([&] {
        suspenders::Queue report_q("reports", suspenders::QoS::Normal);

        for (int q = 0; q < NUM_QUERIES; q++) {
            Query query = { q, {} };
            std::snprintf(query.text, sizeof(query.text), "query-%d", q);

            auto reply_ch = std::make_shared<suspenders::Channel<Reply>>(NUM_BACKENDS);

            /* Scatter: fire off all backends in parallel */
            for (int b = 0; b < NUM_BACKENDS; b++) {
                suspenders::spawn([b, query, reply_ch] {
                    int lat = backend_latency[b] + (std::rand() % 20);
                    suspenders::sleep_ms(lat);

                    Reply r = {
                        query.id, b,
                        100 - lat + (std::rand() % 30),
                        lat
                    };
                    (void)reply_ch->try_send(r);
                });
            }

            /* Gather: collect replies until deadline */
            suspenders::spawn([q, reply_ch, &total_replies, &queries_completed,
                               &report_q] {
                std::array<Reply, NUM_BACKENDS> results{};
                int count = 0;

                uint64_t deadline = suspenders::now_ns() +
                                    QUERY_DEADLINE_MS * 1'000'000ULL;

                while (count < NUM_BACKENDS) {
                    Reply r;
                    int rc = reply_ch->recv_dl(r, deadline);
                    if (rc == SUSPENDERS_TIMEDOUT || rc != SUSPENDERS_OK) break;
                    results[count++] = r;
                    total_replies.fetch_add(1);
                }

                std::sort(results.begin(), results.begin() + count,
                          [](const Reply& a, const Reply& b) {
                              return a.score > b.score;
                          });

                (void)report_q.async([q, results, count] {
                    std::printf("[query %2d] %d/%d replied: ",
                                q, count, NUM_BACKENDS);
                    for (int i = 0; i < count && i < 3; i++)
                        std::printf("b%d(%dms,s%d) ", results[i].backend_id,
                                    results[i].latency_ms, results[i].score);
                    if (count > 3) std::printf("...");
                    std::printf("\n");
                });

                queries_completed.fetch_add(1);
            }, suspenders::QoS::High);

            suspenders::sleep_ms(20);
        }

        /* Wait for all gatherers to finish */
        while (queries_completed.load() < NUM_QUERIES)
            suspenders::sleep_ms(50);

        /* Barrier: print summary after all reports drain */
        (void)report_q.barrier_async([&] {
            std::printf("\n=== Results ===\n");
            std::printf("Queries: %d  Total replies: %d (max possible: %d)\n",
                        NUM_QUERIES, total_replies.load(),
                        NUM_QUERIES * NUM_BACKENDS);
            std::printf("Per query: %.1f/%d backends within %dms\n",
                        static_cast<double>(total_replies.load()) / NUM_QUERIES,
                        NUM_BACKENDS, QUERY_DEADLINE_MS);
        });

        /* Let the queue drain */
        suspenders::sleep_ms(200);
    });

    ctx.run();

    bool ok = queries_completed.load() == NUM_QUERIES;
    std::printf("%s\n", ok ? "SUCCESS" : "FAILURE");
    return ok ? 0 : 1;
}
