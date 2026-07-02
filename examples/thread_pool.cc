#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include <cstdio>
#include <atomic>

static std::atomic<int> processed{0};

int main() {
    suspenders::Context ctx;

    suspenders::spawn([&] {
        suspenders::Pool pool(4, suspenders::QoS::Normal);
        for (int i = 0; i < 10; i++) {
            pool.submit([i] {
                std::printf("[pool] processing task %d on coroutine\n", i);
                processed.fetch_add(1);
            });
        }
        suspenders::sleep_ms(100);
        /* pool goes out of scope and shuts down workers. */
    });

    ctx.run();
    std::printf("processed %d tasks\n", processed.load());
    return (processed == 10) ? 0 : 1;
}
