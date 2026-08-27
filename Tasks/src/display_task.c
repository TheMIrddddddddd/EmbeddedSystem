#include "display_task.h"
#include "board_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_events.h"

#define DISPLAY_TASK_PRIORITY          2U
#define DISPLAY_TASK_STACK_DEPTH       256U

static StaticTask_t s_display_task_tcb;
static StackType_t  s_display_task_stack[DISPLAY_TASK_STACK_DEPTH];

static volatile EventBits_t s_display_event_bits;


static void display_task(void *argument)
{   

    TickType_t last_wake_time;
    uint8_t led_state;

    (void)argument;
    
    last_wake_time = xTaskGetTickCount();
    led_state = 0U;
  
    for(;;)
    {
        led_state = (uint8_t)!led_state;
        board_led_set(1U, led_state);

        s_display_event_bits = xEventGroupWaitBits(task_events_get(), TASK_EVENT_SYSTEM_READY, pdFALSE, pdTRUE, portMAX_DELAY);

        vTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(250U));
    }
}

int display_task_create(void)
{
    TaskHandle_t display_task_handle;

    display_task_handle = xTaskCreateStatic(
        display_task,
        "Display",
        DISPLAY_TASK_STACK_DEPTH,
        NULL,
        DISPLAY_TASK_PRIORITY,
        s_display_task_stack,
        &s_display_task_tcb
    );

    if (display_task_handle == NULL)
    {
        return 0;
    }
    
    return 1;
}
