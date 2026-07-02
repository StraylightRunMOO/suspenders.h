#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include <cstdio>
#include <atomic>
#include <cstring>

static constexpr int EVENT_LOOP_PORT = 54321;
static std::atomic<int> ticks{0};

static void handle_client(suspenders::Hose client) {
    char buf[256];
    ssize_t n;
    while ((n = client.read(buf, sizeof(buf))) > 0) {
        if (client.write(buf, static_cast<size_t>(n)) < 0) break;
    }
}

int main() {
    suspenders::Context ctx;

    suspenders::spawn([&] {
        suspenders::Timer tick(250, true, [] {
            std::printf("[event_loop] tick %d\n", ++ticks);
        });

        suspenders::spawn([&] {
            suspenders::Hose listener;
            if (!listener.listen("tcp://127.0.0.1:" + std::to_string(EVENT_LOOP_PORT))) {
                std::fprintf(stderr, "[event_loop] listen failed\n");
                return;
            }
            std::printf("[event_loop] echo server listening on %d\n", EVENT_LOOP_PORT);

            while (ticks < 8) {
                suspenders::Hose client;
                if (listener.accept(client)) {
                    suspenders::spawn([c = std::move(client)]() mutable {
                        handle_client(std::move(c));
                    });
                }
            }
        }, suspenders::QoS::High);

        suspenders::sleep_ms(50);

        suspenders::spawn([] {
            suspenders::Hose conn;
            if (!conn.dial("tcp://127.0.0.1:" + std::to_string(EVENT_LOOP_PORT))) {
                std::fprintf(stderr, "[event_loop] dial failed\n");
                return;
            }
            const char *msg = "hello event loop";
            if (conn.write(msg, std::strlen(msg)) > 0) {
                char buf[64] = {0};
                ssize_t n = conn.read(buf, sizeof(buf) - 1);
                if (n > 0)
                    std::printf("[event_loop] client received: %.*s\n", static_cast<int>(n), buf);
            }
        });

        /* Let the demo run for a couple of seconds. */
        suspenders::sleep_ms(2200);
    });

    ctx.run();
    std::printf("[event_loop] done\n");
    return 0;
}
