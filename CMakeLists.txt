cmake_minimum_required(VERSION 3.16)
project(libsuspenders VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# Platform-specific setup
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Find liburing - try pkg-config first, then manual search
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBURING liburing>=2.0)
    endif()

    if(NOT LIBURING_FOUND)
        find_path(LIBURING_INCLUDE_DIR 
            NAMES liburing.h
            PATHS /usr/include /usr/local/include
        )
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
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    message(STATUS "Platform: macOS/BSD - using kqueue backend")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Platform: Windows - using WSAPoll backend")
else()
    message(STATUS "Platform: Generic POSIX - using poll backend")
endif()

# Find Threads
find_package(Threads REQUIRED)

# Find math library (for benchmark)
find_library(MATH_LIBRARY m)

# Common include directories
set(SUSPENDERS_INCLUDE_DIRS include third_party)
if(LIBURING_FOUND)
    list(APPEND SUSPENDERS_INCLUDE_DIRS ${LIBURING_INCLUDE_DIRS})
endif()

# Common link libraries
set(SUSPENDERS_LINK_LIBS Threads::Threads)
if(LIBURING_FOUND)
    list(APPEND SUSPENDERS_LINK_LIBS ${LIBURING_LIBRARIES})
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND SUSPENDERS_LINK_LIBS ws2_32)
endif()

# Helper to add example/benchmark targets
function(add_suspenders_executable name source_dir)
    add_executable(${name} ${source_dir}/${name}.c)
    target_include_directories(${name} PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${SUSPENDERS_LINK_LIBS})
    if(LIBURING_CFLAGS_OTHER)
        target_compile_options(${name} PRIVATE ${LIBURING_CFLAGS_OTHER})
    endif()
endfunction()

# Main server executable
add_executable(suspenders-demo src/main.c)
target_include_directories(suspenders-demo PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-demo PRIVATE ${SUSPENDERS_LINK_LIBS})
if(LIBURING_CFLAGS_OTHER)
    target_compile_options(suspenders-demo PRIVATE ${LIBURING_CFLAGS_OTHER})
endif()

# Enable testing
enable_testing()

# Test executable
add_executable(suspenders-tests tests/test_main.c)
target_include_directories(suspenders-tests PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-tests PRIVATE ${SUSPENDERS_LINK_LIBS})
if(LIBURING_CFLAGS_OTHER)
    target_compile_options(suspenders-tests PRIVATE ${LIBURING_CFLAGS_OTHER})
endif()

# Add test
add_test(NAME suspenders_unit_tests COMMAND suspenders-tests)

# Benchmark executables
add_executable(suspenders-benchmark benchmarks/benchmark_suspenders.c)
target_include_directories(suspenders-benchmark PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-benchmark PRIVATE ${SUSPENDERS_LINK_LIBS} ${MATH_LIBRARY})
target_compile_options(suspenders-benchmark PRIVATE -O3)

add_executable(suspenders-switch benchmarks/suspenders_switch.c)
target_include_directories(suspenders-switch PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-switch PRIVATE ${SUSPENDERS_LINK_LIBS})
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    target_compile_options(suspenders-switch PRIVATE -O3 -march=armv8.2-a)
else()
    target_compile_options(suspenders-switch PRIVATE -O3)
endif()

add_executable(suspenders-bench-channels benchmarks/benchmark_channels.c)
target_include_directories(suspenders-bench-channels PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-bench-channels PRIVATE ${SUSPENDERS_LINK_LIBS})
target_compile_options(suspenders-bench-channels PRIVATE -O3)

add_executable(suspenders-bench-hose benchmarks/benchmark_hose.c)
target_include_directories(suspenders-bench-hose PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
target_link_libraries(suspenders-bench-hose PRIVATE ${SUSPENDERS_LINK_LIBS})
target_compile_options(suspenders-bench-hose PRIVATE -O3)

# Example programs
add_suspenders_executable(tcp_pingpong examples)
add_suspenders_executable(tcp_echo examples)
add_suspenders_executable(udp_echo examples)
add_suspenders_executable(unix_echo examples)
add_suspenders_executable(channel_demo examples)
add_suspenders_executable(suspend_resume_demo examples)

# C++ example
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
    add_executable(simple_cc examples/simple.cc)
    set_target_properties(simple_cc PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(simple_cc PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
    target_link_libraries(simple_cc PRIVATE ${SUSPENDERS_LINK_LIBS})
    if(LIBURING_CFLAGS_OTHER)
        target_compile_options(simple_cc PRIVATE ${LIBURING_CFLAGS_OTHER})
    endif()
endif()

# Install targets
install(TARGETS suspenders-demo DESTINATION bin)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C Compiler:     ${CMAKE_C_COMPILER}")
message(STATUS "  C Flags:        ${CMAKE_C_FLAGS}")
message(STATUS "  Platform:       ${CMAKE_SYSTEM_NAME}")
if(LIBURING_FOUND)
    message(STATUS "  Backend:        io_uring")
    message(STATUS "  liburing:       ${LIBURING_LIBRARIES}")
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
        message(STATUS "  Backend:        kqueue")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(STATUS "  Backend:        WSAPoll")
    else()
        message(STATUS "  Backend:        poll")
    endif()
endif()
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
