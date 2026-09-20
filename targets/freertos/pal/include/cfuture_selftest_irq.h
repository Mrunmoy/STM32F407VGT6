#ifndef CFUTURE_SELFTEST_IRQ_H
#define CFUTURE_SELFTEST_IRQ_H

#include "diagnostics/cfuture_selftest.h"

/* Runs handler(context) in real interrupt context by software-pending an NVIC line this
 * application does not otherwise use (CAN2 SCE). */
void cfuture_selftest_raise_irq(CfutureSelfTestIsrHandler handler, void *context);

#endif /* CFUTURE_SELFTEST_IRQ_H */
