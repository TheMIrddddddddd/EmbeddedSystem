#include "display_task.h"

#include "board_key.h"
#include "ebtn.h"

#include "FreeRTOS.h"
#include "task.h"

#include "task_queues.h"

#define DISPLAY_TASK_PRIORITY          2U
#define DISPLAY_TASK_STACK_DEPTH       256U

static StaticTask_t s_display_task_tcb;
static StackType_t  s_display_task_stack[DISPLAY_TASK_STACK_DEPTH];

static volatile uint32_t s_display_task_heartbeat;
static volatile uint32_t s_display_task_stack_high_water_mark;

static const ebtn_btn_param_t s_key_param =
    EBTN_PARAMS_INIT(
        20U,       /* 按下消抖时间 */
        20U,       /* 释放消抖时间 */
        30U,       /* 最短按下时间 */
        1000U,     /* 最长单击时间 */
        400U,      /* 连续点击间隔 */
        0U,        /* 不启用 KEEPALIVE */
        1U);       /* 最大连续点击次数 */

static ebtn_btn_t s_ebtn_keys[6] =
{
    EBTN_BUTTON_INIT(1U, &s_key_param),
    EBTN_BUTTON_INIT(2U, &s_key_param),
    EBTN_BUTTON_INIT(3U, &s_key_param),
    EBTN_BUTTON_INIT(4U, &s_key_param),
    EBTN_BUTTON_INIT(5U, &s_key_param),
    EBTN_BUTTON_INIT(6U, &s_key_param)
};

static uint8_t display_key_get_state(ebtn_btn_t *btn)
{
    if (btn == NULL)
    {
        return 0U;
    }

    /*
     * 板级按键低有效：
     * GPIO = 0 表示按下
     * ebtn 返回 1 表示活动
     */
    return (uint8_t)(board_key_read((uint8_t)btn->key_id) == 0U);
}

static void display_key_event(ebtn_btn_t *btn, ebtn_evt_t event)
{

    key_event_t  key_event;

    if (btn == NULL)
    {
        return;
    }
    
    key_event.key_id = btn->key_id;
    key_event.event  = (uint8_t)event;
    key_event.reserved = 0U;
    key_event.timestamp_ms = (uint32_t)(xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);

    (void)key_event_send(&key_event);
}

static void display_task(void *argument)
{   
    uint8_t ebtn_init_ok;

    (void)argument;

    ebtn_init_ok = (uint8_t)ebtn_init(
        s_ebtn_keys,
        6U,
        NULL,
        0U,
        display_key_get_state,
        display_key_event);

    if (ebtn_init_ok == 0U)
    {
        taskDISABLE_INTERRUPTS();

        for(;;){
        }
    }
    
    for (;;)
    {
        TickType_t now_tick;

        now_tick = xTaskGetTickCount();

        ebtn_process((ebtn_time_t)(now_tick * 1000U / configTICK_RATE_HZ));

        s_display_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_display_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
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

uint32_t display_task_get_heartbeat(void)
{
    return s_display_task_heartbeat;
}

uint32_t display_task_get_stack_high_water_mark(void)
{
    return s_display_task_stack_high_water_mark;
}
