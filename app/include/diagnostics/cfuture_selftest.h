#ifndef CFUTURE_SELFTEST_H
#define CFUTURE_SELFTEST_H

/* Storage-free on-target self-test of libcfuture: timeout accuracy against the OSAL clock,
 * cross-task delivery, fulfilment from a real interrupt, stale-handle rejection and cancel.
 * Runs as two registry tasks and logs one "cfst <test> PASS|FAIL: ..." line per check. */

#include "cfuture.h"
#include "core/app_threads.h"
#include "core/logger.h"

#include <stdbool.h>

typedef void (*CfutureSelfTestIsrHandler)(void *context);

/* Target-provided: makes `handler(context)` run in real interrupt context, soon.
 * NULL on targets that cannot (the ISR check is then reported as SKIP). */
typedef void (*CfutureSelfTestRaiseIrqFn)(CfutureSelfTestIsrHandler handler, void *context);

bool cfutureSelfTestRegister(AppThreadRegistry *registry, Logger *logger, const cfuture_sync_ops_t *syncOps,
                             CfutureSelfTestRaiseIrqFn raiseIrq) __attribute__((warn_unused_result));

#endif /* CFUTURE_SELFTEST_H */
