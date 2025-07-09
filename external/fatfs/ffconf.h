/*---------------------------------------------------------------------------/
/  FatFs (R0.12c) - Project configuration file for stm32f407-threadx
/
/  Based on src/ffconf_template.h (kept alongside, unmodified, as a diff
/  target) and edited for this project's constraints: no dynamic allocation,
/  write support required, single volume, fixed 512-byte sectors, RTC-backed
/  timestamps via fatfs_time.c. Macro names are this package's actual R0.12c
/  underscore-prefixed names (_FS_READONLY, _USE_LFN, ...), NOT the FF_-
/  prefixed names used by R0.13+.
/---------------------------------------------------------------------------*/
#pragma once

#define _FFCONF 68300   /* Must match _FATFS revision ID in ff.h */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define _FS_READONLY    0   /* MUST be 0 - write support required */
#define _FS_MINIMIZE    0   /* Keep full API (f_stat/f_getfree/f_unlink/f_mkdir/f_truncate/f_rename) */
#define _USE_STRFUNC    1   /* f_gets/f_putc/f_puts/f_printf, no LF->CRLF conversion */
#define _USE_FIND       0   /* f_findfirst/f_findnext not needed - saves flash */
#define _USE_MKFS       1   /* Enable in-field f_mkfs(); caller supplies the work buffer
                                 (stack/static) - no heap involved, safe under the
                                 no-malloc constraint. */
#define _USE_FASTSEEK   0
#define _USE_EXPAND     0
#define _USE_CHMOD      0
#define _USE_LABEL      0
#define _USE_FORWARD    0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define _CODE_PAGE      1   /* ASCII only - consistent with _USE_LFN == 0 (short 8.3 names) */
#define _USE_LFN        0   /* NO long filenames, NO malloc anywhere in FatFs - 8.3 short
                                 names only. If long filenames are ever required, set this
                                 to 1 (NOT 2 or 3): mode 1 keeps the LFN working buffer in a
                                 static BSS array inside ff.c, still zero heap/stack growth.
                                 Modes 2 (stack) and 3 (malloc) are both disallowed here. */
#define _MAX_LFN        255 /* only consulted when _USE_LFN != 0 */
#define _LFN_UNICODE    0   /* ANSI/OEM strings (irrelevant while _USE_LFN == 0) */
#define _STRF_ENCODE    3   /* irrelevant while _LFN_UNICODE == 0 */
#define _FS_RPATH       0   /* no f_chdir/f_chdrive/f_getcwd - fixed absolute paths only */

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define _VOLUMES         1   /* single physical volume */
#define _STR_VOLUME_ID   0
#define _MULTI_PARTITION 0
#define _MIN_SS   512        /* fixed 512-byte sectors (SD/eMMC/MSC standard); keeping
                                 _MIN_SS == _MAX_SS removes FatFs's variable-sector-size
                                 code path and the GET_SECTOR_SIZE ioctl requirement */
#define _MAX_SS   512
#define _USE_TRIM 0          /* enable + implement CTRL_TRIM in disk_ioctl only if the
                                 backing PalStorage is TRIM-capable */
#define _FS_NOFSINFO 0        /* trust FSINFO free-cluster count on FAT32 */

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define _FS_TINY    0   /* Normal config: FIL gets its own _MAX_SS-byte window, separate
                            from FATFS.win[]. Costs one extra 512B buffer per open file
                            vs. _FS_TINY=1, but keeps multi-file access simple/safe. */
#define _FS_EXFAT   0   /* Not needed for plain FAT32 R/W; exFAT requires _USE_LFN >= 1
                            (conflicts with _USE_LFN == 0 above) plus more flash. */
#define _FS_NORTC     0     /* RTC-backed timestamps ARE available (fatfs_time.c) - keep
                                timestamping enabled and implement get_fattime(). */
#define _NORTC_MON   1      /* fallback values, unused while _FS_NORTC == 0 */
#define _NORTC_MDAY  1
#define _NORTC_YEAR  2026

#define _FS_LOCK   4    /* Small file-lock table (guards against illegal duplicate
                            open/rename/unlink of the same object). Must be 0 when
                            _FS_READONLY == 1 (not the case here). */
#define _FS_REENTRANT 0 /* Disabled: no more than one task touches the filesystem
                            concurrently yet. Enable (and vendor src/option/syscall.c)
                            only once that changes. */
#define _USE_MUTEX 0    /* irrelevant while _FS_REENTRANT == 0 */
