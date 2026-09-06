# ── storage_demo_host ───────────────────────────────────────────────────
#
# Native (POSIX/host) build of the storage-service showcase. This links the
# exact same shared app/ sources at the repo root that also build for the
# FreeRTOS/ThreadX/Zephyr targets (see ../freertos, ../threadx, ../zephyr)
# against this target's own concrete adapters: OSAL (osal/, cfuture_sync_ops.c)
# and PAL (pal/, pal_host_disk.c, log_sink_impl.c, time_source_impl.c, board_led.c),
# so the whole Servicer/Requester + libcfuture flow builds and runs on the
# desktop with no board attached.
#
# Deliberately contains no STM32 HAL/CMSIS-dependent code - just app/ plus
# this directory's own host-only adapters.

set(APP_DIR ${CMAKE_SOURCE_DIR}/app)
set(FATFS_DIR ${CMAKE_SOURCE_DIR}/external/fatfs)
set(OSAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/osal)
set(PAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pal)

add_executable(storage_demo_host
    ${APP_DIR}/src/core/app.c
    ${APP_DIR}/src/core/app_threads.c
    ${APP_DIR}/src/core/logger.c
    ${APP_DIR}/src/diagnostics/app_task_trace.c
    ${APP_DIR}/src/diagnostics/blinky_task.c
    ${APP_DIR}/src/storage/storage_service.c
    ${APP_DIR}/src/storage/client_tasks.c
    ${APP_DIR}/src/storage/storage_demo.c
    ${APP_DIR}/src/storage/fatfs_diskio.c
    ${APP_DIR}/src/storage/fatfs_time.c
    ${OSAL_DIR}/src/osal.c
    ${OSAL_DIR}/src/cfuture_sync_ops.c
    ${PAL_DIR}/src/log_sink_impl.c
    ${PAL_DIR}/src/time_source_impl.c
    ${PAL_DIR}/src/board_led.c
    ${PAL_DIR}/src/pal_host_disk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${FATFS_DIR}/src/ff.c
)

target_include_directories(storage_demo_host PRIVATE
    ${APP_DIR}/include
    ${APP_DIR}/include/osal
    ${APP_DIR}/include/pal
    ${APP_DIR}/include/core
    ${APP_DIR}/include/diagnostics
    ${APP_DIR}/include/storage
    ${APP_DIR}/include/usb
    ${OSAL_DIR}/include
    ${PAL_DIR}/include
    ${FATFS_DIR}            # ffconf.h lives here, one level above src/
    ${FATFS_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(storage_demo_host PRIVATE cfuture Threads::Threads)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(storage_demo_host PRIVATE -Wall -Wextra)
endif()
