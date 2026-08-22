#include <stdio.h>
#include "gd32f4xx.h"
#include "systick.h"
#include "board_gpio.h"
#include "common_flash_layout.h"

int main(void)
{
    /* 把向量表指到 App 区 */
    SCB->VTOR = APP_BASE;

    __enable_irq();

    /* SysTick 1ms 节拍 */
    systick_config();

    board_led_init();

    while (1)
    {
        board_led_on();
        delay_1ms(250U);
        board_led_off();
        delay_1ms(250U);
    }
}
