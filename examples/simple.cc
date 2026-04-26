#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include <cstdio>

int main() {
    suspenders::Context ctx;

    // suspenders::Channel<std::string_view> ch;  // Oops, won't compile (not trivially copyable)
    suspenders::Channel<uint64_t> ch;          // OK

    (void)suspenders::spawn([&ch] {
        (void)ch.send(42);
    });

    (void)suspenders::spawn([&ch] {
        uint64_t value = 0;
        if (ch.recv(value)) {
            std::printf("received %llu\n", (unsigned long long)value);
        }
    });

    ctx.run();
    return 0;
}
