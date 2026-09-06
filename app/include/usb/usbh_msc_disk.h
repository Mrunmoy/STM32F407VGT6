#pragma once

#include "pal_storage.h"

/* Concrete PalStorage adapter over the USBH MSC class driver (usb_host.h /
 * Middlewares/ST/STM32_USB_Host_Library Class/MSC). Talks to LUN 0 of
 * whatever is currently enumerated on the global hUsbHostFS handle
 * (usb_host.h) - this project only ever expects a single USB mass-storage
 * stick, not a hub or a multi-LUN enclosure.
 *
 * status()/read()/write() all report kPalStorageNotReady (or false, for
 * status()) whenever usbHostGetMscStatus() has not yet reached
 * kUsbHostMscReady - i.e. before the USBH MSC class driver has finished its
 * own SCSI INQUIRY/READ CAPACITY/TEST UNIT READY sequence, or after a
 * disconnect. No per-instance state is needed (there is exactly one LUN, on
 * exactly one host controller, for the life of this build), so unlike
 * board_led.c/uart_log_sink.c this adapter's PalStorage.context is unused
 * (NULL) - matching board_led.c's own pattern for a single fixed peripheral. */
void usbhMscDiskInit(PalStorage *outStorage);
