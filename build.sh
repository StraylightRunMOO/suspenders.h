#!/bin/bash

# Build script for libsuspenders

set -e

echo "========================================"
echo "  libsuspenders Build Script"
echo "========================================"

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building..."
make -j$(nproc)

echo ""
echo "========================================"
echo "  Build Complete!"
echo "========================================"
echo ""
echo "Binaries:"
echo "  ./build/suspenders-demo       - Coroutine demo"
echo "  ./build/suspenders-benchmark  - Context switch benchmark"
echo "  ./build/suspenders-tests      - Unit test suite"
echo "  ./build/suspenders-switch     - Raw switch benchmark"
echo "  ./build/suspenders-bench-channels - Channel benchmark"
echo "  ./build/suspenders-bench-hose - Hose latency benchmark"
echo "  ./build/tcp_echo              - TCP echo server"
echo "  ./build/tcp_pingpong          - TCP ping/pong demo"
echo "  (plus examples: udp_echo unix_echo channel_demo suspend_resume_demo"
echo "   thread_pool dispatch_demo event_loop and *_cc C++ variants)"
echo ""
echo "Usage:"
echo "  ./build/suspenders-demo                    # Run demo"
echo "  ./build/suspenders-tests                   # Run unit tests"
echo "  ./build/suspenders-benchmark 1000000       # Run benchmark"
echo ""
