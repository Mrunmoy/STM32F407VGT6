/**
 * @file crash_dump.h
 * @brief Cortex-M fault capture/report/recover, shared across every embedded
 *        target (host has no hardware faults in this sense - never compiled
 *        into the host target, same precedent as usb_host.h).
 *
 * Every embedded target's fault entry point - freertos/threadx's naked-asm
 * HardFault/MemManage/BusFault/UsageFault trampolines (Core/Src/
 * stm32f4xx_it.c), or Zephyr's own k_sys_fatal_error_handler() override
 * (os_glue/) - ends up calling crashDumpFaultEntry() with the raw exception
 * stack frame. Everything from there on (reading fault status registers,
 * printing the dump, blinking the LED, resetting) is 100% identical code,
 * because it's genuinely identical hardware (same chip, same UART pins,
 * same LED pin) regardless of which RTOS is running - it deliberately talks
 * to raw registers instead of going through any OS/HAL abstraction, since
 * none of those can be trusted to still be in a sane state after a fault.
 */
#pragma once

#include <stdint.h>

/* Registers captured at the moment of a fault. */
typedef struct
{
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
    uint32_t sp;
    uint32_t hfsr;
    uint32_t cfsr;
    uint32_t mmfar;
    uint32_t bfar;
} CrashContext;

/* Reads r0-r3/r12/lr/pc/psr from a raw Cortex-M exception stack frame (8
 * words, standard ARMv7-M layout - what a HardFault_Handler-style naked
 * trampoline hands off in r0) plus the SCB fault status registers.
 * `stackFrame` may be NULL (nothing to read into r0-r3/r12/lr/pc/psr, e.g.
 * a fatal error that wasn't a CPU exception at all) - the SCB registers are
 * still read either way. */
void crashDumpCapture(const uint32_t *stackFrame, CrashContext *outContext);

/* Prints the full crash report - fault PC/LR, register dump, decoded fault
 * status - over a raw, HAL/RTOS-independent UART write. Safe to call from
 * fault/ISR context regardless of what state the rest of the system is in. */
void crashDumpReport(const CrashContext *context);

/* Blinks the onboard LED via raw GPIO (bypassing LedDevice on purpose - the
 * app's own DI object graph can't be trusted after a fault) for a few
 * seconds, then resets the system. Never returns. */
void crashDumpHalt(void) __attribute__((noreturn));

/* The one entry point every target's fault trampoline calls: capture,
 * report, halt, in that order. Never returns. */
void crashDumpFaultEntry(const uint32_t *stackFrame) __attribute__((noreturn));

/* Deliberately triggers one of a few fault types, for testing the above:
 *   0 = divide by zero
 *   1 = invalid memory write
 *   2 = undefined instruction (most reliable) */
void crashDumpTriggerTestFault(uint32_t faultType);

/* Enables this project's fault-safety net:
 *   - Routes MemManage/BusFault/UsageFault to their own dedicated handlers
 *     (SCB->SHCSR) instead of every fault escalating to HardFault_Handler.
 *   - Arms the independent watchdog (IWDG) as a hardware backstop. It runs
 *     off its own LSI clock and keeps counting down even if the CPU is in a
 *     Lockup state (PM0214 Section 2.4.4) - the one scenario this crash-dump
 *     code cannot recover from on its own (a second fault while already
 *     inside the fault handler). Refreshed by crashDumpWatchdogTaskEntry().
 * Call once, as the very first thing, from each embedded target's
 * composition root - before board_led_init(), before anything else. */
void crashDumpEarlyInit(void);

/* Refreshes the watchdog armed by crashDumpEarlyInit(). Register via
 * AppDependencies.watchdogTaskEntry so appRun() starts it like any other
 * thread - see crash_dump.c for the refresh interval and the watchdog's
 * timeout margin. Never returns. */
void crashDumpWatchdogTaskEntry(void *context) __attribute__((noreturn));
