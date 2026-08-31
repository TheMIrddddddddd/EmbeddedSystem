#include "board_i2c.h"

#include "gd32f4xx_gpio.h"
#include "gd32f4xx_i2c.h"
#include "gd32f4xx_rcu.h"

#include "board_config.h"

#define BOARD_I2C0_TIMEOUT  100000U

int board_i2c0_init(void)
{
    uint32_t i2c_pins;
    i2c_pins = BOARD_I2C0_SCL_PIN | BOARD_I2C0_SDA_PIN;

    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_I2C0);

    gpio_af_set(BOARD_I2C0_PORT, BOARD_I2C0_AF, i2c_pins);

    gpio_mode_set(BOARD_I2C0_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, i2c_pins);

    gpio_output_options_set(BOARD_I2C0_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, i2c_pins);

    i2c_deinit(I2C0);

    i2c_clock_config(I2C0, 100000U, I2C_DTCY_2);

    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0U);

    i2c_ack_config(I2C0, I2C_ACK_ENABLE);

    i2c_enable(I2C0);

    return 1;
}

static int board_i2c0_wait_flag(i2c_flag_enum flag)
{
    uint32_t timeout;

    timeout = BOARD_I2C0_TIMEOUT;

    while (RESET == i2c_flag_get(I2C0, flag))
    {
        if (SET == i2c_flag_get(I2C0, I2C_FLAG_BERR))
        {
            return 0;
        }

        if (SET == i2c_flag_get(I2C0, I2C_FLAG_LOSTARB))
        {
            return 0;
        }

        if (SET == i2c_flag_get(I2C0, I2C_FLAG_AERR))
        {
            return 0;
        }
        
        if (timeout == 0)
        {
            return 0;
        }
        timeout--;
    }
    return 1;
}

static int board_i2c0_wait_bus_idle(void)
{
    uint32_t timeout;

    timeout = BOARD_I2C0_TIMEOUT;

    while (SET == i2c_flag_get(I2C0, I2C_FLAG_I2CBSY))
    {
        if (timeout == 0U)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

int board_i2c0_write(uint8_t device_address, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if ((data == NULL) || (length == 0U))
    {
        return 0;
    }

    if (board_i2c0_wait_bus_idle() == 0)
    {
        return 0;
    }
    
    i2c_flag_clear(I2C0, I2C_FLAG_BERR);
    i2c_flag_clear(I2C0, I2C_FLAG_LOSTARB);
    i2c_flag_clear(I2C0, I2C_FLAG_AERR);

    i2c_start_on_bus(I2C0);

    if (board_i2c0_wait_flag(I2C_FLAG_SBSEND) == 0)
    {
        goto write_failed;
    }
    
    i2c_master_addressing(I2C0, device_address, I2C_TRANSMITTER);

    if (board_i2c0_wait_flag(I2C_FLAG_ADDSEND) == 0)
    {
        goto write_failed;
    }
    
    i2c_flag_clear(I2C0, I2C_FLAG_ADDSEND);

    for (index = 0; index < length; index++)
    {
        if (board_i2c0_wait_flag(I2C_FLAG_TBE) == 0)
        {
            goto write_failed;
        }
        
        i2c_data_transmit(I2C0, data[index]);

        if (board_i2c0_wait_flag(I2C_FLAG_BTC) == 0)
        {
            goto write_failed;
        }
    }

    i2c_stop_on_bus(I2C0);

    if (board_i2c0_wait_bus_idle() == 0)
    {
        return 0;
    }

    return 1;

write_failed:
    i2c_stop_on_bus(I2C0);
    (void)board_i2c0_wait_bus_idle();

    return 0;
}

