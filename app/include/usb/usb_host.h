#pragma once

#include "usbh_def.h"

#include <stdbool.h>
#include <stdint.h>

/* USB Host (MSC class) application init and connect/ready/disconnect state.
 * Shared across every embedded target (host has no USB) - this used to be
 * a near-duplicate per target (only IRQ registration and one Kconfig-style
 * flag genuinely differed); those two things are now the only things left
 * to inject, via usbh_conf.h's usbHostIrqInit()/usbHostIrqDisable() and
 * osal.h's osal_malloc()/osal_free() respectively. */

/* USB Host Core handle - the single OTG_FS Host instance this board uses. */
extern USBH_HandleTypeDef hUsbHostFS;

/* MSC connect/ready/disconnect state, updated from this file's own
 * USBH_UserProcess callback. Consumers (e.g. usbh_msc_disk.c's PalStorage
 * adapter) should treat this as the gate for "safe to touch USBH_MSC_* /
 * f_mount". */
typedef enum UsbHostMscStatus
{
    kUsbHostMscDisconnected = 0, /* no device attached, or attach/enum failed */
    kUsbHostMscConnected = 1,    /* HOST_USER_CONNECTION seen; not yet SCSI-ready */
    kUsbHostMscReady = 2,        /* HOST_USER_CLASS_ACTIVE seen; LUN 0 usable */
} UsbHostMscStatus;

/** USB Host initialization function - registers the MSC class and starts
  * the host stack. Called once, internally, by usbHostProcessTaskEntry()
  * below - callers never need to call this directly. */
void MX_USB_HOST_Init(void);

/** Task entry point (osal.h's OsalTaskEntryFn shape - register it via
  * AppDependencies.usbHostProcessEntry, see app.h): calls
  * MX_USB_HOST_Init() once, then pumps USBH_Process(&hUsbHostFS) in a loop
  * for the lifetime of the app via osal_delay_ms() - USBH_USE_OS is always
  * 0 in this project's usbh_conf.h (see that file for why), so nothing
  * else drives the USB Host state machine forward. */
void usbHostProcessTaskEntry(void *context);

/** Current MSC connect/ready state - cheap, non-blocking, safe to poll. */
UsbHostMscStatus usbHostGetMscStatus(void);

/** Serializes USBH_Process() (run from usbHostProcessTaskEntry()'s own
  * thread) against any other thread's direct USBH_MSC_* calls (e.g.
  * usbh_msc_disk.c's blocking Read/Write on the Servicer thread). Without
  * this, a disconnect handled by USBH_Process() can free/tear down MSC
  * class state (USBH_MSC_InterfaceDeInit()) while another thread is still
  * mid-transfer against that same state - a real, hardware-confirmed
  * use-after-free. Any code outside usb_host.c that calls a USBH_MSC_ or
  * USBH_ function directly (currently only usbh_msc_disk.c) must hold this
  * lock for the duration of that call. Created once, lazily, by the first
  * caller - safe because usbhMscDiskInit() (the only other caller) always
  * runs from the single-threaded composition root, before appRun() starts
  * the process task or the Servicer task that would otherwise race it.
  *
  * usbHostLock() is timeout-bounded and returns false if the lock could not
  * be acquired in time - nothing in this codebase blocks on a lock forever
  * (see osal_mutex_lock's own doc comment). Callers must treat false as a
  * real failure (propagate it - do not proceed as though the lock were
  * held) and must not call usbHostUnlock() unless usbHostLock() returned
  * true first. */
void usbHostLockInit(void);
bool usbHostLock(void) __attribute__((warn_unused_result));
void usbHostUnlock(void);
