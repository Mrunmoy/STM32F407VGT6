#pragma once

#include "pal_storage.h"

/* Binds the single PalStorage instance that fatfs_diskio.c's disk_status/
 * disk_initialize/disk_read/disk_write/disk_ioctl implementations dispatch
 * to. Call once from the composition root, before f_mount() is ever called -
 * storage-backend-agnostic, works unchanged for the host RAM/file disk and
 * the STM32 USBH-MSC disk. This project's ffconf.h fixes _VOLUMES == 1, so
 * every disk_xxx() call is expected to arrive with pdrv == 0. */
void fatfsDiskioBind(PalStorage *storage);
