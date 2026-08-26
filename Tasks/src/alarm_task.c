#include "alarm_task.h"
#include "FreeRTOS.h"
#include "task.h"

#define ALARM_TASK_PRIORITY          3U
#define ALARM_TASK_STACK_DEPTH       256U

static StaticTask_t s_alarm_task_tcb;
static StackType_t  s_alarm_task_stack[ALARM_TASK_STACK_DEPTH];

static volatile uint32_t s_alarm_task_heartbeat;

static void alarm_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        s_alarm_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int alarm_task_create(void)
{
    TaskHandle_t alarm_task_handle;

    alarm_task_handle = xTaskCreateStatic(
        alarm_task,
        "Alarm",
        ALARM_TASK_STACK_DEPTH,
        NULL,
        ALARM_TASK_PRIORITY,
        s_alarm_task_stack,
        &s_alarm_task_tcb
    );

    if (alarm_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}
