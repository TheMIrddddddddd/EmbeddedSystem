#include "board_spi_flash.h"

#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_spi.h"

#include "board_config.h"

void board_spi_flash_cs_high(void)
{
    gpio_bit_set(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN);
}

void board_spi_flash_cs_low(void)
{
    gpio_bit_reset(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN);
}

int board_spi_flash_init(void)
{
    spi_parameter_struct spi_init_struct;
    uint32_t spi_pins;

    spi_pins = BOARD_SPI1_SCK_PIN | BOARD_SPI1_MISO_PIN | BOARD_SPI1_MOSI_PIN;

    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_SPI1);

    gpio_af_set(BOARD_SPI1_PORT, BOARD_SPI1_AF, spi_pins);
    gpio_mode_set(BOARD_SPI1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, spi_pins);
    gpio_output_options_set(BOARD_SPI1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, spi_pins);
    gpio_mode_set(BOARD_FLASH_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BOARD_FLASH_CS_PIN);
    gpio_output_options_set(BOARD_FLASH_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BOARD_FLASH_CS_PIN);

    board_spi_flash_cs_high();

    spi_i2s_deinit(SPI1);
    spi_struct_para_init(&spi_init_struct);

    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode = SPI_MASTER;
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    spi_init_struct.nss = SPI_NSS_SOFT;
    spi_init_struct.prescale = SPI_PSC_8;
    spi_init_struct.endian = SPI_ENDIAN_MSB;

    spi_init(SPI1, &spi_init_struct);
    spi_enable(SPI1);
    return 1;
}

int board_spi_flash_reset(void)
{
    volatile uint32_t delay_count;

    /* Reset Enable */
    board_spi_flash_cs_low();
    board_spi_flash_transfer(0x66U);
    board_spi_flash_cs_high();

    /* Reset */
    board_spi_flash_cs_low();
    board_spi_flash_transfer(0x99U);
    board_spi_flash_cs_high();

    /* 等待器件完成复位，避免立即访问导致时序冲突 */
    delay_count = 10000U;
    while (delay_count > 0U)
    {
        delay_count--;
    }

    return board_spi_flash_wait_ready();
}

uint8_t board_spi_flash_transfer(uint8_t data)
{
    while (RESET == spi_i2s_flag_get(SPI1, SPI_FLAG_TBE))
    {}

    spi_i2s_data_transmit(SPI1, data);

    while (RESET == spi_i2s_flag_get(SPI1, SPI_FLAG_RBNE))
    {}
    
    return spi_i2s_data_receive(SPI1);
}

int board_spi_flash_read_jedec_id(uint8_t id[3])
{
    if (id == 0)
    {
        return 0;
    }

    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x9FU);

    id[0] = board_spi_flash_transfer(0xFFU);
    id[1] = board_spi_flash_transfer(0xFFU);
    id[2] = board_spi_flash_transfer(0xFFU);

    board_spi_flash_cs_high();
    
    return 1;
}

uint8_t board_spi_flash_read_status(void)
{
    uint8_t status;

    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x05U);
    status = board_spi_flash_transfer(0xFFU);

    board_spi_flash_cs_high();

    return status;
}

int board_spi_flash_write_enable(void)
{
    uint8_t status;

    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x06U);

    board_spi_flash_cs_high();

    status = board_spi_flash_read_status();

    if ((status & 0x02U) == 0U)
    {
        return 0;
    }

    return 1;
}

int board_spi_flash_wait_ready(void)
{
    uint8_t status;
    volatile uint32_t timeout;

    timeout = 1000000U;

    do
    {
        status = board_spi_flash_read_status();

        if ((status & 0x01) == 0U)
        {
            return 1;
        }

        timeout--;
    } while (timeout > 0U);

    return 0;
}

int board_spi_flash_sector_erase(uint32_t address)
{
    if (address >= 0x80000U)
    {
        return 0;
    }

    if (board_spi_flash_write_enable() == 0)
    {
        return 0;
    }

    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x20U);
    board_spi_flash_transfer((uint8_t)(address >> 16));
    board_spi_flash_transfer((uint8_t)(address >> 8));
    board_spi_flash_transfer((uint8_t)address);

    board_spi_flash_cs_high();

    return board_spi_flash_wait_ready();
}

int board_spi_flash_page_program(uint32_t address, const uint8_t* data, uint32_t length)
{
    uint32_t index;

    if ((data == 0U) || (length == 0U) || (length > 256U))
    {
        return 0;
    }

    if(((address & 0xFFU) + length) > 256U)
    {
        return 0;
    }

    if (board_spi_flash_write_enable() == 0)
    {
        return 0;
    }
    
    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x02U);

    board_spi_flash_transfer((uint8_t)(address >> 16));
    board_spi_flash_transfer((uint8_t)(address >> 8));
    board_spi_flash_transfer((uint8_t)address);

    for (index = 0; index < length; index++)
    {
        board_spi_flash_transfer(data[index]);
    }
    board_spi_flash_cs_high();

    return board_spi_flash_wait_ready();
}

int board_spi_flash_read(uint32_t address, uint8_t *data, uint32_t length)
{
    uint32_t index;

    if (((uint64_t)address + length) > 0x80000U)
    {
        return 0;
    }

    if ((data == 0) || (length == 0U))
    {
        return 0;
    }

    board_spi_flash_cs_low();

    board_spi_flash_transfer(0x03U);

    board_spi_flash_transfer((uint8_t)(address >> 16));
    board_spi_flash_transfer((uint8_t)(address >> 8));
    board_spi_flash_transfer((uint8_t)address);

    for (index = 0U; index < length; index++)
    {
        data[index] = board_spi_flash_transfer(0xFFU);
    }

    board_spi_flash_cs_high();

    return 1;
}
