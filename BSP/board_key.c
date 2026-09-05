#include "board_key.h"

#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"

#include "board_config.h"

typedef struct
{
    uint32_t port;
    uint32_t pin;
} board_key_config_t;

static const board_key_config_t s_board_key_config[6] =
{
    {BOARD_KEY1_PORT, BOARD_KEY1_PIN},
    {BOARD_KEY2_PORT, BOARD_KEY2_PIN},
    {BOARD_KEY3_PORT, BOARD_KEY3_PIN},
    {BOARD_KEY4_PORT, BOARD_KEY4_PIN},
    {BOARD_KEY5_PORT, BOARD_KEY5_PIN},
    {BOARD_KEY6_PORT, BOARD_KEY6_PIN}
};

void board_key_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOB);

    gpio_mode_set(
        GPIOE,
        GPIO_MODE_INPUT,
        GPIO_PUPD_NONE,
        BOARD_KEY1_PIN |
        BOARD_KEY2_PIN |
        BOARD_KEY3_PIN |
        BOARD_KEY4_PIN |
        BOARD_KEY5_PIN);

    gpio_mode_set(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BOARD_KEY6_PIN);
}

uint8_t board_key_read(uint8_t key_no)
{
    if ((key_no < 1U) || (key_no > 6U))
    {
        return 1U;
    }

    return (uint8_t)gpio_input_bit_get(
        s_board_key_config[key_no - 1U].port,
        s_board_key_config[key_no - 1U].pin);
}
