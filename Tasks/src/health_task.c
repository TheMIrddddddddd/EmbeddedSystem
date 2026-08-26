#include "health_task.h"
#include "FreeRTOS.h"
#include "task.h"

#define HEALTH_TASK_PRIORITY          5U
#define HEALTH_TASK_STACK_DEPTH       128U

static StaticTask_t s_health_task_tcb;
static StackType_t  s_health_task_stack[HEALTH_TASK_STACK_DEPTH];

static volatile uint32_t s_health_task_heartbeat;

static void health_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        s_health_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int health_task_create(void)
{
    TaskHandle_t health_task_handle;

    health_task_handle = xTaskCreateStatic(
        health_task,
        "Health",
        HEALTH_TASK_STACK_DEPTH,
        NULL,
        HEALTH_TASK_PRIORITY,
        s_health_task_stack,
        &s_health_task_tcb
    );

    if (health_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}
