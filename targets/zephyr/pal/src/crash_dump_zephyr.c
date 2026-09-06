#include "crash_dump.h"

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

/* Zephyr owns its own fault vectors (arch/arm/core/cortex_m/fault.c already
 * does the MSP/PSP determination internally, unlike freertos/threadx where
 * this project's own naked-asm trampolines do it) and exposes exactly one
 * sanctioned override point for what happens next - same "use the RTOS's
 * own mechanism, don't fight it" reasoning as usb_host_irq.c's
 * IRQ_CONNECT() vs raw NVIC pokes. `esf` may be NULL for a fatal error that
 * wasn't a CPU exception at all (e.g. a stack check failure); crashDumpFaultEntry()
 * handles that by still reading the SCB fault status registers, matching
 * freertos/threadx's own handling of an all-zero stack frame. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    (void)reason;

    if (esf == NULL)
    {
        crashDumpFaultEntry(NULL);
        return; /* unreachable - crashDumpFaultEntry() never returns */
    }

    uint32_t stackFrame[8] = {
        esf->basic.r0,
        esf->basic.r1,
        esf->basic.r2,
        esf->basic.r3,
        esf->basic.r12,
        esf->basic.lr,
        esf->basic.pc,
        esf->basic.xpsr,
    };
    crashDumpFaultEntry(stackFrame);
}
