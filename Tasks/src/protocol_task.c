#include "protocol_task.h"
#include "FreeRTOS.h"
#include "task.h"

#define PROTOCOL_TASK_PRIORITY          5U
#define PROTOCOL_TASK_STACK_DEPTH       256U

static StaticTask_t s_protocol_task_tcb;
static StackType_t  s_protocol_task_stack[PROTOCOL_TASK_STACK_DEPTH];

static volatile uint32_t s_protocol_task_heartbeat;

static void protocol_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        s_protocol_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int protocol_task_create(void)
{
    TaskHandle_t protocol_task_handle;

    protocol_task_handle = xTaskCreateStatic(
        protocol_task,
        "Protocol",
        PROTOCOL_TASK_STACK_DEPTH,
        NULL,
        PROTOCOL_TASK_PRIORITY,
        s_protocol_task_stack,
        &s_protocol_task_tcb
    );

    if (protocol_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}
