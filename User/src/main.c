#include "gd32f4xx.h"
#include "systick.h"
#include "board_gpio.h"


int main(void)
{
    /* SysTick 1ms 节拍 —— delay_1ms 依赖它,必须先调 */
    systick_config();

    /* 初始化 LED */
    board_led_init();

   while(1) {
    delay_1ms(1);
}
}
