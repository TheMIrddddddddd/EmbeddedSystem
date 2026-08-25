#include <stdint.h>

#include "gd32f4xx.h"
#include "gd32f4xx_misc.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_gpio.h"
#include "common_flash_layout.h"

#define TEST_TASK_PRIORITY      1U
#define TEST_TASK_STACK_DEPTH   256U

static StaticTask_t s_test_task_tcb;
static StackType_t s_test_task_stack[TEST_TASK_STACK_DEPTH];

static void test_task(void *argument)
{
    TickType_t last_wake_time;
    uint8_t led_state;

    (void)argument;

    last_wake_time = xTaskGetTickCount();
    led_state = 0U;

    for (;;) {
        led_state = (uint8_t)!led_state;
        board_led_set(1U, led_state);

        vTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(500U));
    }
}

int main(void)
{
    TaskHandle_t test_task_handle;

    /*
     * App向量表位于APP_BASE。
     */
    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    /*
     * FreeRTOS要求使用4位抢占优先级、0位子优先级。
     */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    /*
     * 只初始化最小验证所需的LED。
     *
     * 不再调用systick_config()。
     * SysTick由FreeRTOS的port.c在调度器启动时配置。
     */
    board_led_init();

    /*
     * Bootloader跳转App前关闭了全局中断。
     * App必须重新开启中断。
     */
    __enable_irq();

    test_task_handle = xTaskCreateStatic(
        test_task,
        "Test",
        TEST_TASK_STACK_DEPTH,
        NULL,
        TEST_TASK_PRIORITY,
        s_test_task_stack,
        &s_test_task_tcb);

    if (test_task_handle == NULL) {
        __disable_irq();

        for (;;) {
        }
    }

    vTaskStartScheduler();

    /*
     * 全静态对象配置正确、内存充足时，
     * vTaskStartScheduler()不应该返回。
     */
    __disable_irq();

    for (;;) {
    }
}
