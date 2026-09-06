cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fk407m2_blinky_uart_usbh_msc)

# This app links the SAME shared app/ + external/ sources (two levels up,
# repo root) as the host/freertos/threadx targets - only this directory's
# own concrete adapters (OSAL: osal.c, cfuture_sync_ops.c; PAL: log_sink_impl.c,
# time_source_impl.c, board_led.c, usb_host_irq.c, hal_shim.c, crash_dump_zephyr.c)
# and src/main.c (composition root) are Zephyr-specific. usb_host.c/usbh_conf.c
# used to be a near-duplicate per target too; they're shared app/ sources now -
# only IRQ registration (usb_host_irq.c) and the heap (osal.c's
# osal_malloc()/osal_free()) still differ per platform.
set(APP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SHARED_APP_DIR ${APP_ROOT}/app)
set(SHARED_EXTERNAL_DIR ${APP_ROOT}/external)
set(OSAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/osal)
set(PAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pal)

# Zephyr's own hal_stm32 module vendors this SoC's HAL/LL sources but only
# compiles the subset its own drivers reference (see CLAUDE.md for why). We
# add exactly the files this app's USB Host MSC + FatFS showcase needs
# directly to `app`, alongside the app's own sources.
set(HAL_STM32_F4XX_DIR $ENV{ZEPHYR_BASE}/../modules/hal/stm32/stm32cube/stm32f4xx)

target_sources(app PRIVATE
    src/main.c
    osal/src/osal.c
    osal/src/cfuture_sync_ops.c
    pal/src/board_led.c
    pal/src/log_sink_impl.c
    pal/src/time_source_impl.c
    pal/src/hal_shim.c
    pal/src/usb_host_irq.c
    pal/src/crash_dump_zephyr.c

    # Shared OS-agnostic application layer (identical across every target)
    ${SHARED_APP_DIR}/src/core/app.c
    ${SHARED_APP_DIR}/src/core/app_threads.c
    ${SHARED_APP_DIR}/src/core/logger.c
    ${SHARED_APP_DIR}/src/diagnostics/app_task_trace.c
    ${SHARED_APP_DIR}/src/diagnostics/blinky_task.c
    ${SHARED_APP_DIR}/src/diagnostics/crash_dump.c
    ${SHARED_APP_DIR}/src/storage/storage_service.c
    ${SHARED_APP_DIR}/src/storage/client_tasks.c
    ${SHARED_APP_DIR}/src/storage/storage_demo.c
    ${SHARED_APP_DIR}/src/storage/fatfs_diskio.c
    ${SHARED_APP_DIR}/src/storage/fatfs_time.c
    ${SHARED_APP_DIR}/src/usb/usbh_msc_disk.c
    ${SHARED_APP_DIR}/src/usb/usb_host.c
    ${SHARED_APP_DIR}/src/usb/usbh_conf.c

    # Vendored ST HAL (HCD + GPIO + Cortex/NVIC - see CLAUDE.md)
    ${HAL_STM32_F4XX_DIR}/drivers/src/stm32f4xx_hal_hcd.c
    ${HAL_STM32_F4XX_DIR}/drivers/src/stm32f4xx_ll_usb.c
    ${HAL_STM32_F4XX_DIR}/drivers/src/stm32f4xx_hal_gpio.c
    ${HAL_STM32_F4XX_DIR}/drivers/src/stm32f4xx_hal_cortex.c

    # Shared vendored ST USB Host Core + MSC class library
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Core/Src/usbh_core.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Core/Src/usbh_ctlreq.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Core/Src/usbh_ioreq.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Core/Src/usbh_pipes.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Class/MSC/Src/usbh_msc.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Class/MSC/Src/usbh_msc_bot.c
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Class/MSC/Src/usbh_msc_scsi.c

    # Shared vendored Chan's FatFS (_USE_LFN == 0 in this project's
    # ffconf.h - no long-filename support, so option/ffunicode.c is not needed)
    ${SHARED_EXTERNAL_DIR}/fatfs/src/ff.c

    # Shared vendored libcfuture
    ${SHARED_EXTERNAL_DIR}/cfuture/src/cfuture.c
    ${SHARED_EXTERNAL_DIR}/cfuture/src/cfuture_pal.c
)

target_include_directories(app PRIVATE
    ${OSAL_DIR}/include
    ${PAL_DIR}/include
    ${SHARED_APP_DIR}/include
    ${SHARED_APP_DIR}/include/osal
    ${SHARED_APP_DIR}/include/pal
    ${SHARED_APP_DIR}/include/core
    ${SHARED_APP_DIR}/include/diagnostics
    ${SHARED_APP_DIR}/include/storage
    ${SHARED_APP_DIR}/include/usb
    ${SHARED_EXTERNAL_DIR}/cfuture/include
    ${SHARED_EXTERNAL_DIR}/fatfs
    ${SHARED_EXTERNAL_DIR}/fatfs/src
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Core/Inc
    ${SHARED_EXTERNAL_DIR}/usb_host_lib/Class/MSC/Inc
    ${HAL_STM32_F4XX_DIR}/drivers/include
    ${HAL_STM32_F4XX_DIR}/soc
    $ENV{ZEPHYR_BASE}/../modules/hal/cmsis/CMSIS/Core/Include
)

target_compile_definitions(app PRIVATE
    STM32F407xx
    USE_HAL_DRIVER
)
