#pragma once

#include "FreeRTOS.h"
#include "task.h"

template <typename T>
class TaskLauncher
{
public:
    static void launch(const char *name, uint16_t stackSize, UBaseType_t priority, T *instance, TaskHandle_t *handle = nullptr)
    {
        xTaskCreate(&TaskLauncher::taskEntry, name, stackSize, instance, priority, handle);
    }

private:
    static void taskEntry(void *pvParams)
    {
        static_cast<T *>(pvParams)->run();
        vTaskDelete(nullptr);  // Optional: auto-delete task on return
    }
};
