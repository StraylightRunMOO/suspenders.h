cmake_minimum_required(VERSION 3.16)
project(libsuspenders VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SUSPENDERS_WERROR "Treat warnings as errors" ON)
set(SUSPENDERS_SANITIZE "off" CACHE STRING
    "Sanitizer: off, address, undefined, address,undefined, or thread")

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -pedantic -O2)
    # Strict -std=c11 hides POSIX/GNU declarations in glibc; the library needs
    # sockets, clock_gettime, usleep, pthreads, io_uring.
    add_compile_definitions(_GNU_SOURCE)
    if(SUSPENDERS_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

if(NOT SUSPENDERS_SANITIZE STREQUAL "off")
    add_compile_options(-fsanitize=${SUSPENDERS_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${SUSPENDERS_SANITIZE})
    message(STATUS "Sanitizers enabled: ${SUSPENDERS_SANITIZE}")
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
        else()
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

find_package(Threads REQUIRED)
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

# Helper to add a C target
function(add_suspenders_executable name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${SUSPENDERS_LINK_LIBS})
    if(LIBURING_CFLAGS_OTHER)
        target_compile_options(${name} PRIVATE ${LIBURING_CFLAGS_OTHER})
    endif()
endfunction()

# Demo
add_suspenders_executable(suspenders-demo src/main.c)

# Tests
enable_testing()
add_suspenders_executable(suspenders-tests tests/test_main.c)
add_test(NAME suspenders_unit_tests COMMAND suspenders-tests)

add_suspenders_executable(test-memento tests/test_memento.c)
add_test(NAME memento_unit_tests COMMAND test-memento)

# Same suite pinned to the portable poll backend (keeps the readiness path verified)
add_suspenders_executable(suspenders-tests-poll tests/test_main.c)
target_compile_definitions(suspenders-tests-poll PRIVATE SUSPENDERS_FORCE_POLL)
add_test(NAME suspenders_unit_tests_poll COMMAND suspenders-tests-poll)

# Benchmarks
add_suspenders_executable(suspenders-benchmark benchmarks/benchmark_suspenders.c)
target_link_libraries(suspenders-benchmark PRIVATE ${MATH_LIBRARY})
target_compile_options(suspenders-benchmark PRIVATE -O3)

add_suspenders_executable(suspenders-switch benchmarks/suspenders_switch.c)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    target_compile_options(suspenders-switch PRIVATE -O3 -march=armv8.2-a)
else()
    target_compile_options(suspenders-switch PRIVATE -O3)
endif()

add_suspenders_executable(suspenders-bench-channels benchmarks/benchmark_channels.c)
target_compile_options(suspenders-bench-channels PRIVATE -O3)

add_suspenders_executable(suspenders-bench-hose benchmarks/benchmark_hose.c)
target_compile_options(suspenders-bench-hose PRIVATE -O3)

add_suspenders_executable(suspenders-bench-cycles benchmarks/benchmark_cycles.c)
target_compile_options(suspenders-bench-cycles PRIVATE -O3)

# C examples
foreach(example tcp_pingpong tcp_echo udp_echo unix_echo channel_demo
                suspend_resume_demo thread_pool dispatch_demo event_loop
                pipeline select_mux sync_primitives chat_server timeout_retry)
    add_suspenders_executable(${example} examples/${example}.c)
endforeach()

# C++ examples (<name>.cc builds target <name>_cc; simple.cc -> simple_cc)
function(add_suspenders_cc_executable name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${SUSPENDERS_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${SUSPENDERS_LINK_LIBS})
    if(LIBURING_CFLAGS_OTHER)
        target_compile_options(${name} PRIVATE ${LIBURING_CFLAGS_OTHER})
    endif()
endfunction()

add_suspenders_cc_executable(simple_cc examples/simple.cc)
add_suspenders_cc_executable(thread_pool_cc examples/thread_pool.cc)
add_suspenders_cc_executable(dispatch_demo_cc examples/dispatch_demo.cc)
add_suspenders_cc_executable(event_loop_cc examples/event_loop.cc)
add_suspenders_cc_executable(pipeline_cc examples/pipeline.cc)
add_suspenders_cc_executable(chat_server_cc examples/chat_server.cc)
add_suspenders_cc_executable(scatter_gather_cc examples/scatter_gather.cc)

# Install: headers + pkg-config (header-only library)
include(GNUInstallDirs)
install(FILES include/suspenders.h include/suspenders.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES third_party/memento.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/suspenders)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/suspenders.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/suspenders.pc"
    @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/suspenders.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C Compiler:     ${CMAKE_C_COMPILER}")
message(STATUS "  Platform:       ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Sanitizers:     ${SUSPENDERS_SANITIZE}")
if(LIBURING_FOUND)
    message(STATUS "  Backend:        io_uring (${LIBURING_LIBRARIES})")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    message(STATUS "  Backend:        kqueue")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "  Backend:        WSAPoll")
else()
    message(STATUS "  Backend:        poll")
endif()
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
