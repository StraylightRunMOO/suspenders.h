#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.hpp"
#include "suspenders.h"

int main() {
    suspenders::Context ctx;
    
    // suspenders::Channel<std::string_view> ch;  // Oops, won't compile (not trivially copyable)
    suspenders::Channel<uint64_t> ch;          // OK
    
    auto task = suspenders::spawn([&ch]{ 
        ch.send(42); 
    });
    
    ctx.run();
}
