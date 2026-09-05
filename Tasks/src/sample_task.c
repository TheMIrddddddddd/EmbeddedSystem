#include "sample_task.h"
#include "FreeRTOS.h"
#include "task.h"

#define SAMPLE_TASK_PRIORITY          4U
#define SAMPLE_TASK_STACK_DEPTH       256U

static StaticTask_t s_sample_task_tcb;
static StackType_t  s_sample_task_stack[SAMPLE_TASK_STACK_DEPTH];

static volatile uint32_t s_sample_task_heartbeat;
static volatile uint32_t s_sample_task_stack_high_water_mark;

static void sample_task(void *argument)
{
    (void)argument;

    for(;;)
    {
        s_sample_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_sample_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int sample_task_create(void)
{
    TaskHandle_t sample_task_handle;

    sample_task_handle = xTaskCreateStatic(
        sample_task,
        "Sample",
        SAMPLE_TASK_STACK_DEPTH,
        NULL,
        SAMPLE_TASK_PRIORITY,
        s_sample_task_stack,
        &s_sample_task_tcb
    );

    if (sample_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}

uint32_t sample_task_get_heartbeat(void)
{
    return s_sample_task_heartbeat;
}

uint32_t sample_task_get_stack_high_water_mark(void)
{
    return s_sample_task_stack_high_water_mark;
}
