#include "gd32f4xx.h"
#include "systick.h"
#include "board_gpio.h"
#include "boot_jump.h"

int main(void)
{
    /* SysTick 1ms 节拍 */
    systick_config();

    board_led_init();

    for (uint32_t i = 0U; i < 5U; i++)
    {
        if (0U == (i & 1U))
        {
            board_led_on();
        }
        else
        {
            board_led_off();
        }
        delay_1ms(1000U);
    }
    board_led_off();

    boot_jump_to_app();

    /* 跳转失败(校验不通过):停住,LED 保持熄灭;失败灯效 M6 再定义 */
    while (1)
    {

    }
}
