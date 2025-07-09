#pragma once

#include "logger.h"
#include "osal.h"

#include "cfuture.h"

/* Requester tasks (T_A-style) demonstrating libcfuture's three concurrency
 * traps against storage_service.c's Servicer task (T_S), entirely through
 * osal.h + storage_protocol.h + cfuture.h - never a concrete OS/peripheral
 * API - so this file is identical whether the linked OSAL/PalStorage pair
 * is host or FreeRTOS. Each scenario logs one PASS/FAIL EVENT line per run
 * via the injected Logger and then repeats forever on a fixed cadence, so a
 * board/host run keeps re-proving all four properties for as long as it's
 * up rather than proving them once and going quiet. */

/* Shared by every Requester task spawned by storage_demo.c - one instance,
 * safe to point every task's context at, because none of its fields are
 * ever mutated after storage_demo.c wires it up (queue/pool/logger are
 * read-only shared handles). */
typedef struct ClientTaskConfig
{
    OsalQueueHandle queue; /* posts StorageRequest items to storage_service.c's T_S */
    cfuture_pool_t *pool;  /* shared promise/future pool, see storage_demo.c */
    Logger *logger;
} ClientTaskConfig;

/* Scenario 1 - happy path: write a block, read it back, verify the round
 * trip. Matches osal.h's OsalTaskEntryFn. */
void clientTaskHappyPathEntry(void *context);

/* Scenario 2 - queue-timeout cancellation: gives up on a write before T_S
 * even dequeues it (T_S is kept busy on an occupying request first, so the
 * timing is deterministic, not a race) and confirms the write never lands.
 * Matches osal.h's OsalTaskEntryFn. */
void clientTaskQueueTimeoutEntry(void *context);

/* Scenario 3 - late-completion discard: gives up on a request after T_S has
 * already committed to servicing it (using storage_service.h's
 * kStorageDemoSlowBlockId hook to make T_S's in-flight window deterministic)
 * and confirms the late completion is safely discarded, never touching the
 * caller's already-returned result buffer. Matches osal.h's
 * OsalTaskEntryFn. */
void clientTaskLateCompletionEntry(void *context);

/* Scenario 4 - ABA slot isolation: lets a claimed slot time out while it is
 * still sitting unprocessed in T_S's queue (same occupying-request trick as
 * scenario 2), then claims a second slot while the first is still
 * allocated-but-stale and confirms the second requester gets a genuinely
 * different, fully independent slot. Matches osal.h's OsalTaskEntryFn. */
void clientTaskAbaIsolationEntry(void *context);

/* Total number of scenario FAIL results logged so far, across all four
 * scenarios, since boot. A caller (host/main.c) can poll this after letting
 * the scenarios run for a while to turn "did anything fail" into an actual
 * automated pass/fail signal instead of a log line nobody's reading. */
uint32_t clientTasksFailureCount(void);
