#pragma once

#include "osal.h"

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* USB Host library configuration, shared across every embedded target
 * (host has no USB) for this board (FK407M2-ZGT6 / STM32F407ZGT6,
 * USB_OTG_FS, embedded PHY). Used to be a near-duplicate per target; the
 * only two things that genuinely differ per platform are handled through
 * injection instead of a whole separate file:
 *  - USBH_malloc/USBH_free route through osal.h's osal_malloc()/
 *    osal_free() - FreeRTOS's heap is not newlib's heap
 *    (pvPortMalloc()/vPortFree()), so a plain malloc()/free() definition
 *    here would be wrong on that one target specifically; every other
 *    target's osal_malloc()/osal_free() happens to just be malloc()/free()
 *    or that platform's own heap (k_malloc() on Zephyr).
 *  - IRQ registration (HAL_NVIC_* vs Zephyr's IRQ_CONNECT()/irq_enable())
 *    is genuinely a different mechanism per platform, not just a different
 *    function name - usbh_conf.c calls usbHostIrqInit()/
 *    usbHostIrqDisable() instead, implemented once per target in
 *    pal/src/usb_host_irq.c. */

/* USBH_MAX_NUM_ENDPOINTS / USBH_MAX_NUM_INTERFACES / config+data buffer sizes
 * below are the generic MSC-bulk-only values ST's own USBH examples use -
 * not board-specific. USBH_MAX_NUM_SUPPORTED_CLASS is 1 because this build
 * only ever registers USBH_MSC_CLASS. */
#define USBH_MAX_NUM_ENDPOINTS                2U
#define USBH_MAX_NUM_INTERFACES               2U
#define USBH_MAX_NUM_CONFIGURATION            1U
#define USBH_MAX_NUM_SUPPORTED_CLASS          1U
#define USBH_KEEP_CFG_DESCRIPTOR              0U
#define USBH_MAX_SIZE_CONFIGURATION           0x200U
#define USBH_MAX_DATA_BUFFER                  0x200U
#define USBH_DEBUG_LEVEL                      1U

/* USBH_Init() never spawns its own OS thread on any target in this project
 * - usb_host.c's usbHostProcessTaskEntry() always pumps USBH_Process()
 * itself, via osal.h, so this is 0 everywhere rather than a per-target
 * choice. */
#define USBH_USE_OS                           0U

/* Memory management macros - required by usbh_core.c/usbh_pipes.c for per-
 * device/per-pipe bookkeeping (real alloc/free churn during enumeration,
 * not just once at boot). */
#define USBH_malloc               osal_malloc
#define USBH_free                 osal_free
#define USBH_memset               memset
#define USBH_memcpy               memcpy

/* Debug/log macros - required by the vendor .c files (USBH_UsrLog/ErrLog/
 * DbgLog are called unconditionally throughout usbh_core.c and the MSC
 * class driver) - function-like macros since the vendor call sites pass a
 * variable argument list straight through, which a plain static function
 * can't. printf() works identically on every embedded target here
 * (verified on real hardware on all three, including Zephyr, whose
 * picolibc printf() is already routed to the same console UART as
 * printk() once CONFIG_UART_CONSOLE is set). */
#if (USBH_DEBUG_LEVEL > 0U)
#define USBH_UsrLog(...)   printf(__VA_ARGS__); \
                            printf("\n");
#else
#define USBH_UsrLog(...)
#endif /* (USBH_DEBUG_LEVEL > 0U) */

#if (USBH_DEBUG_LEVEL > 1U)
#define USBH_ErrLog(...)   printf("ERROR: "); \
                            printf(__VA_ARGS__); \
                            printf("\n");
#else
#define USBH_ErrLog(...)
#endif /* (USBH_DEBUG_LEVEL > 1U) */

#if (USBH_DEBUG_LEVEL > 2U)
#define USBH_DbgLog(...)   printf("DEBUG : "); \
                            printf(__VA_ARGS__); \
                            printf("\n");
#else
#define USBH_DbgLog(...)
#endif /* (USBH_DEBUG_LEVEL > 2U) */

/* Implemented once per target in pal/src/usb_host_irq.c - see this
 * file's own top comment for why IRQ registration can't be a plain macro
 * the way USBH_malloc/USBH_free can. */
void usbHostIrqInit(void);
void usbHostIrqDisable(void);
