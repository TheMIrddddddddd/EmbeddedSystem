#include "gd32f4xx.h"
#include "systick.h"
#include "board_config.h"

#define LED_PORT        BOARD_LED1_PORT
#define LED_CLK_PORT    BOARD_LED_GPIO_CLK
#define LED1_PIN        BOARD_LED1_PIN

static void led_gpio_config(void)
{
    /* ① 使能 GPIOD 的时钟(不使能时钟,寄存器写不进去) */
    rcu_periph_clock_enable(LED_CLK_PORT);

    /* ② 配置 PD8 为推挽输出 */
    gpio_mode_set(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED1_PIN);
    gpio_output_options_set(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, LED1_PIN);

    /* ③ 初始状态:灭(PD8 拉低) */
    gpio_bit_reset(LED_PORT, LED1_PIN);
}

int main(void)
{
    /* SysTick 1ms 节拍 —— delay_1ms 依赖它,必须先调 */
    systick_config();

    /* 初始化 LED */
    led_gpio_config();

    while(1) {
        /* 点亮 */
        gpio_bit_set(LED_PORT, LED1_PIN);
        delay_1ms(1000);

        /* 熄灭 */
        gpio_bit_reset(LED_PORT, LED1_PIN);
        delay_1ms(1000);
    }
}
