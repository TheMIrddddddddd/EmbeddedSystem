#include "protocol_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_queues.h"

#define PROTOCOL_TASK_PRIORITY          5U
#define PROTOCOL_TASK_STACK_DEPTH       256U

static StaticTask_t s_protocol_task_tcb;
static StackType_t  s_protocol_task_stack[PROTOCOL_TASK_STACK_DEPTH];

static volatile uint32_t s_protocol_task_heartbeat;

static void protocol_task(void *argument)
{
    (void)argument;

    protocol_request_t request;
    request.request_id = 1U;
    request.origin = 1U;
    request.operation = 1U;
    request.protocol_sequence = 1U;
    request.deadline_tick = xTaskGetTickCount() + pdMS_TO_TICKS(100);

    for(;;)
    {
        s_protocol_task_heartbeat++;

        request.request_id++;
        protocol_request_send(&request);

        vTaskDelay(pdMS_TO_TICKS(10));
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

uint32_t protocol_task_get_heartbeat(void)
{
    return s_protocol_task_heartbeat;
}
