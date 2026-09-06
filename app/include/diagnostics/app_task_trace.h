#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Lightweight, portable task supervision: every registered task reports its
 * own progress by name at a handful of points in its own loop - no task
 * ever reads another task's internal state (no stack walking, no reading
 * another RTOS thread's saved registers, none of which is exposed
 * identically, or sometimes at all, across FreeRTOS/ThreadX/Zephyr/host).
 * A task that hangs simply stops calling in; whoever's watching (the
 * watchdog, see crash_dump.c) reads the last thing that task reported
 * instead of trying to reconstruct it from the outside.
 *
 * This same per-task record also carries the other half of task lifecycle:
 * a one-way stop request a task polls for itself (appTaskTraceShouldStop())
 * and reports completion of (appTaskTraceMarkStopped()). This lives here
 * rather than in a second parallel-lookup module because it is keyed by the
 * exact same task name and populated at the exact same registration point -
 * two tables that must always agree with each other by convention are a
 * worse design than one table that cannot disagree with itself.
 *
 * This module only depends on osal_get_time_ms() - it works identically,
 * and usefully, on all four targets, not just the three with a hardware
 * watchdog.
 *
 * Each task is looked up by the exact same name string it was registered
 * under (OsalTaskConfig.name, app_threads.h) - a small linear scan over a
 * handful of entries, called at most once per loop iteration per task,
 * comfortably cheap. This avoids every task's own config struct needing a
 * new field just to carry a trace handle.
 *
 * No lock guards s_traces (app_task_trace.c) - deliberately: a reader (the
 * watchdog, via appTaskTraceGetByIndex()) can therefore observe a record
 * mid-update by its owning task. This is safe and self-correcting: each
 * field is written by exactly one task (stopRequested is the one exception -
 * see its own comment below), a torn read of a fast-moving record only ever
 * makes it look *more* recently checked-in than the instant the read began,
 * never less, and the very next read (one watchdog tick later) reflects
 * reality again. A reader comparing a timestamp from this struct against its
 * own separately-sampled "now" must still treat a lastCheckInTimeMs that
 * lands at or after "now" as "just checked in", not as a
 * negative/underflowed elapsed time - see crash_dump.c's watchdog. */

enum
{
    /* Matches app_threads.h's kAppThreadRegistryCapacity - one trace slot
     * per registerable task. */
    kAppTaskTraceCapacity = 8U,

    kAppTaskTraceCheckpointNone = 0,
};

typedef struct AppTaskTrace
{
    const char *taskName;       /* NULL if this slot is unused */
    uint32_t registeredTimeMs;  /* when appTaskTraceRegister() was called for this task */
    uint32_t lastCheckInTimeMs; /* updated by every call below - what the watchdog reads */
    uint32_t lastLoopStartMs;
    uint32_t lastLoopDurationMs; /* how long the most recently *completed* iteration took */
    uint32_t lastCadenceMs;      /* time between the start of that iteration and the one before it */
    uint32_t iterationCount;
    const char *lastCheckpoint; /* a string literal - not copied, not freed */

    /* Written by appTaskTraceRequestStop() (whoever wants this task to stop -
     * currently a test hook, eventually a restart supervisor), read by the
     * task itself (appTaskTraceShouldStop()) and by the watchdog. This is
     * the one field in this struct NOT owned by a single writer, but it is
     * still safe without a lock: it only ever transitions false -> true,
     * once, for the remaining lifetime of this boot (no un-request, no
     * restart yet - see osal.h's osal_task_exit()), so every reader either
     * sees the old value or the new one, never a torn/partial one. */
    volatile bool stopRequested;

    /* Written once by the task itself, via appTaskTraceMarkStopped(), right
     * before it calls osal_task_exit() - tells the watchdog to stop
     * expecting check-ins from this task at all (not "unhealthy", just
     * gone), see crash_dump.c. */
    bool stopped;
} AppTaskTrace;

/* Called once by app_threads.c's appThreadRegistryStartAll(), right after a
 * task is successfully created - not by the task itself, so a task that
 * hangs before ever running a single instruction (or during its own
 * one-time setup, before its main loop even begins) still has a trace
 * record from the moment it was scheduled to exist, not just from whenever
 * it first got around to calling in. */
void appTaskTraceRegister(const char *taskName);

/* Called by a task itself, once at the top of every loop iteration. */
void appTaskTraceLoopStart(const char *taskName);

/* Called by a task itself, once at the bottom of every loop iteration
 * (right before looping back to the top / going idle for the next
 * cadence). Computes this iteration's duration from the matching
 * appTaskTraceLoopStart() call. */
void appTaskTraceLoopEnd(const char *taskName);

/* Called by a task itself at any point worth being able to look back on -
 * "waiting for queue", "servicing write", "usb locked" - a breadcrumb, not
 * a log message. Pass a string literal; only the pointer is stored. */
void appTaskTraceCheckpoint(const char *taskName, const char *checkpoint);

/* For a supervisor/diagnostic reader (crash_dump.c's watchdog) to walk
 * every currently-registered task without needing to know their names in
 * advance - iterate index 0..appTaskTraceCount()-1. */
uint32_t appTaskTraceCount(void);
bool appTaskTraceGetByIndex(uint32_t index, AppTaskTrace *outTrace) __attribute__((warn_unused_result));

/* ── Lifecycle: the signaling mechanism to tear a task down ──────────────
 *
 * Deliberately a polled flag, not an OS-level event/semaphore that would
 * wake a task out of its blocking wait early: every task's own blocking
 * calls are already bounded (osal_mutex_lock/osal_queue_receive timeouts,
 * see usb_host.c/storage_service.c/client_tasks.c's own comments), so a
 * task already wakes up on its own, at worst, every few seconds - checking
 * this flag right then is simple, safe, and needs no extra per-task OS
 * object. Worst-case stop latency is therefore bounded by that task's own
 * longest legitimate loop iteration, not unbounded. */

/* Requests that the named task stop. Safe to call from any task (or, for
 * now, a temporary test hook - there is no restart supervisor yet). Has no
 * effect if the task doesn't exist or has already stopped. One-way: there
 * is no corresponding "cancel the request" call. */
void appTaskTraceRequestStop(const char *taskName);

/* Called by a task itself, typically right after appTaskTraceLoopStart(),
 * before doing any of that iteration's actual work - if this returns true,
 * the task should break out of its loop, clean up whatever it owns, call
 * appTaskTraceMarkStopped(), and then osal_task_exit(). Returns false (never
 * stop) if the task isn't found, so a typo in taskName fails open rather
 * than silently wedging a task that never checks a name it doesn't have. */
bool appTaskTraceShouldStop(const char *taskName) __attribute__((warn_unused_result));

/* Called by a task itself exactly once, after it has broken out of its loop
 * and released everything it owns, immediately before calling
 * osal_task_exit() - tells the watchdog (and anyone else reading this
 * trace) that this task is intentionally gone, not stuck. */
void appTaskTraceMarkStopped(const char *taskName);
