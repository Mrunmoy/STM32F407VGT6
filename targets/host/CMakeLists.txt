# ── storage_demo_host ───────────────────────────────────────────────────
#
# Native (POSIX/host) build of the storage-service showcase. This links the
# exact same shared app/ sources at the repo root that also build for the
# FreeRTOS/ThreadX/Zephyr targets (see ../freertos, ../threadx, ../zephyr)
# against this target's own concrete adapters (os_glue/) - osal.c,
# cfuture_sync_ops.c, log_sink_impl.c, time_source_impl.c, board_led.c - and
# pal_host_disk.c (this target's PalStorage adapter, no counterpart
# elsewhere), so the whole Servicer/Requester + libcfuture flow builds and
# runs on the desktop with no board attached.
#
# Deliberately contains no STM32 HAL/CMSIS-dependent code - just app/ plus
# this directory's own host-only adapters.

set(APP_DIR ${CMAKE_SOURCE_DIR}/app)
set(FATFS_DIR ${CMAKE_SOURCE_DIR}/external/fatfs)
set(OS_GLUE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/os_glue)

add_executable(storage_demo_host
    ${APP_DIR}/src/app.c
    ${APP_DIR}/src/app_threads.c
    ${APP_DIR}/src/app_task_trace.c
    ${APP_DIR}/src/blinky_task.c
    ${APP_DIR}/src/storage_service.c
    ${APP_DIR}/src/client_tasks.c
    ${APP_DIR}/src/storage_demo.c
    ${APP_DIR}/src/fatfs_diskio.c
    ${APP_DIR}/src/fatfs_time.c
    ${APP_DIR}/src/logger.c
    ${OS_GLUE_DIR}/src/osal.c
    ${OS_GLUE_DIR}/src/cfuture_sync_ops.c
    ${OS_GLUE_DIR}/src/log_sink_impl.c
    ${OS_GLUE_DIR}/src/time_source_impl.c
    ${OS_GLUE_DIR}/src/board_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pal_host_disk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${FATFS_DIR}/src/ff.c
)

target_include_directories(storage_demo_host PRIVATE
    ${APP_DIR}/include
    ${OS_GLUE_DIR}/include
    ${FATFS_DIR}            # ffconf.h lives here, one level above src/
    ${FATFS_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(storage_demo_host PRIVATE cfuture Threads::Threads)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(storage_demo_host PRIVATE -Wall -Wextra)
endif()
