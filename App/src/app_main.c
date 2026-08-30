#include <stdint.h>

#include "gd32f4xx.h"
#include "gd32f4xx_misc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"

#include "board_gpio.h"
#include "board_spi_flash.h"
#include "common_flash_layout.h"

volatile uint8_t g_flash_jedec_id[3];

int main(void)
{

    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    board_led_init();

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

    if (board_spi_flash_read_jedec_id(
            (uint8_t *)g_flash_jedec_id) == 0) {
        __disable_irq();

        for (;;) {
        }
    }
    app_tasks_status_t task_status;
    
    task_status = app_tasks_create();

    __enable_irq();

    if (task_status != APP_TASKS_STATUS_OK) {
        __disable_irq();

        for (;;) {
        }
    }
    
    vTaskStartScheduler();
    
    __disable_irq();

    for (;;) {
    }
}
