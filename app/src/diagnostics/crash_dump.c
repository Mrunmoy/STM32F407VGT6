#include "crash_dump.h"

#include "app_task_trace.h"
#include "logger.h"
#include "osal.h"

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdio.h>

enum
{
    kExcFrameR0 = 0,
    kExcFrameR1 = 1,
    kExcFrameR2 = 2,
    kExcFrameR3 = 3,
    kExcFrameR12 = 4,
    kExcFrameLr = 5,
    kExcFramePc = 6,
    kExcFramePsr = 7,

    /* HFSR */
    kHfsrForced = (1UL << 30),
    kHfsrVecttbl = (1UL << 1),

    /* CFSR - MemManage (bits 0-7) */
    kMmfsrIaccviol = (1UL << 0),
    kMmfsrDaccviol = (1UL << 1),
    kMmfsrMmarvalid = (1UL << 7),

    /* CFSR - BusFault (bits 8-15) */
    kBfsrIbuserr = (1UL << 8),
    kBfsrPreciserr = (1UL << 9),
    kBfsrImpreciserr = (1UL << 10),
    kBfsrBfarvalid = (1UL << 15),

    /* CFSR - UsageFault (bits 16-31) */
    kUfsrUndefinstr = (1UL << 16),
    kUfsrInvstate = (1UL << 17),
    kUfsrInvpc = (1UL << 18),
    kUfsrNocp = (1UL << 19),
    kUfsrUnaligned = (1UL << 24),
    kUfsrDivbyzero = (1UL << 25),

    /* This board's two real clock states (see each target's stm32f407.ioc /
     * SystemClock_Config()): HSI at reset (16 MHz, AHB/APB2 prescalers at
     * their /1 reset default) before the PLL locks, or the verified 168 MHz
     * SYSCLK / 84 MHz APB2 once it has. See crashDumpApb2ClockHz() below for
     * why this is read from a live hardware register instead of the CMSIS
     * SystemCoreClock global. */
    kApb2ClockHsiHz = 16000000UL,
    kApb2ClockPllLockedHz = 84000000UL,
    kUartBaud = 115200UL,

    kFaultBlinkCount = 10,

    /* Bounds crashDumpRawUartPuts()'s TXE/TC busy-waits so a wedged USART1
     * (clock/GPIO state corrupted by whatever caused the fault, or the
     * USB-TTL adapter unplugged at the wrong moment) can't hang this path
     * forever - crashDumpHalt()'s LED blink + reset must still be reached.
     * Not time-calibrated (no timer can be trusted here) - just large enough
     * that it never trips during genuinely-working UART transmission at any
     * clock this board runs. */
    kUartSpinLimit = 1000000UL,

    /* SRAM bounds for every target this project ships (128 KB at
     * 0x20000000, all STM32F407V/ZGT6) - crashDumpCapture() below refuses
     * to dereference a stack-frame pointer outside this range instead of
     * blindly reading it. */
    kSramBase = 0x20000000UL,
    kSramSizeBytes = 128UL * 1024UL,
};

/* ── Raw, HAL/RTOS-independent UART (USART1, PA9=TX/PA10=RX) ────────────── */

static volatile bool s_uartReady;

/* RCC->CFGR bits [3:2] (SWS) report the ACTUALLY active system clock source
 * right now - a live hardware status register, not a software-maintained
 * variable that can go stale. Fixes two real gaps a single hardcoded
 * constant had: a fault during freertos/threadx's own early-boot window
 * before SystemClock_Config() completes (APB2 is genuinely still 16 MHz
 * HSI then - assuming PLL-locked unconditionally garbled exactly the
 * diagnostic output this feature exists to produce), and Zephyr's own
 * SystemCoreClock staying stale at 16 MHz for its entire runtime (its
 * clock_control driver programs the real PLL correctly but never updates
 * that separate CMSIS bookkeeping variable - confirmed by reading its
 * soc_early_init_hook()). Reading the hardware switch-status bits directly
 * is correct and safe on all three embedded targets either way. */
static uint32_t crashDumpApb2ClockHz(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS_Msk) == RCC_CFGR_SWS_PLL)
    {
        return (uint32_t)kApb2ClockPllLockedHz;
    }

    return (uint32_t)kApb2ClockHsiHz;
}

static void crashDumpRawUartInit(void)
{
    if (s_uartReady)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpioInit);

    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    USART1->BRR = (uint16_t)(crashDumpApb2ClockHz() / (uint32_t)kUartBaud);
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    s_uartReady = true;
}

static void crashDumpRawUartPuts(const char *str)
{
    crashDumpRawUartInit();

    while (*str != '\0')
    {
        uint32_t spins = 0;
        while (((USART1->SR & USART_SR_TXE) == 0) && (spins < (uint32_t)kUartSpinLimit))
        {
            spins++;
        }
        if (spins >= (uint32_t)kUartSpinLimit)
        {
            return; /* USART wedged - give up on this line, let the caller's
                      * halt/reset path still run instead of hanging here. */
        }
        USART1->DR = (uint32_t)*str++;
    }

    uint32_t spins = 0;
    while (((USART1->SR & USART_SR_TC) == 0) && (spins < (uint32_t)kUartSpinLimit))
    {
        spins++;
    }
}

static void crashDumpPrintHex(const char *prefix, uint32_t value)
{
    static const char kHexDigits[] = "0123456789ABCDEF";
    char buf[11];

    crashDumpRawUartPuts(prefix);

    for (int32_t i = 7; i >= 0; i--)
    {
        buf[i] = kHexDigits[value & 0xFU];
        value >>= 4;
    }
    buf[8] = '\r';
    buf[9] = '\n';
    buf[10] = '\0';

    crashDumpRawUartPuts(buf);
}

/* ── Capture ──────────────────────────────────────────────────────────── */

/* Reject a stack-frame pointer outside real SRAM (or not word-aligned)
 * instead of blindly dereferencing it. A severely corrupted MSP/PSP - e.g.
 * a stack overflow that walked past its allocated region - is exactly the
 * kind of bug most likely to be what tripped the original fault; reading
 * 8 words from an invalid address would raise a second fault while already
 * inside the highest-priority fault handler, which the core cannot recover
 * from (PM0214 Section 2.4.4 "Lockup") - crashDumpHalt()'s own reset would
 * then never run. Skipping the read here means the report just shows zeros
 * for the register dump instead, and still reaches the LED blink + reset
 * (backstopped by the watchdog crashDumpEarlyInit() arms, in case some
 * other cause still re-faults downstream). */
static bool crashDumpStackFrameValid(const uint32_t *stackFrame)
{
    uintptr_t address = (uintptr_t)stackFrame;

    if ((address & 0x3U) != 0U)
    {
        return false;
    }

    if ((address < (uintptr_t)kSramBase) || (address > (uintptr_t)(kSramBase + kSramSizeBytes - (8U * sizeof(uint32_t)))))
    {
        return false;
    }

    return true;
}

void crashDumpCapture(const uint32_t *stackFrame, CrashContext *outContext)
{
    if ((stackFrame != NULL) && !crashDumpStackFrameValid(stackFrame))
    {
        stackFrame = NULL;
    }

    if (stackFrame != NULL)
    {
        outContext->r0 = stackFrame[kExcFrameR0];
        outContext->r1 = stackFrame[kExcFrameR1];
        outContext->r2 = stackFrame[kExcFrameR2];
        outContext->r3 = stackFrame[kExcFrameR3];
        outContext->r12 = stackFrame[kExcFrameR12];
        outContext->lr = stackFrame[kExcFrameLr];
        outContext->pc = stackFrame[kExcFramePc];
        outContext->psr = stackFrame[kExcFramePsr];
        outContext->sp = (uint32_t)stackFrame;
    }
    else
    {
        outContext->r0 = 0U;
        outContext->r1 = 0U;
        outContext->r2 = 0U;
        outContext->r3 = 0U;
        outContext->r12 = 0U;
        outContext->lr = 0U;
        outContext->pc = 0U;
        outContext->psr = 0U;
        outContext->sp = 0U;
    }

    outContext->hfsr = SCB->HFSR;
    outContext->cfsr = SCB->CFSR;
    outContext->mmfar = SCB->MMFAR;
    outContext->bfar = SCB->BFAR;
}

/* ── Report ───────────────────────────────────────────────────────────── */

void crashDumpReport(const CrashContext *context)
{
    crashDumpRawUartPuts("\r\n\r\n========================================\r\n");
    crashDumpRawUartPuts("          HARD FAULT DETECTED\r\n");
    crashDumpRawUartPuts("========================================\r\n\r\n");

    crashDumpPrintHex("Fault PC  = 0x", context->pc);
    crashDumpPrintHex("Fault LR  = 0x", context->lr);
    crashDumpRawUartPuts("\r\n");

    crashDumpRawUartPuts("--- Register Dump ---\r\n");
    crashDumpPrintHex("R0  = 0x", context->r0);
    crashDumpPrintHex("R1  = 0x", context->r1);
    crashDumpPrintHex("R2  = 0x", context->r2);
    crashDumpPrintHex("R3  = 0x", context->r3);
    crashDumpPrintHex("R12 = 0x", context->r12);
    crashDumpPrintHex("LR  = 0x", context->lr);
    crashDumpPrintHex("PC  = 0x", context->pc);
    crashDumpPrintHex("PSR = 0x", context->psr);
    crashDumpPrintHex("SP  = 0x", context->sp);
    crashDumpRawUartPuts("\r\n");

    crashDumpRawUartPuts("--- Fault Status ---\r\n");
    crashDumpPrintHex("HFSR  = 0x", context->hfsr);
    crashDumpPrintHex("CFSR  = 0x", context->cfsr);

    if ((context->hfsr & kHfsrForced) != 0U)
    {
        crashDumpRawUartPuts("  -> FORCED: Escalated fault\r\n");
    }
    if ((context->hfsr & kHfsrVecttbl) != 0U)
    {
        crashDumpRawUartPuts("  -> VECTTBL: Vector table read error\r\n");
    }

    if ((context->cfsr & 0xFFU) != 0U)
    {
        crashDumpRawUartPuts("MemManage Fault:\r\n");
        if ((context->cfsr & kMmfsrIaccviol) != 0U)
        {
            crashDumpRawUartPuts("  -> Instruction access violation\r\n");
        }
        if ((context->cfsr & kMmfsrDaccviol) != 0U)
        {
            crashDumpRawUartPuts("  -> Data access violation\r\n");
        }
        if ((context->cfsr & kMmfsrMmarvalid) != 0U)
        {
            crashDumpPrintHex("  -> MMFAR = 0x", context->mmfar);
        }
    }

    if ((context->cfsr & 0xFF00U) != 0U)
    {
        crashDumpRawUartPuts("BusFault:\r\n");
        if ((context->cfsr & kBfsrIbuserr) != 0U)
        {
            crashDumpRawUartPuts("  -> Instruction bus error\r\n");
        }
        if ((context->cfsr & kBfsrPreciserr) != 0U)
        {
            crashDumpRawUartPuts("  -> Precise data bus error\r\n");
        }
        if ((context->cfsr & kBfsrImpreciserr) != 0U)
        {
            crashDumpRawUartPuts("  -> Imprecise data bus error\r\n");
        }
        if ((context->cfsr & kBfsrBfarvalid) != 0U)
        {
            crashDumpPrintHex("  -> BFAR = 0x", context->bfar);
        }
    }

    if ((context->cfsr & 0xFFFF0000U) != 0U)
    {
        crashDumpRawUartPuts("UsageFault:\r\n");
        if ((context->cfsr & kUfsrUndefinstr) != 0U)
        {
            crashDumpRawUartPuts("  -> Undefined instruction\r\n");
        }
        if ((context->cfsr & kUfsrInvstate) != 0U)
        {
            crashDumpRawUartPuts("  -> Invalid state (EPSR.T)\r\n");
        }
        if ((context->cfsr & kUfsrInvpc) != 0U)
        {
            crashDumpRawUartPuts("  -> Invalid PC (EXC_RETURN)\r\n");
        }
        if ((context->cfsr & kUfsrNocp) != 0U)
        {
            crashDumpRawUartPuts("  -> Coprocessor error\r\n");
        }
        if ((context->cfsr & kUfsrUnaligned) != 0U)
        {
            crashDumpRawUartPuts("  -> Unaligned access\r\n");
        }
        if ((context->cfsr & kUfsrDivbyzero) != 0U)
        {
            crashDumpRawUartPuts("  -> Divide by zero\r\n");
        }
    }

    crashDumpRawUartPuts("\r\n========================================\r\n");
}

/* ── Halt ─────────────────────────────────────────────────────────────── */

static void crashDumpBlinkDelay(void)
{
    for (volatile uint32_t i = 0; i < 168U; i++)
    {
        for (volatile uint32_t j = 0; j < 100000U; j++)
        {
            __NOP();
        }
    }
}

void crashDumpHalt(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin = GPIO_PIN_13;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpioInit);

    crashDumpRawUartPuts("System will reset shortly...\r\n");

    for (uint32_t i = 0; i < kFaultBlinkCount; i++)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        crashDumpBlinkDelay();
    }

    NVIC_SystemReset();

    for (;;)
    {
        /* Unreachable - NVIC_SystemReset() does not return - but the
         * function is declared noreturn, so keep the compiler happy about
         * that contract even if a fault ever hit before the reset lands. */
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */

void crashDumpFaultEntry(const uint32_t *stackFrame)
{
    CrashContext context;
    crashDumpCapture(stackFrame, &context);
    crashDumpReport(&context);
    crashDumpHalt();
}

/* ── Test triggers ────────────────────────────────────────────────────── */

void crashDumpTriggerTestFault(uint32_t faultType)
{
    switch (faultType)
    {
        case 0:
        {
            crashDumpRawUartPuts("Triggering divide by zero...\r\n");
            SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
            volatile uint32_t denominator = 0;
            volatile uint32_t result = 1U / denominator;
            (void)result;
            break;
        }

        case 1:
        {
            crashDumpRawUartPuts("Triggering invalid memory write...\r\n");
            volatile uint32_t *badAddress = (volatile uint32_t *)0xCCCCCCCCU;
            *badAddress = 0xDEADBEEFU;
            break;
        }

        case 2:
            crashDumpRawUartPuts("Triggering undefined instruction...\r\n");
            __asm volatile(".word 0xFFFFFFFF");
            break;

        default:
            crashDumpRawUartPuts("Unknown fault type\r\n");
            break;
    }
}

/* ── Fault-safety net: sub-fault routing + independent watchdog ─────────── */

enum
{
    /* IWDG_PR prescaler encoding (RM0090): 0b110 = LSI/256, the maximum
     * divider - paired with the maximum 12-bit reload (4095) for the
     * longest, most conservative timeout this peripheral can express
     * (roughly 30s at LSI's nominal ~32 kHz, safely bounded either way at
     * LSI's real per-chip tolerance - RM0090 Section 21). Deliberately not
     * tuned tight: the only job here is "eventually force a reset out of a
     * genuine lockup/hang", not a fast-reacting deadline, and an overly
     * tight watchdog risks spurious resets during normal operation instead. */
    kIwdgPrescalerDiv256 = 0x06UL,
    kIwdgReloadMax = 0x0FFFUL,

    kIwdgKeyEnableAccess = 0x5555UL,
    kIwdgKeyRefresh = 0xAAAAUL,
    kIwdgKeyStart = 0xCCCCUL,

    /* Refresh well inside the watchdog's own multi-second timeout - Blinky
     * itself toggles every 500ms (app.c), so this leaves a very wide
     * margin without depending on Blinky specifically (this task is its
     * own independent thread, registered like any other). */
    kWatchdogRefreshIntervalMs = 1000U,

    /* A task counts as unhealthy once this long has passed since its last
     * check-in (app_task_trace.h - updated on every loop start/end and
     * every checkpoint). Sized above the worst-case legitimate gap between
     * check-ins anywhere in this app: client_tasks.c's occupy-drain wait
     * (kClientTaskOccupyDrainTimeoutMs, 20s) is the longest single stretch
     * any task's loop can legitimately run without reporting in, and
     * usb_host.c's lock timeout (kUsbHostLockTimeoutMs, 12s) is the next
     * longest. 25s leaves a few seconds of margin above that 20s bound
     * while still resolving well before the IWDG's own ~32.7s hardware
     * timeout (kIwdgReloadMax/(LSI/256), see crashDumpEarlyInit()) - so an
     * unhealthy task gets logged at least once before the hardware
     * watchdog would otherwise reset the board with no diagnostic at all. */
    kWatchdogHealthTimeoutMs = 25000U,

    /* How often (in refresh intervals) the watchdog task logs a one-line
     * profiling summary per task, even when everything is healthy - so
     * profiling data (cadence/turnaround) is visible during normal
     * operation, not just when something goes wrong. 10 * 1000ms = 10s. */
    kWatchdogProfileDumpIntervalTicks = 10U,

    kWatchdogLogMessageLength = 112U,
};

static void logTaskHealth(Logger *logger, const AppTaskTrace *trace, uint32_t sinceLastCheckInMs)
{
    char message[kWatchdogLogMessageLength];
    (void)snprintf(message, sizeof(message), "watchdog: %s UNHEALTHY - %lu ms since check-in, last checkpoint '%s'",
                   trace->taskName, (unsigned long)sinceLastCheckInMs,
                   (trace->lastCheckpoint != NULL) ? trace->lastCheckpoint : "(none)");
    loggerLog(logger, kLogLevelError, message);
}

static void logTaskProfile(Logger *logger, const AppTaskTrace *trace)
{
    char message[kWatchdogLogMessageLength];

    if (trace->stopped)
    {
        (void)snprintf(message, sizeof(message), "trace: %s STOPPED", trace->taskName);
    }
    else
    {
        (void)snprintf(message, sizeof(message), "trace: %s iter=%lu turnaround=%lums cadence=%lums up=%lums",
                       trace->taskName, (unsigned long)trace->iterationCount, (unsigned long)trace->lastLoopDurationMs,
                       (unsigned long)trace->lastCadenceMs, (unsigned long)trace->registeredTimeMs);
    }

    loggerLog(logger, kLogLevelEvent, message);
}

void crashDumpEarlyInit(void)
{
    /* Route MemManage/BusFault/UsageFault to their own dedicated handlers
     * instead of every fault (SCB->SHCSR resets to 0, all three disabled)
     * universally escalating to HardFault_Handler with HFSR.FORCED set -
     * PM0214 Section 2.4.2 "Fault escalation and hard faults" / Section
     * 4.4.9 "SHCSR". The report content is unaffected either way (CFSR is
     * read fresh regardless of which vector fired) - this only affects
     * which of the four hand-written trampolines actually runs. */
    SCB->SHCSR |= (uint32_t)(SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk);

    /* Arm the independent watchdog. IWDG runs off its own LSI clock and
     * keeps counting down even if the CPU enters a Lockup state (PM0214
     * Section 2.4.4) - the one fault-handler-re-faults scenario this file's
     * own code cannot recover from - RM0090 Section 21.1: "clocked by its
     * own dedicated low-speed clock (LSI) and thus stays active even if the
     * main clock fails". */
    IWDG->KR = (uint32_t)kIwdgKeyEnableAccess;
    IWDG->PR = (uint32_t)kIwdgPrescalerDiv256;
    IWDG->RLR = (uint32_t)kIwdgReloadMax;
    IWDG->KR = (uint32_t)kIwdgKeyRefresh;
    IWDG->KR = (uint32_t)kIwdgKeyStart;
}

void crashDumpWatchdogTaskEntry(void *context)
{
    static const char kTaskName[] = "Watchdog";

    Logger *logger = (Logger *)context;
    uint32_t tick = 0U;

    for (;;)
    {
        appTaskTraceLoopStart(kTaskName);

        /* Stopping this particular task is supported for architectural
         * consistency (every task can be asked to stop the same way), but
         * be aware of what it actually does: the STM32F4 IWDG cannot be
         * disabled once started (RM0090 Section 21 documents no
         * disable/stop command in its register interface, only
         * Reload/Prescaler/Key) - stopping this task just means nothing
         * refreshes it anymore, so the hardware watchdog will still reset
         * the board on its own ~32.7s schedule regardless. That is a real
         * hardware property, not a shortcut this code is taking. */
        if (appTaskTraceShouldStop(kTaskName))
        {
            break;
        }

        /* Only kick the hardware watchdog once every registered task has
         * checked in recently - a task that has gone silent (locked up
         * somewhere with no timeout, or stuck looping without reaching a
         * checkpoint) leaves the IWDG un-refreshed instead, so a genuine
         * lockup still reaches the hardware reset (crashDumpEarlyInit())
         * rather than being masked by an unconditional kick. This is
         * diagnostic-only for now - no restart/teardown is attempted here,
         * the hardware watchdog is the deliberate backstop. */
        appTaskTraceCheckpoint(kTaskName, "checking task health");

        bool allHealthy = true;
        uint32_t now = osal_get_time_ms();
        uint32_t count = appTaskTraceCount();

        for (uint32_t i = 0U; i < count; i++)
        {
            AppTaskTrace trace;
            if (!appTaskTraceGetByIndex(i, &trace))
            {
                continue;
            }

            /* A deliberately-stopped task (appTaskTraceMarkStopped(), called
             * right before it called osal_task_exit()) is expected to never
             * check in again - that is not the same thing as being stuck,
             * so it must not count against allHealthy or get logged as
             * unhealthy. */
            if (trace.stopped)
            {
                continue;
            }

            /* trace.lastCheckInTimeMs can legitimately land a few ms ahead
             * of this loop's own 'now' sample - appTaskTraceGetByIndex()
             * copies the record without any lock (deliberately - see
             * app_task_trace.h), so a fast-cadence task (e.g.
             * UsbHostProcess, ~10ms) can check in again between 'now' being
             * sampled above and this record being read. That is never a
             * real staleness problem (the task just checked in), but the
             * plain subtraction below would otherwise underflow to
             * approximately UINT32_MAX and falsely report it as unhealthy -
             * confirmed on real hardware before this guard was added.
             * Clamp to zero instead of treating that race as elapsed time. */
            uint32_t sinceLastCheckInMs = (now >= trace.lastCheckInTimeMs) ? (now - trace.lastCheckInTimeMs) : 0U;
            if (sinceLastCheckInMs > (uint32_t)kWatchdogHealthTimeoutMs)
            {
                allHealthy = false;
                if (logger != NULL)
                {
                    logTaskHealth(logger, &trace, sinceLastCheckInMs);
                }
            }
        }

        if (allHealthy)
        {
            appTaskTraceCheckpoint(kTaskName, "kicking IWDG");
            IWDG->KR = (uint32_t)kIwdgKeyRefresh;
        }

        if ((logger != NULL) && ((tick % (uint32_t)kWatchdogProfileDumpIntervalTicks) == 0U))
        {
            for (uint32_t i = 0U; i < count; i++)
            {
                AppTaskTrace trace;
                if (appTaskTraceGetByIndex(i, &trace))
                {
                    logTaskProfile(logger, &trace);
                }
            }
        }

        appTaskTraceLoopEnd(kTaskName);
        tick++;
        osal_delay_ms((uint32_t)kWatchdogRefreshIntervalMs);
    }

    appTaskTraceMarkStopped(kTaskName);
    osal_task_exit();
}
