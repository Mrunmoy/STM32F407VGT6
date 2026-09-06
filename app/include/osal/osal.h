#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal OS abstraction sized to exactly what storage_service.c (the
 * Servicer task) and client_tasks.c (Requester tasks) need: create a task,
 * create a queue and send/receive fixed-size items through it, delay, and
 * read an elapsed-time clock. Nothing more general is exposed here.
 *
 * Unlike this project's other Core/App/ interfaces (LogSink, TimeSource,
 * LedDevice - a function-pointer-plus-context struct an app.c wires up at
 * runtime), this header is deliberately plain extern functions: exactly one
 * concrete implementation is linked per build target (osal_posix.c for the
 * host build, osal_freertos.c for the FreeRTOS/board build - matching how
 * build.py's --os selects a target), so there is nothing to inject or swap
 * at runtime, only at link time. storage_service.c/client_tasks.c call these
 * functions directly and stay identical across both targets.
 *
 * Timeout convention on every blocking call below (matches libcfuture's own
 * cfuture_wait_for convention, so callers don't have to remember two
 * different rules): timeoutMs == 0 means "poll, don't block"; timeoutMs ==
 * UINT32_MAX means "block until it happens" (a concrete OSAL may impose its
 * own defensive real deadline instead of a true infinite wait, exactly as
 * libcfuture's own POSIX adapter caps UINT32_MAX at 600000 ms - the contract
 * is "blocks until signaled/available or timeout", not "guaranteed forever"). */

/* ── Task creation ────────────────────────────────────────────────────── */

typedef void *OsalTaskHandle;

/* Task entry point. Signature matches CMSIS-OS2's osThreadFunc_t exactly so
 * the FreeRTOS OsalTaskEntryFn can be passed straight to osThreadNew() with
 * no wrapper. A task entry function runs its own loop for as long as it's
 * healthy and nobody has asked it to stop (see app_task_trace.h's
 * appTaskTraceShouldStop()) - it must never plainly `return` out of that
 * loop. To end itself gracefully, break out of the loop, clean up, then call
 * osal_task_exit() below (which does not return, ending the task properly
 * for whichever concrete OS is linked). Plainly returning from this function
 * is undefined behavior on FreeRTOS/ThreadX/Zephyr - only host's pthread
 * backend would tolerate it, and even there it skips the state bookkeeping
 * osal_task_exit()'s callers are expected to have already done. */
typedef void (*OsalTaskEntryFn)(void *context);

typedef enum OsalTaskPriority
{
    kOsalPriorityLow = 0,
    kOsalPriorityNormal = 1,
    kOsalPriorityHigh = 2,
} OsalTaskPriority;

typedef struct OsalTaskConfig
{
    const char *name;              /* short human-readable name, for logging/debuggers */
    OsalTaskEntryFn entry;
    void *context;                 /* passed to entry() verbatim */
    uint32_t stackSizeBytes;
    OsalTaskPriority priority;
} OsalTaskConfig;

/* Creates and starts a task. outHandle may be NULL if the caller never needs
 * to refer to the task again (storage_service.c/client_tasks.c tasks run
 * forever and typically don't). Returns false on failure (e.g. out of
 * RTOS-heap/thread slots) - callers should treat that as a boot-time fault,
 * not something to retry. */
bool osal_task_create(const OsalTaskConfig *config, OsalTaskHandle *outHandle) __attribute__((warn_unused_result));

/* Cleanly terminates the CALLING task - only ever call this on yourself, from
 * inside your own entry function, after you've already broken out of your
 * main loop, released anything you own (locks, open files, ...), and
 * recorded your own final state (see app_task_trace.h's
 * appTaskTraceMarkStopped()). Does not return - verified per target against
 * real vendor source/docs, not assumed: pthread_exit() on host (POSIX);
 * CMSIS-OS2's osThreadExit() on FreeRTOS (declared __NO_RETURN in
 * cmsis_os2.h); ThreadX's tx_thread_terminate() called on
 * tx_thread_identify() (confirmed against the vendored
 * tx_thread_terminate.c - terminating the calling thread suspends it and it
 * is never rescheduled, so control never returns to the caller); and
 * Zephyr's k_thread_abort() on k_current_get() (confirmed against the
 * vendored kernel/sched.c's own comment on z_thread_halt(): "aborting
 * _current will not return, obviously"). */
void osal_task_exit(void) __attribute__((noreturn));

/* ── Queue ────────────────────────────────────────────────────────────── */

typedef void *OsalQueueHandle;

/* Creates a queue holding up to itemCount items of itemSize bytes each
 * (storage_service.c/client_tasks.c pass sizeof(StorageRequest), see
 * storage_protocol.h). Items are copied in/out by value - never pass a
 * pointer to a stack-local StorageRequest through this queue, embed the
 * whole struct. Returns false on failure. */
bool osal_queue_create(uint32_t itemCount, size_t itemSize, OsalQueueHandle *outHandle) __attribute__((warn_unused_result));

/* Copies *item (itemSize bytes, per osal_queue_create) into the queue. Returns
 * true if the item was enqueued within timeoutMs, false on timeout (queue
 * stayed full). */
bool osal_queue_send(OsalQueueHandle queue, const void *item, uint32_t timeoutMs) __attribute__((warn_unused_result));

/* Copies one item out of the queue into *outItem (itemSize bytes). Returns
 * true if an item was dequeued within timeoutMs, false on timeout (queue
 * stayed empty). */
bool osal_queue_receive(OsalQueueHandle queue, void *outItem, uint32_t timeoutMs) __attribute__((warn_unused_result));

/* ── Mutex ────────────────────────────────────────────────────────────── */

typedef void *OsalMutexHandle;

/* Creates a mutual-exclusion lock. Callers here never lock it twice from the
 * same task before unlocking, so a non-recursive implementation is fine.
 * Returns false on failure. */
bool osal_mutex_create(OsalMutexHandle *outHandle) __attribute__((warn_unused_result));

/* Same timeout convention as osal_queue_send/receive above - and the same
 * rule applies with extra weight here: nothing in this codebase should ever
 * pass UINT32_MAX to this function. A lock that's never coming back is
 * exactly the kind of thing a caller needs to detect and report, not block
 * on forever - pick a real bound based on how long the critical section it
 * guards can legitimately take. Returns false on timeout (the lock was NOT
 * acquired - callers must not proceed as though they hold it). */
bool osal_mutex_lock(OsalMutexHandle mutex, uint32_t timeoutMs) __attribute__((warn_unused_result));

/* Only valid to call after a osal_mutex_lock() that returned true. */
void osal_mutex_unlock(OsalMutexHandle mutex);

/* ── Delay / time ─────────────────────────────────────────────────────── */

void osal_delay_ms(uint32_t ms);

/* Free-running elapsed-time clock in milliseconds (osKernelGetTickCount()-
 * derived on FreeRTOS, a monotonic clock on host/POSIX). No epoch/calendar
 * meaning - only differences between two calls are meaningful. */
uint32_t osal_get_time_ms(void);

/* ── Heap ─────────────────────────────────────────────────────────────── */

/* Thin wrapper over whichever heap allocator is actually safe to call from
 * this target's task context - NOT necessarily plain malloc()/free(). On
 * FreeRTOS these are pvPortMalloc()/vPortFree() (a different heap than
 * newlib's own, and the only one FreeRTOS task code is guaranteed safe
 * calling into); on ThreadX/host, newlib's malloc()/free() are already
 * thread-safe (Core/ThreadSafe/ on ThreadX, pthread-safe libc on host) so
 * they're used directly; on Zephyr, k_malloc()/k_free() (Zephyr's own heap,
 * matching how osal_queue_create() already allocates there).
 *
 * Exists specifically so app/'s shared usbh_conf.c (USBH_malloc/USBH_free)
 * doesn't need a target-specific macro - see that file's own comment. Not
 * used anywhere else in app/ (the rest of this project's own code follows
 * the "no dynamic allocation" rule; this exists only to satisfy vendor
 * middleware that itself allocates). */
void *osal_malloc(size_t size);
void osal_free(void *ptr);
