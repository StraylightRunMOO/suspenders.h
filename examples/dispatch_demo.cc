#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include <cstdio>
#include <atomic>

static std::atomic<int> counter{0};

int main() {
    suspenders::Context ctx;

    suspenders::spawn([&] {
        suspenders::DispatchQueue queue("serial", suspenders::QoS::Normal);
        for (int i = 0; i < 5; i++) {
            queue.async([] {
                int n = counter.fetch_add(1) + 1;
                std::printf("[dispatch] counter incremented to %d\n", n);
            });
        }
        queue.barrier_async([] {
            std::printf("[dispatch] barrier reached, counter=%d\n", counter.load());
        });
        suspenders::sleep_ms(100);
    });

    ctx.run();
    std::printf("final counter=%d\n", counter.load());
    return (counter == 5) ? 0 : 1;
}
