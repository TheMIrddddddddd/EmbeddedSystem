#include "control_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_queues.h"
#include "task_events.h"
#include "task_mutex.h"

#define CONTROL_TASK_PRIORITY          3U
#define CONTROL_TASK_STACK_DEPTH       256U

static StaticTask_t s_control_task_tcb;
static StackType_t  s_control_task_stack[CONTROL_TASK_STACK_DEPTH];

static volatile uint32_t s_control_task_heartbeat;

static void control_task(void *argument)
{
    protocol_request_t request;

    (void)argument;

    for(;;)
    {
        // if (protocol_request_receive(&request, portMAX_DELAY) == pdTRUE)
        // {
        //     s_control_task_heartbeat++;
        // }
        xEventGroupSetBits(task_events_get(), TASK_EVENT_SYSTEM_READY);

        if (xSemaphoreTake(task_mutex_get(), pdMS_TO_TICKS(10U)) == pdTRUE)
        {
            s_control_task_heartbeat++;
            xSemaphoreGive(task_mutex_get());
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int control_task_create(void)
{
    TaskHandle_t control_task_handle;

    control_task_handle = xTaskCreateStatic(
        control_task,
        "Control",
        CONTROL_TASK_STACK_DEPTH,
        NULL,
        CONTROL_TASK_PRIORITY,
        s_control_task_stack,
        &s_control_task_tcb
    );

    if (control_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}

uint32_t control_task_get_heartbeat(void)
{
    return s_control_task_heartbeat;
}
