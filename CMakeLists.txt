cmake_minimum_required(VERSION 3.16)
project(libsuspenders VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Compiler flags
add_compile_options(-Wall -Wextra -O2)

# Find liburing - try pkg-config first, then manual search
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBURING liburing>=2.0)
endif()

if(NOT LIBURING_FOUND)
    # Manual search for liburing
    find_path(LIBURING_INCLUDE_DIR 
        NAMES liburing.h
        PATHS /usr/include /usr/local/include
    )
    
    # Search for liburing with various naming schemes
    find_library(LIBURING_LIBRARY
        NAMES uring uring.so.2 uring-ffi
        PATHS /usr/lib /usr/local/lib 
              /usr/lib/aarch64-linux-gnu 
              /usr/lib/x86_64-linux-gnu
              /usr/lib/arm-linux-gnueabihf
    )
    
    if(LIBURING_INCLUDE_DIR AND LIBURING_LIBRARY)
        set(LIBURING_FOUND TRUE)
        set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE_DIR})
        set(LIBURING_LIBRARIES ${LIBURING_LIBRARY})
        set(LIBURING_CFLAGS_OTHER "")
        message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
        message(STATUS "liburing include: ${LIBURING_INCLUDE_DIR}")
    else()
        message(STATUS "liburing include dir: ${LIBURING_INCLUDE_DIR}")
        message(STATUS "liburing library: ${LIBURING_LIBRARY}")
        message(FATAL_ERROR "liburing not found. Please install liburing-dev or liburing-devel")
    endif()
else()
    message(STATUS "Found liburing via pkg-config")
endif()

# Find Threads
find_package(Threads REQUIRED)

# Find math library (for benchmark)
find_library(MATH_LIBRARY m)

# Main server executable
add_executable(suspenders-demo src/main.c)
target_include_directories(suspenders-demo PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(suspenders-demo PRIVATE ${LIBURING_LIBRARIES} Threads::Threads)
if(LIBURING_CFLAGS_OTHER)
    target_compile_options(suspenders-demo PRIVATE ${LIBURING_CFLAGS_OTHER})
endif()

# Enable testing
enable_testing()

# Test executable
add_executable(suspenders-tests tests/test_main.c)
target_include_directories(suspenders-tests PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(suspenders-tests PRIVATE ${LIBURING_LIBRARIES} Threads::Threads)
if(LIBURING_CFLAGS_OTHER)
    target_compile_options(suspenders-tests PRIVATE ${LIBURING_CFLAGS_OTHER})
endif()

# Add test
add_test(NAME suspenders_unit_tests COMMAND suspenders-tests)

# Benchmark executables
add_executable(suspenders-benchmark benchmarks/benchmark_suspenders.c)
target_include_directories(suspenders-benchmark PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(suspenders-benchmark PRIVATE ${LIBURING_LIBRARIES} Threads::Threads ${MATH_LIBRARY})
target_compile_options(suspenders-benchmark PRIVATE -O3)

add_executable(suspenders-switch benchmarks/suspenders_switch.c)
target_include_directories(suspenders-switch PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(suspenders-switch PRIVATE ${LIBURING_LIBRARIES} Threads::Threads)
target_compile_options(suspenders-switch PRIVATE -O3 -march=armv8.2-a)

# Example programs
# Example programs
add_executable(tcp_pingpong examples/tcp_pingpong.c)
target_include_directories(tcp_pingpong PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(tcp_pingpong PRIVATE ${LIBURING_LIBRARIES} Threads::Threads)
target_compile_options(tcp_pingpong PRIVATE -O2)

add_executable(tcp_echo examples/tcp_echo.c)
target_include_directories(tcp_echo PRIVATE ${LIBURING_INCLUDE_DIRS} include third_party)
target_link_libraries(tcp_echo PRIVATE ${LIBURING_LIBRARIES} Threads::Threads)
target_compile_options(tcp_echo PRIVATE -O2)

# Install targets
install(TARGETS suspenders-demo DESTINATION bin)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C Compiler:     ${CMAKE_C_COMPILER}")
message(STATUS "  C Flags:        ${CMAKE_C_FLAGS}")
message(STATUS "  liburing:       ${LIBURING_LIBRARIES}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
