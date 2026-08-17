#include "board_gpio.h"
#include "gd32f4xx_rcu.h"

void board_led_init(void)
{
    rcu_periph_clock_enable(BOARD_LED_GPIO_CLK);

    gpio_mode_set(BOARD_LED1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BOARD_LED_ALL_PINS);
    gpio_output_options_set(BOARD_LED1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BOARD_LED_ALL_PINS);

    gpio_bit_reset(BOARD_LED1_PORT, BOARD_LED_ALL_PINS);
}

void board_led_on(void)
{
    gpio_bit_set(BOARD_LED1_PORT, BOARD_LED1_PIN);
}

void board_led_off(void)
{
    gpio_bit_reset(BOARD_LED1_PORT, BOARD_LED1_PIN);
}

void board_led_set(uint8_t led_no, uint8_t on)
{
    uint32_t led_port;
    uint32_t led_pin;

    switch(led_no) {
    case 1:
        led_port = BOARD_LED1_PORT;
        led_pin = BOARD_LED1_PIN;
        break;

    case 2:
        led_port = BOARD_LED2_PORT;
        led_pin = BOARD_LED2_PIN;
        break;

    case 3:
        led_port = BOARD_LED3_PORT;
        led_pin = BOARD_LED3_PIN;
        break;

    case 4:
        led_port = BOARD_LED4_PORT;
        led_pin = BOARD_LED4_PIN;
        break;

    case 5:
        led_port = BOARD_LED5_PORT;
        led_pin = BOARD_LED5_PIN;
        break;

    case 6:
        led_port = BOARD_LED6_PORT;
        led_pin = BOARD_LED6_PIN;
        break;

    default:
        return;
    }

    if(on != 0U) {
        gpio_bit_set(led_port, led_pin);
    } else {
        gpio_bit_reset(led_port, led_pin);
    }
}
