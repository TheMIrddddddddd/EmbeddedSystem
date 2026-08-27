#include "task_timer.h"

#include "FreeRTOS.h"
#include "timers.h"

#define TASK_TIMER_PERIOD_MS        1000U

static StaticTimer_t s_task_timer;
static TimerHandle_t s_task_timer_handle;
static volatile uint32_t s_task_timer_count;

static void task_timer_callback(TimerHandle_t timer)
{
    (void)timer;

    s_task_timer_count++;
}

int task_timer_init(void)
{
    s_task_timer_handle = xTimerCreateStatic(
        "TaskTimer",
        pdMS_TO_TICKS(TASK_TIMER_PERIOD_MS),
        pdTRUE,
        NULL,
        task_timer_callback,
        &s_task_timer
    );

    if (s_task_timer_handle == NULL) {
        return 0;
    }

    if (xTimerStart(s_task_timer_handle, 0U) != pdPASS) {
        return 0;
    }
}

uint32_t task_timer_get_count(void)
{
    return s_task_timer_count;
}
