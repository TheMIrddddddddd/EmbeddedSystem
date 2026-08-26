#include <stdint.h>

#include "gd32f4xx.h"
#include "gd32f4xx_misc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "app_task.h"

#include "board_gpio.h"
#include "common_flash_layout.h"

int main(void)
{

    SCB->VTOR = APP_BASE;

    __DSB();
    __ISB();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

    board_led_init();
    
    app_tasks_status_t task_status;
    
    task_status = app_tasks_create();
    
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
