#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_dac.h"
#include "gd32f4xx_rcu.h"

#include "board_config.h"
#include "board_dac.h"

static volatile uint8_t s_board_dac_initialized;
static volatile uint16_t s_board_dac_value;

int board_dac_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_DAC);

    gpio_mode_set(BOARD_DAC0_OUT0_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, BOARD_DAC0_OUT0_PIN);
    dac_deinit(DAC0);

    dac_trigger_disable(DAC0, DAC_OUT0);

    dac_output_buffer_enable(DAC0, DAC_OUT0);

    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, 0U);
    dac_enable(DAC0, DAC_OUT0);

    s_board_dac_value = 0U;
    s_board_dac_initialized = 1U;

    return 1;
}

int board_dac_output_set(uint16_t value)
{
    if (s_board_dac_initialized == 0)
    {
        return 0;
    }

    if (value > 4095U)
    {
        return 0;
    }
    dac_data_set(DAC0, DAC_OUT0, DAC_ALIGN_12B_R, value);

    s_board_dac_value = value;

    return 1;
}

uint16_t board_dac_output_get(void)
{
    return s_board_dac_value;
}
