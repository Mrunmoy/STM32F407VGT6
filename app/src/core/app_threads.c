#include "app_threads.h"

#include "app_task_trace.h"

void appThreadRegistryInit(AppThreadRegistry *registry)
{
    registry->count = 0U;
}

bool appThreadRegistryAdd(AppThreadRegistry *registry, const OsalTaskConfig *config)
{
    if (registry->count >= kAppThreadRegistryCapacity)
    {
        return false;
    }

    registry->entries[registry->count] = *config;
    registry->count++;
    return true;
}

bool appThreadRegistryStartAll(const AppThreadRegistry *registry)
{
    bool allStarted = true;

    for (size_t i = 0U; i < registry->count; i++)
    {
        if (osal_task_create(&registry->entries[i], NULL))
        {
            /* Registered here, not by the task itself - a task that hangs
             * before its own first loop iteration (still in one-time setup)
             * is then still visible to the watchdog as "created but never
             * checked in", not simply absent. */
            appTaskTraceRegister(registry->entries[i].name);
        }
        else
        {
            allStarted = false;
        }
    }

    return allStarted;
}
