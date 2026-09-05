#include <stdint.h>

#include "gd32f4xx.h"
#include "gd32f4xx_misc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"

#include "board_gpio.h"
#include "board_key.h"
#include "board_spi_flash.h"
#include "board_usart.h"
#include "board_i2c.h"
#include "board_oled.h"
#include "common_flash_layout.h"

int main(void)
{
    app_tasks_status_t task_status;

    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    board_led_init();
    board_key_init();

    board_usart0_init();
    board_usart1_rs485_init();

    if (board_i2c0_init() == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }
    
    if (board_oled_init() != BOARD_OLED_STATUS_OK)
    {
        __disable_irq();

        for (;;)
        {
        }
    }
    
    if (board_spi_flash_init() == 0) {
        __disable_irq();

        for (;;) {
        }
    }

    if (board_spi_flash_reset() == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    task_status = app_tasks_create();

    if (task_status != APP_TASKS_STATUS_OK) {
        __disable_irq();

        for (;;) {
        }
    }

    __enable_irq();
    
    vTaskStartScheduler();
    
    __disable_irq();

    for (;;) {
    }
}
