# Embedded Systems Debugging Journal & Platform Quirks

This document chronicles the real-world bugs, concurrency races, hardware edge cases, and toolchain discrepancies discovered, diagnosed, and resolved during the development of this multi-RTOS firmware architecture.

---

## Table of Contents

- [Case 01: Multi-Core Race Condition in ABA Slot Isolation (Scenario 4)](#case-01-multi-core-race-condition-in-aba-slot-isolation-scenario-4)
- [Case 02: Silent FreeRTOS SRAM Corruption via Stack Overflow](#case-02-silent-freertos-sram-corruption-via-stack-overflow)
- [Case 03: ST HAL Version Drift & Uninitialized Flash Structure](#case-03-st-hal-version-drift--uninitialized-flash-structure)
- [Case 04: USB Host Initialization Deadlock under FreeRTOS](#case-04-usb-host-initialization-deadlock-under-freertos)
- [Case 05: Zephyr SoC Clock / APB2 Baud Rate Mismatch](#case-05-zephyr-soc-clock--apb2-baud-rate-mismatch)
- [Case 06: Devicetree Node Disabling Breaking Zephyr IRQ Table Auto-Sizing](#case-06-devicetree-node-disabling-breaking-zephyr-irq-table-auto-sizing)
- [Case 07: Unsigned Timestamp Subtraction Underflow in Task Health Check-In](#case-07-unsigned-timestamp-subtraction-underflow-in-task-health-check-in)
- [Case 08: Watchdog Blind Spots Caused by Unbounded Blocking Calls](#case-08-watchdog-blind-spots-caused-by-unbounded-blocking-calls)
- [Case 09: RTC Time Source Register Coherency Race](#case-09-rtc-time-source-register-coherency-race)
- [Case 10: ThreadX Self-Termination vs Deletion Constraint](#case-10-threadx-self-termination-vs-deletion-constraint)
- [Case 11: POSIX pthread_exit() Memory Leak in Host OSAL](#case-11-posix-pthread_exit-memory-leak-in-host-osal)
- [Case 12: Silent Flash Failure via Missing J-Link Verification](#case-12-silent-flash-failure-via-missing-j-link-verification)
- [Case 13: Direct RTOS API Leak in Application Task](#case-13-direct-rtos-api-leak-in-application-task)
- [Case 14: Non-Uniform Hardware Indication on ThreadX](#case-14-non-uniform-hardware-indication-on-threadx)

---

### Case 01: Multi-Core Race Condition in ABA Slot Isolation (Scenario 4)

- **Component / Subsystem**: Storage showcase concurrency test harness (`app/src/client_tasks.c`)
- **Target(s) Affected**: Native POSIX Host (`targets/host/`)
- **Symptom & Discovery Context**:
  During host unit test runs, Scenario 4 (ABA slot isolation) intermittently failed on its very first iteration (~40–60% failure rate), while all subsequent iterations passed cleanly. On all three embedded ARM targets (FreeRTOS, ThreadX, Zephyr), Scenario 4 passed 100% of the time across dozens of runs.
- **Root Cause Analysis**:
  Scenario 4 validates that rapid promise destruction and re-allocation does not misroute a late-arriving result to a recycled slot. It assumes sole access to the request queue during its initialization handshake.
  
  On the host desktop, POSIX pthreads run on a multi-core CPU with true hardware parallelism. Threads can execute instructions at the identical physical nanosecond. When all four requester tasks started concurrently at boot, Scenario 1–3 threads occasionally enqueued requests at the exact same instant Scenario 4 was establishing its baseline queue state.
  
  On the STM32F407, the Cortex-M4 core is strictly single-core. Thread context switches only occur at explicit scheduling boundaries (SysTick interrupts or blocking OSAL calls). Because of single-core instruction interleaving, the concurrent instant race never manifested on hardware.
- **Resolution & Verification**:
  Introduced a deterministic startup stagger delay (`kClientTaskStartStaggerMs`: 0ms, 100ms, 200ms, 300ms) before the first iteration of each client task. Verified with 15/15 consecutive clean host test runs without a single failure.

---

### Case 02: Silent FreeRTOS SRAM Corruption via Stack Overflow

- **Component / Subsystem**: FreeRTOS stack configuration and hook (`targets/freertos/Core/Src/freertos.c`)
- **Target(s) Affected**: FreeRTOS (`targets/freertos/`)
- **Symptom & Discovery Context**:
  Under heavy concurrent load on hardware, the system experienced intermittent hard faults or random memory corruptions. Tracing with SEGGER J-Link revealed that a FreeRTOS `Queue_t` control block in SRAM had its `uxItemSize` field overwritten with garbage, causing `memcpy` in `prvCopyDataFromQueue` to overrun SRAM.
- **Root Cause Analysis**:
  Initial stack estimations (4096 bytes for `StorageSvc`, 3072 bytes for client tasks) were estimated theoretically rather than measured on hardware. Scenario 4 holds a stale `StorageResult` and a new `StorageRequest`/`StorageResult` pair (~530B each) across nested stack frames (`occupyServicer`, `cfuture_wait_for`, `osal_queue_send`). 
  
  Crucially, CubeMX had generated `vApplicationStackOverflowHook()` as an empty stub function (`/* USER CODE BEGIN */ /* USER CODE END */`). When the stack overflowed, execution continued silently, corrupting adjacent heap memory without tripping an assertion.
- **Resolution & Verification**:
  1. Doubled stack allocations to verified safe margins: `kStorageServiceStackBytes = 8192U`, `kClientTaskStackBytes = 6144U`.
  2. Implemented a hard trap in `vApplicationStackOverflowHook()` that turns on the status LED, emits an error over UART, and enters an infinite loop.

---

### Case 03: ST HAL Version Drift & Uninitialized Flash Structure

- **Component / Subsystem**: Vendor Hardware Abstraction Layer (`external/stm32f4xx_hal/`)
- **Target(s) Affected**: FreeRTOS & ThreadX targets
- **Symptom & Discovery Context**:
  During codebase consolidation, code diffs revealed subtle behavioral differences in flash access and peripheral configuration between the FreeRTOS and ThreadX targets.
- **Root Cause Analysis**:
  Each target originally maintained its own private copy of ST's `Drivers/STM32F4xx_HAL_Driver` directory. Over time, these trees had quietly drifted to two different upstream versions: V1.8.4 in one target and V1.8.5 in the other.
  
  Checking against the canonical `STM32Cube_FW_F4_V1.28.3` firmware package confirmed that V1.8.4 suffered from a known upstream bug involving an uninitialized `FLASH_ProcessTypeDef pFlash` structure, which could cause unpredictable behavior during internal flash routines.
- **Resolution & Verification**:
  Deleted both per-target `Drivers/` directories and vendored a single, authoritative copy of CMSIS and STM32F4xx HAL V1.8.5 under `external/stm32f4xx_hal/`. Both Makefiles were updated to link against this shared directory, eliminating version skew.

---

### Case 04: USB Host Initialization Deadlock under FreeRTOS

- **Component / Subsystem**: USB Host Core state machine (`app/src/usb_host.c`)
- **Target(s) Affected**: FreeRTOS (`targets/freertos/`)
- **Symptom & Discovery Context**:
  The FreeRTOS firmware hung indefinitely during boot before the scheduler began multi-tasking.
- **Root Cause Analysis**:
  ST's USB Host library supports two modes: `USBH_USE_OS = 1` (the library internally creates a CMSIS-RTOS thread to pump state changes) and `USBH_USE_OS = 0` (the application periodically calls `USBH_Process()`).
  
  FreeRTOS had been configured with `USBH_USE_OS = 1`. In the generated startup code, `MX_USB_HOST_Init()` called `osThreadNew()` *before* `osKernelStart()`. In CMSIS-OS2 on FreeRTOS, creating threads prior to kernel initialization led to internal queue and lock deadlocks. ThreadX and Zephyr had already been using `USBH_USE_OS = 0` with a dedicated application pump task.
- **Resolution & Verification**:
  Switched FreeRTOS to `USBH_USE_OS = 0`. Unified the background polling logic into a single shared `usbHostProcessTaskEntry()` loop in `app/src/usb_host.c` across all three embedded targets, extracting only the NVIC IRQ enable into `pal/src/usb_host_irq.c`.

---

### Case 05: Zephyr SoC Clock / APB2 Baud Rate Mismatch

- **Component / Subsystem**: Raw UART diagnostic crash dump (`app/src/crash_dump.c`)
- **Target(s) Affected**: Zephyr RTOS (`targets/zephyr/`)
- **Symptom & Discovery Context**:
  When a CPU fault was deliberately triggered on Zephyr, the output on USART1 was unreadable binary garbage, while FreeRTOS and ThreadX dumped pristine ASCII registers.
- **Root Cause Analysis**:
  To remain safe during a crash, `crashDumpReport()` directly computes the USART1 Baud Rate Register (`BRR = PCLK2 / Baud`). It originally read the CMSIS global variable `SystemCoreClock` to determine peripheral clock speed.
  
  In Zephyr's startup sequence (`soc_early_init_hook()`), `SystemCoreClock` is initialized to the 16 MHz HSI default. Although Zephyr's clock control driver subsequently programs the PLL to 168 MHz, Zephyr never updates the CMSIS `SystemCoreClock` global. Reading `SystemCoreClock` computed `16 MHz / 2 = 8 MHz` for APB2 instead of the actual `84 MHz`, generating completely erroneous baud divisor values.
- **Resolution & Verification**:
  Replaced the dynamic `SystemCoreClock` read in the bare-metal fault reporter with a constant board frequency definition (`crashDumpApb2ClockHz() = 84000000U`). USART1 crash dumps immediately became crystal clear at 115200 baud on Zephyr.

---

### Case 06: Devicetree Node Disabling Breaking Zephyr IRQ Table Auto-Sizing

- **Component / Subsystem**: Zephyr devicetree overlay (`targets/zephyr/boards/black_f407zg_pro.overlay`)
- **Target(s) Affected**: Zephyr RTOS (`targets/zephyr/`)
- **Symptom & Discovery Context**:
  When building Zephyr, calling `IRQ_CONNECT(OTG_FS_IRQn, ...)` resulted in a runtime kernel assertion or hard fault during IRQ table registration.
- **Root Cause Analysis**:
  Because this project drives the USB OTG FS hardware directly using ST's HAL rather than Zephyr's native USB driver, the board overlay initially marked the devicetree node `usbotg_fs` as `status = "disabled"` to prevent driver collisions.
  
  However, Zephyr's build system inspects devicetree nodes to compute the maximum interrupt vector table size. Marking the node disabled pruned `OTG_FS_IRQn` (IRQ 67) from the active interrupt table, leaving its vector slot unallocated.
- **Resolution & Verification**:
  Retained `status = "okay"` for `&usbotg_fs` in `black_f407zg_pro.overlay`, while disabling Zephyr's higher-level USB stack in `prj.conf` (`CONFIG_USB_DEVICE_STACK=n`). This preserved the full hardware vector table while giving our application exclusive control of the peripheral.

---

### Case 07: Unsigned Timestamp Subtraction Underflow in Task Health Check-In

- **Component / Subsystem**: Task supervision telemetry (`app/src/app_task_trace.c`)
- **Target(s) Affected**: All targets
- **Symptom & Discovery Context**:
  The watchdog task occasionally emitted false alarm warnings:
  `[ERROR] watchdog: StorageSvc UNHEALTHY - 4294967280 ms since check-in`
  even though the task was actively running and healthy.
- **Root Cause Analysis**:
  In `app_task_trace.c`, task check-in timestamps are written by individual worker threads and read asynchronously by the watchdog thread without locks to maintain non-blocking execution.
  
  When a fast-cadence task updated `lastCheckInTimeMs` just as the watchdog sampled its own `now` timestamp, the worker's timestamp could land fractionally ahead of the watchdog's timestamp due to tick boundaries (`lastCheckInTimeMs > now`). The calculation `uint32_t elapsed = now - lastCheckInTimeMs` underflowed to `~0xFFFFFFF0` (~49 days), tripping the liveness threshold.
- **Resolution & Verification**:
  Added an underflow guard: if `lastCheckInTimeMs >= now`, the elapsed duration is treated as `0 ms` (just checked in). False warnings were completely eliminated.

---

### Case 08: Watchdog Blind Spots Caused by Unbounded Blocking Calls

- **Component / Subsystem**: Task supervision and synchronization interfaces (`app/include/osal.h`)
- **Target(s) Affected**: All targets
- **Symptom & Discovery Context**:
  If a hardware fault occurred on the USB bus (e.g., flash drive pulled during transfer), threads waiting on the device could hang forever without the watchdog detecting the stall.
- **Root Cause Analysis**:
  Several synchronization points originally used infinite timeouts (`0xFFFFFFFF` / `OSAL_WAIT_FOREVER`). Specifically, `osal_mutex_lock()` had no timeout parameter on some OSAL backends, and `storage_service.c` waited indefinitely on an empty queue. 
  
  Because an idle storage service was legitimately blocked on the queue, the watchdog had no way to distinguish between an idle worker waiting for requests and a thread deadlocked on a stuck peripheral.
- **Resolution & Verification**:
  1. Updated `osal_mutex_lock(handle, timeoutMs)` across all four targets to accept a bounded millisecond timeout.
  2. Bounded the queue wait in `storage_service.c` to 1000ms, waking periodically to record an idle check-in checkpoint before sleeping again.
  3. Added timeout handling to `usbHostLock()` in `app/src/usbh_msc_disk.c`.

---

### Case 09: RTC Time Source Register Coherency Race

- **Component / Subsystem**: Hardware RTC adapter (`targets/freertos/pal/src/rtc_time_source.c`)
- **Target(s) Affected**: FreeRTOS (`targets/freertos/`)
- **Symptom & Discovery Context**:
  Logging timestamps intermittently exhibited erratic jumps (e.g., jumping forward or backward by hours or days for a single log line).
- **Root Cause Analysis**:
  The STM32 RTC stores time and date in separate BCD shadow registers (`RTC_TR` and `RTC_DR`). Reading `RTC_TR` unlocks the shadow registers, but if an asynchronous rollover (such as a midnight transition) occurs between reading `RTC_TR` and `RTC_DR`, the date and time registers become mutually inconsistent.
- **Resolution & Verification**:
  Implemented a double-read coherency loop in `rtc_time_source.c`. The driver reads `TR` and `DR`, reads them a second time, and verifies that both samples match before calculating epoch milliseconds.

---

### Case 10: ThreadX Self-Termination vs Deletion Constraint

- **Component / Subsystem**: Thread lifecycle management (`targets/threadx/osal/src/osal.c`)
- **Target(s) Affected**: Azure RTOS ThreadX (`targets/threadx/`)
- **Symptom & Discovery Context**:
  Attempting to execute cooperative task teardown on ThreadX caused an immediate kernel return error (`TX_DELETE_ERROR`).
- **Root Cause Analysis**:
  In ThreadX, `tx_thread_delete()` is strictly forbidden on a thread that is currently executing or in a ready state. Calling `tx_thread_delete(&thread)` from within that thread's own execution context violates kernel state invariants.
- **Resolution & Verification**:
  In `osal_task_exit()` for ThreadX, the implementation calls `tx_thread_terminate(&thread)` on self. Terminating the thread removes it from the scheduling list and halts execution cleanly without requiring immediate deletion.

---

### Case 11: POSIX pthread_exit() Memory Leak in Host OSAL

- **Component / Subsystem**: Workstation OSAL implementation (`targets/host/osal/src/osal.c`)
- **Target(s) Affected**: Native POSIX Host (`targets/host/`)
- **Symptom & Discovery Context**:
  Running the host test suite under Valgrind or AddressSanitizer (`-fsanitize=address`) reported memory leaks upon thread termination.
- **Root Cause Analysis**:
  On the host target, `osal_task_create()` allocates an internal tracking struct on the heap. When a task voluntarily exited via `osal_task_exit()` (`pthread_exit(NULL)`), the thread stack unwound without freeing its heap-allocated metadata.
- **Resolution & Verification**:
  Implemented a POSIX thread-specific data key (`pthread_key_create`) with a registered destructor callback. When `pthread_exit()` executes, POSIX automatically invokes the destructor, releasing the tracking structure without memory leaks.

---

### Case 12: Silent Flash Failure via Missing J-Link Verification

- **Component / Subsystem**: Hardware flashing automation (`build.py`)
- **Target(s) Affected**: FreeRTOS, ThreadX, and Zephyr
- **Symptom & Discovery Context**:
  `build.py --flash` reported successful programming, but the board continued executing the previous firmware image.
- **Root Cause Analysis**:
  The generated J-Link command script invoked `loadbin <binary> 0x08000000`, followed by `r` (reset) and `g` (go). If flash write protection was active, or if target voltage dipped causing write errors, `JLinkExe` returned exit code 0 despite the failure.
- **Resolution & Verification**:
  Appended `verifybin <binary> 0x08000000` after `loadbin` in `_jlink_flash()`, and added `-ExitOnError 1` to the `JLinkExe` invocation flags. Any flash verification mismatch now immediately halts the script and raises a build error.

---

### Case 13: Direct RTOS API Leak in Application Task

- **Component / Subsystem**: Blinky diagnostic heartbeat (`app/src/blinky_task.c`)
- **Target(s) Affected**: ThreadX and Zephyr targets
- **Symptom & Discovery Context**:
  During early development, `blinky_task.c` compiled on FreeRTOS but failed with an unresolved symbol error when compiling for ThreadX or Zephyr.
- **Root Cause Analysis**:
  `blinky_task.c` had been created on the FreeRTOS target and directly invoked CMSIS-RTOS's `osDelay()`. This violated the architectural mandate that code under `app/` must never include or call concrete OS headers.
- **Resolution & Verification**:
  Replaced `osDelay()` with `osal_delay_ms()`. Added a pre-commit check to ensure files in `app/` only include headers from `app/include/`.

---

### Case 14: Non-Uniform Hardware Indication on ThreadX

- **Component / Subsystem**: Target board initialization and LED driver (`targets/threadx/pal/src/board_led.c`)
- **Target(s) Affected**: Azure RTOS ThreadX (`targets/threadx/`)
- **Symptom & Discovery Context**:
  ThreadX exhibited a distinct "airplane strobe" (double flash pause) on the user LED that was absent on other targets, and did not respond to cooperative stop commands.
- **Root Cause Analysis**:
  ThreadX had bypassed `LedDevice` and `blinky_task.c` entirely, spawning a proprietary `heartbeat_thread_entry` task that toggled PC13 via raw registers. This introduced target divergence and prevented the watchdog from supervising the blinky thread.
- **Resolution & Verification**:
  Removed the bespoke heartbeat task. Implemented `board_led.c` adhering to the standard `LedDevice` interface (`on`, `off`, `toggle`) and registered the shared `blinky_task.c` into `AppThreadRegistry`, achieving visual and structural parity across all targets.
