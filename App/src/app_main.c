#include <stdint.h>

#include "gd32f4xx.h"
#include "gd32f4xx_misc.h"
#include "gd32f4xx_fwdgt.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"
#include "app_config.h"

#include "board_gpio.h"
#include "board_key.h"
#include "board_spi_flash.h"
#include "board_usart.h"
#include "board_i2c.h"
#include "board_oled.h"
#include "board_adc.h"
#include "board_dac.h"
#include "board_rtc.h"
#include "board_timebase.h"
#include "common_flash_layout.h"

static uint8_t app_fwdgt_init(void)
{
    if (fwdgt_config(4095U, FWDGT_PSC_DIV256) != SUCCESS)
    {
        return 0U;
    }

    fwdgt_counter_reload();
    return 1U;
}

static void app_spi_flash_wait_feed(void)
{
    fwdgt_counter_reload();
}

int main(void)
{
    app_tasks_status_t task_status;

    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    /* 试运行 App 在外设初始化阶段也必须受看门狗保护。 */
    if (app_fwdgt_init() == 0U)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    if (board_timebase_init() == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

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

    board_spi_flash_set_wait_hook(app_spi_flash_wait_feed);

    (void)board_rtc_init();

    if (board_dac_init() == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    if (board_dac_output_set(2048U) == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    if (board_adc_init() == 0)
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    if (app_config_init() == 0)
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
