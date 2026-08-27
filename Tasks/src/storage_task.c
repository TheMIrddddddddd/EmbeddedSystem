#include "storage_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_mutex.h"

#define STORAGE_TASK_PRIORITY          3U
#define STORAGE_TASK_STACK_DEPTH       512U

static StaticTask_t s_storage_task_tcb;
static StackType_t  s_storage_task_stack[STORAGE_TASK_STACK_DEPTH];

static volatile uint32_t s_storage_task_heartbeat;

static void storage_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        if (xSemaphoreTake(task_mutex_get(), pdMS_TO_TICKS(10U)) == pdTRUE) {

            s_storage_task_heartbeat++;

            xSemaphoreGive(task_mutex_get());
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int storage_task_create(void)
{
    TaskHandle_t storage_task_handle;

    storage_task_handle = xTaskCreateStatic(
        storage_task,
        "Storage",
        STORAGE_TASK_STACK_DEPTH,
        NULL,
        STORAGE_TASK_PRIORITY,
        s_storage_task_stack,
        &s_storage_task_tcb
    );

    if (storage_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}
