cmake_minimum_required(VERSION 3.16)

# This CMakeLists.txt builds the "host" target only - a native executable
# that runs on the desktop, no board attached. The embedded targets
# (targets/freertos, targets/threadx, targets/zephyr) each have their own
# separate build system (GNU Make / west+CMake) - see build.py and each
# target's own README/CLAUDE.md notes. All targets, including this one,
# link the SAME app/ + external/ sources at the root of this repo; only the
# OSAL adapter and composition root differ per target.
project(stm32f407_multi_os_storage_showcase LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# libcfuture (external/cfuture) defines the `cfuture` static library target.
# Its own CMakeLists.txt turns its tests/benchmarks/examples off
# automatically when added as a subdirectory.
add_subdirectory(external/cfuture)

add_subdirectory(targets/host)

# ── Tests ────────────────────────────────────────────────────────────────
#
# app/'s DI interfaces (LogSink, TimeSource, ...) make the pure logic behind
# them directly unit-testable in isolation, no OS/hardware/mocking framework
# needed - see tests/test_logger.c for the pattern. `ctest` from the build
# directory runs everything registered below; tools/test_build_stats.py
# (plain Python unittest, no new dependency) separately covers build.py's
# own parsing logic.
enable_testing()

add_executable(test_logger
    tests/test_logger.c
    app/src/logger.c
)
target_include_directories(test_logger PRIVATE app/include)
add_test(NAME test_logger COMMAND test_logger)
