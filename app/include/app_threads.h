#pragma once

#include "osal.h"

/* The single place every application thread gets registered and started.
 * No module (blinky, the USB Host process pump, the storage showcase's
 * Servicer/Requester tasks) calls osal_task_create() itself - each one only
 * appends its own OsalTaskConfig entries here via appThreadRegistryAdd(),
 * and app.c's appRun() is the only caller of appThreadRegistryStartAll().
 * This registry doesn't know or care what any entry actually does - it just
 * holds a flat list and starts everything on it. */

enum
{
    /* Sized to exactly what this app ever registers today: Blinky (1) +
     * UsbHostProcess (1, embedded targets only) + storage showcase's
     * Servicer + 4 Requesters (5) = 7. Headroom to 8. */
    kAppThreadRegistryCapacity = 8U,
};

typedef struct AppThreadRegistry
{
    OsalTaskConfig entries[kAppThreadRegistryCapacity];
    size_t count;
} AppThreadRegistry;

void appThreadRegistryInit(AppThreadRegistry *registry);

/* Appends one thread descriptor. Returns false if the registry is full -
 * treat that as a boot-time fault (bump kAppThreadRegistryCapacity), not
 * something to retry. */
bool appThreadRegistryAdd(AppThreadRegistry *registry, const OsalTaskConfig *config) __attribute__((warn_unused_result));

/* Starts every registered thread, in registration order, via
 * osal_task_create(). Returns false if any single one failed to start (a
 * partial start is still attempted - later entries are not skipped just
 * because an earlier one failed, so the caller can see the full picture in
 * the logs rather than just "something, somewhere, failed"). */
bool appThreadRegistryStartAll(const AppThreadRegistry *registry) __attribute__((warn_unused_result));
