#include "control_task.h"
#include "FreeRTOS.h"
#include "task.h"

#define CONTROL_TASK_PRIORITY          3U
#define CONTROL_TASK_STACK_DEPTH       256U

static StaticTask_t s_control_task_tcb;
static StackType_t  s_control_task_stack[CONTROL_TASK_STACK_DEPTH];

static volatile uint32_t s_control_task_heartbeat;

static void control_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        s_control_task_heartbeat++;

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
