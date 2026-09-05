#include "board_sdio.h"

#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_sdio.h"
#include "gd32f4xx_dma.h"
#include "gd32f4xx_misc.h"

#include "board_config.h"

#include <stddef.h>
#include <string.h>

#define BOARD_SDIO_INIT_CLOCK_DIVISION          298U
#define BOARD_SDIO_COMMAND_TIMEOUT              1000000U

#define BOARD_SDIO_ACMD41_ARGUMENT              0xC0100000U
#define BOARD_SDIO_ACMD41_RETRY_COUNT           1000U

#define BOARD_SDIO_DATA_POLL_TIMEOUT            10000000U
#define BOARD_SDIO_FIFO_WORD_COUNT              8U
#define BOARD_SDIO_R1_ERROR_MASK                0xFDF9E008U
#define BOARD_SDIO_ACMD6_BUS_WIDTH_4BIT         0X02U
#define BOARD_SDIO_TRANSFER_CLOCK_DIVISION      2U

#define BOARD_SDIO_CARD_READY_RETRY_COUNT       10000U
#define BOARD_SDIO_R1_READY_FOR_DATA            0x00000100U
#define BOARD_SDIO_R1_CURRENT_STATE_MASK        0x00001E00U
#define BOARD_SDIO_CARD_STATE_TRANSFER          0x00000800U

#define BOARD_SDIO_DMA_PERIPH                   DMA1
#define BOARD_SDIO_DMA_CHANNEL                  DMA_CH6
#define BOARD_SDIO_DMA_SUBPERIPH                DMA_SUBPERI4

#define BOARD_SDIO_DMA_WORD_COUNT \
    (BOARD_SDIO_BLOCK_SIZE / 4U)

#define BOARD_SDIO_FIFO_ADDRESS                 0x40012C80U

static volatile uint32_t s_board_sdio_dma_irq_events;
static volatile uint32_t s_board_sdio_dma_irq_count;

static volatile uint32_t s_board_sdio_irq_events;
static volatile uint32_t s_board_sdio_irq_count;

static board_sdio_irq_callback_t s_board_sdio_irq_callback;
static volatile uint8_t s_board_sdio_dma_busy;
static volatile uint8_t s_board_sdio_dma_fee_seen;
static volatile uint8_t s_board_sdio_dma_fee_chen_snapshot;
static volatile uint8_t s_board_sdio_dma_fee_chen_off_seen;

static void board_sdio_data_cleanup(void)
{
    sdio_dsm_disable();
    sdio_dma_disable();

    sdio_interrupt_disable(
        SDIO_INT_DTEND       |
        SDIO_INT_DTBLKEND    |
        SDIO_INT_DTCRCERR    |
        SDIO_INT_DTTMOUT     |
        SDIO_INT_TXURE       |
        SDIO_INT_RXORE       |
        SDIO_INT_STBITE);

    sdio_flag_clear(
        SDIO_FLAG_DTCRCERR  |
        SDIO_FLAG_DTTMOUT   |
        SDIO_FLAG_RXORE     |
        SDIO_FLAG_TXURE     |
        SDIO_FLAG_DTEND     |
        SDIO_FLAG_DTBLKEND  |
        SDIO_FLAG_STBITE    );
}

static void board_sdio_dma_cleanup(void)
{
    dma_interrupt_disable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FTF |
        DMA_INT_SDE |
        DMA_INT_TAE);

    dma_interrupt_disable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FEE);

    dma_channel_disable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL);

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_FEE);

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_SDE);

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_TAE);

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_HTF);

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_FTF);
}

static board_sdio_status_t board_sdio_dma_fifo_check(void)
{
    if (SET != dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_FEE))
    {
        return BOARD_SDIO_STATUS_OK;
    }

    /*
     * FEE 同时表示 FIFO 异常和 FIFO 错误：
     * 通道仍使能时属于异常，传输可以继续；
     * 通道已被硬件关闭时才是致命错误。
     */
    if ((DMA_CHCTL(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL) & DMA_CHXCTL_CHEN) == 0U)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }

    dma_flag_clear(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLAG_FEE);

    return BOARD_SDIO_STATUS_OK;
}

static void board_sdio_dma_config(const uint8_t *buffer,uint32_t direction)
{
    dma_single_data_parameter_struct dma_config;

    rcu_periph_clock_enable(RCU_DMA1);

    board_sdio_dma_cleanup();

    dma_deinit(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL);

    dma_single_data_para_struct_init(&dma_config);

    dma_config.periph_addr = BOARD_SDIO_FIFO_ADDRESS;
    dma_config.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_config.memory0_addr = (uint32_t)buffer;
    dma_config.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_config.periph_memory_width = DMA_PERIPH_WIDTH_32BIT;
    dma_config.circular_mode = DMA_CIRCULAR_MODE_DISABLE;
    dma_config.direction = direction;
    dma_config.number = BOARD_SDIO_DMA_WORD_COUNT;
    dma_config.priority = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, &dma_config);

    dma_channel_subperipheral_select(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, BOARD_SDIO_DMA_SUBPERIPH);

    dma_interrupt_enable(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_INT_FTF);

    dma_channel_enable(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL);
}

static void board_sdio_dma_write_config(const uint8_t *buffer)
{
    dma_multi_data_parameter_struct dma_config;

    rcu_periph_clock_enable(RCU_DMA1);

    board_sdio_dma_cleanup();

    dma_deinit(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL);

    dma_multi_data_para_struct_init(&dma_config);

    dma_config.periph_addr = BOARD_SDIO_FIFO_ADDRESS;
    dma_config.memory0_addr = (uint32_t)buffer;
    dma_config.direction = DMA_MEMORY_TO_PERIPH;

    /*
     * 使用外设作为流控制器时，
     * 传输数量由 SDIO 数据状态机控制。
     */
    dma_config.number = 0U;

    dma_config.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_config.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_config.periph_width = DMA_PERIPH_WIDTH_32BIT;
    dma_config.memory_width = DMA_MEMORY_WIDTH_32BIT;
    dma_config.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_config.periph_burst_width = DMA_PERIPH_BURST_4_BEAT;
    dma_config.memory_burst_width = DMA_MEMORY_BURST_4_BEAT;
    dma_config.circular_mode = DMA_CIRCULAR_MODE_DISABLE;
    dma_config.critical_value = DMA_FIFO_4_WORD;

    dma_multi_data_mode_init(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        &dma_config);

    dma_flow_controller_config(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_FLOW_CONTROLLER_PERI);

    dma_channel_subperipheral_select(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        BOARD_SDIO_DMA_SUBPERIPH);

    dma_interrupt_enable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FTF);

}

static void board_sdio_dma_async_interrupt_enable(void)
{
    dma_interrupt_enable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FTF |
        DMA_INT_SDE |
        DMA_INT_TAE);

    dma_interrupt_enable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FEE);
}

static void board_sdio_data_async_interrupt_enable(void)
{
    sdio_interrupt_enable(
        SDIO_INT_DTEND       |
        SDIO_INT_DTBLKEND    |
        SDIO_INT_DTCRCERR    |
        SDIO_INT_DTTMOUT     |
        SDIO_INT_TXURE       |
        SDIO_INT_RXORE       |
        SDIO_INT_STBITE);
}

static board_sdio_status_t board_sdio_read_fifo_word(uint8_t *buffer, uint32_t *bytes_received)
{
    uint32_t data;

    if ((buffer == NULL) || (bytes_received == NULL))
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if ((*bytes_received + 4U) > (BOARD_SDIO_BLOCK_SIZE))
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }
    
    data = sdio_data_read();

    buffer[*bytes_received] = (uint8_t)((data & 0xFFU));
    buffer[*bytes_received + 1U] = (uint8_t)((data >> 8U) & 0xFFU);
    buffer[*bytes_received + 2U] = (uint8_t)((data >> 16U) & 0xFFU);
    buffer[*bytes_received + 3U] = (uint8_t)((data >> 24U) & 0xFFU);
    
    *bytes_received += 4U;

    return BOARD_SDIO_STATUS_OK;
}

static board_sdio_status_t board_sdio_write_fifo_word(const uint8_t *buffer, uint32_t *bytes_sent)
{
    uint32_t data;

    if ((buffer == NULL) || (bytes_sent == NULL))
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if ((*bytes_sent + 4U) > BOARD_SDIO_BLOCK_SIZE)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }
    data = (uint32_t)buffer[*bytes_sent];

    data |= ((uint32_t)buffer[*bytes_sent + 1U] << 8U);

    data |= ((uint32_t)buffer[*bytes_sent + 2U] << 16U);

    data |= ((uint32_t)buffer[*bytes_sent + 3U] << 24U);

    sdio_data_write(data);

    *bytes_sent += 4U;

    return BOARD_SDIO_STATUS_OK;
}

static board_sdio_status_t board_sdio_wait_command(
    uint32_t complete_flag,
    uint8_t check_crc_error)
{
    uint32_t timeout;

    timeout = BOARD_SDIO_COMMAND_TIMEOUT;

    for (;;)
    {
        /*
         * 命令响应超时
         */
        if (SET == sdio_flag_get(SDIO_FLAG_CMDTMOUT))
        {
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        /*
         * 总线起始位错误
         */
        if (SET == sdio_flag_get(SDIO_FLAG_STBITE))
        {
            return BOARD_SDIO_STATUS_COMMAND_ERROR;
        }

        /*
         * CRC 标志处理
         *
         * 普通响应：
         *     CCRCERR 表示真正的 CRC 错误
         *
         * R3 响应：
         *     ACMD41 没有有效 CRC，
         *     CCRCERR 表示响应已经结束
         */
        if (SET == sdio_flag_get(SDIO_FLAG_CCRCERR))
        {
            if (check_crc_error != 0U)
            {
                return BOARD_SDIO_STATUS_COMMAND_ERROR;
            }

            sdio_flag_clear(SDIO_FLAG_CCRCERR);

            return BOARD_SDIO_STATUS_OK;
        }

        /*
         * 普通命令完成
         */
        if (SET == sdio_flag_get(complete_flag))
        {
            return BOARD_SDIO_STATUS_OK;
        }

        /*
         * 软件超时
         */
        if (timeout == 0U)
        {
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        timeout--;
    }
}

static board_sdio_status_t board_sdio_command_internal(
    uint32_t command,
    uint32_t argument,
    uint32_t response_type,
    uint32_t *response,
    uint8_t check_crc_error)
{
    uint32_t complete_flag;
    board_sdio_status_t status;

    if (command > 63U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if ((response_type != SDIO_RESPONSETYPE_NO) &&
        (response == NULL))
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if (response_type == SDIO_RESPONSETYPE_NO)
    {
        complete_flag = SDIO_FLAG_CMDSEND;
    }
    else
    {
        complete_flag = SDIO_FLAG_CMDRECV;
    }

    /*
     * 清除上一次命令残留标志
     */
    sdio_flag_clear(
        SDIO_FLAG_CCRCERR |
        SDIO_FLAG_CMDTMOUT |
        SDIO_FLAG_CMDSEND |
        SDIO_FLAG_CMDRECV |
        SDIO_FLAG_STBITE);

    sdio_csm_disable();

    sdio_wait_type_set(SDIO_WAITTYPE_NO);

    sdio_command_response_config(
        command,
        argument,
        response_type);

    sdio_csm_enable();

    status = board_sdio_wait_command(
        complete_flag,
        check_crc_error);

    sdio_csm_disable();

    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    if (response != NULL)
    {
        *response = sdio_response_get(SDIO_RESPONSE0);
    }

    return BOARD_SDIO_STATUS_OK;
}

void board_sdio_irq_callback_register(board_sdio_irq_callback_t callback)
{
    s_board_sdio_irq_callback = callback;
}

board_sdio_status_t board_sdio_command(
    uint32_t command,
    uint32_t argument,
    uint32_t response_type,
    uint32_t *response)
{
    /*
     * 普通响应检查 CRC
     */
    return board_sdio_command_internal(
        command,
        argument,
        response_type,
        response,
        1U);
}

board_sdio_status_t board_sdio_command_r2(uint32_t command, uint32_t argument, uint32_t response[4])
{
    board_sdio_status_t status;

    if (response == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    status = board_sdio_command_internal(command, argument, SDIO_RESPONSETYPE_LONG, &response[0], 1U);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }
    
    response[1] = sdio_response_get(SDIO_RESPONSE1);
    response[2] = sdio_response_get(SDIO_RESPONSE2);
    response[3] = sdio_response_get(SDIO_RESPONSE3);

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_command_r6(uint32_t command, uint32_t argument, uint32_t *response)
{
    board_sdio_status_t status;

    if (response == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    status = board_sdio_command(command, argument, SDIO_RESPONSETYPE_SHORT, response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }
    
    if (sdio_command_index_get() != (uint8_t)command)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }
    
    if((*response & 0x0000E000U) != 0U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    return BOARD_SDIO_STATUS_OK;
}

static board_sdio_status_t board_sdio_command_r3(
    uint32_t command,
    uint32_t argument,
    uint32_t *response)
{
    /*
     * R3 响应不检查 CRC
     */
    return board_sdio_command_internal(
        command,
        argument,
        SDIO_RESPONSETYPE_SHORT,
        response,
        0U);
}

board_sdio_status_t board_sdio_acmd41(uint32_t *ocr)
{
    uint32_t retry;
    uint32_t response;
    board_sdio_status_t status;

    if (ocr == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    *ocr = 0U;

    for (retry = 0U;
         retry < BOARD_SDIO_ACMD41_RETRY_COUNT;
         retry++)
    {
        /*
         * CMD55：声明下一条是应用命令
         */
        status = board_sdio_command(
            55U,
            0U,
            SDIO_RESPONSETYPE_SHORT,
            &response);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            return status;
        }

        /*
         * ACMD41：R3 响应，不检查 CRC
         */
        status = board_sdio_command_r3(
            41U,
            BOARD_SDIO_ACMD41_ARGUMENT,
            &response);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            return status;
        }

        *ocr = response;

        /*
         * OCR bit31：
         * 0 = 卡仍在上电初始化
         * 1 = 卡已经准备完成
         */
        if ((response & 0x80000000U) != 0U)
        {
            return BOARD_SDIO_STATUS_OK;
        }
    }

    return BOARD_SDIO_STATUS_TIMEOUT;
}

board_sdio_status_t board_sdio_set_bus_width_4bit(uint16_t rca, uint32_t *response)
{
    uint32_t cmd55_response;
    uint32_t acmd6_response;
    board_sdio_status_t status;

    if (rca == 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    cmd55_response = 0U;
    acmd6_response = 0U;

    status = board_sdio_command(55U, ((uint32_t)rca << 16), SDIO_RESPONSETYPE_SHORT, &cmd55_response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    if (sdio_command_index_get() != 55U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    if ((cmd55_response & 0x00000020U) == 0U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }
    
    if((cmd55_response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }
    
    status = board_sdio_command(6U, BOARD_SDIO_ACMD6_BUS_WIDTH_4BIT, SDIO_RESPONSETYPE_SHORT, &acmd6_response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    if (sdio_command_index_get() != 6U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    if((acmd6_response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    if (response != NULL)
    {
        *response = acmd6_response;
    }

    sdio_clock_config(SDIO_SDIOCLKEDGE_RISING, SDIO_CLOCKBYPASS_DISABLE, SDIO_CLOCKPWRSAVE_DISABLE, BOARD_SDIO_TRANSFER_CLOCK_DIVISION);

    sdio_bus_mode_set(SDIO_BUSMODE_4BIT);

    sdio_hardware_clock_disable();

    return BOARD_SDIO_STATUS_OK;
}

int board_sdio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOE);

    gpio_mode_set(BOARD_SDIO_CD_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BOARD_SDIO_CD_PIN);

    return 1;
}

uint8_t board_sdio_card_present(void)
{
    if (gpio_input_bit_get(BOARD_SDIO_CD_PORT, BOARD_SDIO_CD_PIN) == RESET)
    {
        return 1U;
    }

    return 0U;
}

int board_sdio_bus_init(void)
{
    uint32_t data_clock_pins;
    uint32_t command_pin;

    data_clock_pins = BOARD_SDIO_D0_PIN | BOARD_SDIO_D1_PIN | BOARD_SDIO_D2_PIN | BOARD_SDIO_D3_PIN | BOARD_SDIO_CLK_PIN;

    command_pin = BOARD_SDIO_CMD_PIN;

    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_SDIO);

    nvic_irq_enable(DMA1_Channel6_IRQn, 7U, 0U);
    nvic_irq_enable(SDIO_IRQn, 7U, 0U);

    gpio_af_set(BOARD_SDIO_D0_PORT, BOARD_SDIO_AF, data_clock_pins);
    gpio_mode_set(BOARD_SDIO_D0_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, data_clock_pins);
    gpio_output_options_set(BOARD_SDIO_D0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, data_clock_pins);

    gpio_af_set(BOARD_SDIO_CMD_PORT, BOARD_SDIO_AF, command_pin);
    gpio_mode_set(BOARD_SDIO_CMD_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, command_pin);
    gpio_output_options_set(BOARD_SDIO_CMD_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, command_pin);

    sdio_deinit();

    sdio_clock_config(SDIO_SDIOCLKEDGE_RISING, SDIO_CLOCKBYPASS_DISABLE, SDIO_CLOCKPWRSAVE_DISABLE, BOARD_SDIO_INIT_CLOCK_DIVISION);

    sdio_bus_mode_set(SDIO_BUSMODE_1BIT);

    sdio_power_state_set(SDIO_POWER_ON);

    sdio_clock_enable();

    return 1;
}

static board_sdio_status_t board_sdio_validate_r1(
    uint32_t command,
    uint32_t response)
{
    if (sdio_command_index_get() != (uint8_t)command)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    if ((response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_card_init(board_sdio_card_info_t *info)
{
    board_sdio_status_t status;

    if (info == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    (void)memset(info, 0, sizeof(*info));

    if (board_sdio_init() == 0)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }

    info->card_present = board_sdio_card_present();

    if (info->card_present == 0U)
    {
        return BOARD_SDIO_STATUS_NO_CARD;
    }

    if (board_sdio_bus_init() == 0)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }

    info->cmd0_status =
        (uint32_t)board_sdio_command(
            0U,
            0U,
            SDIO_RESPONSETYPE_NO,
            NULL);

    status = (board_sdio_status_t)info->cmd0_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    info->cmd8_response = 0U;
    info->cmd8_status =
        (uint32_t)board_sdio_command(
            8U,
            0x1AAU,
            SDIO_RESPONSETYPE_SHORT,
            &info->cmd8_response);

    status = (board_sdio_status_t)info->cmd8_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    if ((info->cmd8_response & 0x0FFFU) != 0x1AAU)
    {
        info->cmd8_status =
            (uint32_t)BOARD_SDIO_STATUS_COMMAND_ERROR;
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    info->acmd41_status =
        (uint32_t)board_sdio_acmd41(&info->ocr);

    status = (board_sdio_status_t)info->acmd41_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    if ((info->ocr & 0x40000000U) != 0U)
    {
        info->high_capacity = 1U;
    }

    info->cmd2_status =
        (uint32_t)board_sdio_command_r2(
            2U,
            0U,
            info->cid);

    status = (board_sdio_status_t)info->cmd2_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    info->cmd3_response = 0U;
    info->cmd3_status =
        (uint32_t)board_sdio_command_r6(
            3U,
            0U,
            &info->cmd3_response);

    status = (board_sdio_status_t)info->cmd3_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    info->rca = (uint16_t)(info->cmd3_response >> 16);

    info->cmd9_status =
        (uint32_t)board_sdio_command_r2(
            9U,
            ((uint32_t)info->rca << 16),
            info->csd);

    status = (board_sdio_status_t)info->cmd9_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    info->cmd7_response = 0U;
    info->cmd7_status =
        (uint32_t)board_sdio_command(
            7U,
            ((uint32_t)info->rca << 16),
            SDIO_RESPONSETYPE_SHORT,
            &info->cmd7_response);

    status = (board_sdio_status_t)info->cmd7_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    status = board_sdio_validate_r1(7U, info->cmd7_response);
    if (status != BOARD_SDIO_STATUS_OK)
    {
        info->cmd7_status = (uint32_t)status;
        return status;
    }

    info->cmd13_response = 0U;
    info->cmd13_status =
        (uint32_t)board_sdio_command(
            13U,
            ((uint32_t)info->rca << 16),
            SDIO_RESPONSETYPE_SHORT,
            &info->cmd13_response);

    status = (board_sdio_status_t)info->cmd13_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    status = board_sdio_validate_r1(13U, info->cmd13_response);
    if (status != BOARD_SDIO_STATUS_OK)
    {
        info->cmd13_status = (uint32_t)status;
        return status;
    }

    info->card_state =
        (uint8_t)((info->cmd13_response >> 9) & 0x0FU);

    info->acmd6_response = 0U;
    info->acmd6_status =
        (uint32_t)board_sdio_set_bus_width_4bit(
            info->rca,
            &info->acmd6_response);

    status = (board_sdio_status_t)info->acmd6_status;
    if (status != BOARD_SDIO_STATUS_OK)
    {
        return status;
    }

    info->bus_4bit = 1U;

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_read_block(
    uint32_t block_number,
    uint8_t *buffer)
{
    uint32_t response;
    uint32_t bytes_received;
    uint32_t timeout;
    uint32_t word_index;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    /*
     * 清除上一次数据传输配置和标志
     */
    board_sdio_data_cleanup();

    /*
     * 配置 512 字节数据块
     */
    sdio_data_config(
        0xFFFFFFFFU,
        BOARD_SDIO_BLOCK_SIZE,
        SDIO_DATABLOCKSIZE_512BYTES);

    /*
     * 卡到主机的数据传输
     */
    sdio_data_transfer_config(
        SDIO_TRANSMODE_BLOCK,
        SDIO_TRANSDIRECTION_TOSDIO);

    bytes_received = 0U;
    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    /*
     * 先打开数据状态机，
     * 再发送 CMD17
     */
    sdio_dsm_enable();

    response = 0U;

    status = board_sdio_command(
        17U,
        block_number,
        SDIO_RESPONSETYPE_SHORT,
        &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_data_cleanup();
        return status;
    }

    /*
     * 确认 CMD17 响应命令编号正确
     */
    if (sdio_command_index_get() != 17U)
    {
        board_sdio_data_cleanup();
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    /*
     * 检查 R1 错误位
     */
    if ((response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        board_sdio_data_cleanup();
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    /*
     * 轮询接收数据
     */
    while (RESET ==
           sdio_flag_get(SDIO_FLAG_DTBLKEND))
    {
        /*
         * 数据 CRC 错误
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_DTCRCERR))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * 数据超时
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_DTTMOUT))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        /*
         * FIFO 溢出
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_RXORE))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * 起始位错误
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_STBITE))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * FIFO 半满：一次读取 8 个 32 位数据
         */
        if ((bytes_received + 32U) <=
            BOARD_SDIO_BLOCK_SIZE)
        {
            if (SET ==
                sdio_flag_get(SDIO_FLAG_RFH))
            {
                for (word_index = 0U;
                     word_index < BOARD_SDIO_FIFO_WORD_COUNT;
                     word_index++)
                {
                    status =
                        board_sdio_read_fifo_word(
                            buffer,
                            &bytes_received);

                    if (status != BOARD_SDIO_STATUS_OK)
                    {
                        board_sdio_data_cleanup();
                        return status;
                    }
                }

                continue;
            }
        }

        /*
         * FIFO 中还有数据，但不足半满
         */
        if ((bytes_received + 4U) <=
            BOARD_SDIO_BLOCK_SIZE)
        {
            if (SET ==
                sdio_flag_get(SDIO_FLAG_RXDTVAL))
            {
                status =
                    board_sdio_read_fifo_word(
                        buffer,
                        &bytes_received);

                if (status != BOARD_SDIO_STATUS_OK)
                {
                    board_sdio_data_cleanup();
                    return status;
                }

                continue;
            }
        }

        /*
         * 数据已经读取完，但等待 DTBLKEND
         */
        if (timeout == 0U)
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        timeout--;
    }

    /*
     * DTBLKEND 置位后，
     * 继续读取 FIFO 中剩余的数据
     */
    while (SET ==
           sdio_flag_get(SDIO_FLAG_RXDTVAL))
    {
        if ((bytes_received + 4U) >
            BOARD_SDIO_BLOCK_SIZE)
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        status =
            board_sdio_read_fifo_word(
                buffer,
                &bytes_received);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            board_sdio_data_cleanup();
            return status;
        }
    }

    board_sdio_data_cleanup();

    if (bytes_received != BOARD_SDIO_BLOCK_SIZE)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_read_block_dma_polling(uint32_t block_number, uint8_t *buffer)
{
    uint32_t response;
    uint32_t timeout;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    /*
     * DMA 32 位传输要求缓冲区至少 4 字节对齐
     */
    if ((((uint32_t)buffer) & 0x03U) != 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    s_board_sdio_dma_irq_events = 0U;
    s_board_sdio_irq_events = 0U;

    /*
     * 清理上一次 SDIO 数据传输状态
     */
    board_sdio_data_cleanup();

    /*
     * 配置 DMA1 Channel6
     */
    board_sdio_dma_config(buffer, DMA_PERIPH_TO_MEMORY);

    /*
     * 配置 SDIO 接收 512 字节
     */
    sdio_data_config(0xFFFFFFFFU, BOARD_SDIO_BLOCK_SIZE, SDIO_DATABLOCKSIZE_512BYTES);

    sdio_data_transfer_config(SDIO_TRANSMODE_BLOCK, SDIO_TRANSDIRECTION_TOSDIO);

    sdio_interrupt_enable(SDIO_INT_DTEND | SDIO_INT_DTBLKEND);

    /*
     * 打开 SDIO DMA 请求
     */
    sdio_dma_enable();

    /*
     * 读数据时，必须先启动数据状态机，
     * 再发送 CMD17
     */
    sdio_dsm_enable();

    response = 0U;

    status = board_sdio_command(17U, block_number, SDIO_RESPONSETYPE_SHORT, &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_data_cleanup();
        board_sdio_dma_cleanup();

        return status;
    }

    /*
     * 检查 CMD17 命令编号
     */
    if (sdio_command_index_get() != 17U)
    {
        board_sdio_data_cleanup();
        board_sdio_dma_cleanup();

        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    /*
     * 检查 CMD17 的 R1 错误位
     */
    if ((response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        board_sdio_data_cleanup();
        board_sdio_dma_cleanup();

        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    /*
     * 等待 DMA 完成
     */
    while (RESET == dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_FTF) &&
           ((s_board_sdio_dma_irq_events & BOARD_SDIO_DMA_IRQ_EVENT_FTF) == 0U))
    {
        /*
         * DMA FIFO 错误
         */
        if (SET ==
            dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_FEE))
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * DMA 单数据模式错误
         */
        if (SET ==
            dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_SDE))
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * DMA 访问错误
         */
        if (SET ==
            dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_TAE))
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * SDIO 数据错误
         */
        if (SET == sdio_flag_get(SDIO_FLAG_DTCRCERR)   ||
            SET == sdio_flag_get(SDIO_FLAG_DTTMOUT)    ||
            SET == sdio_flag_get(SDIO_FLAG_RXORE)      ||
            SET == sdio_flag_get(SDIO_FLAG_STBITE))
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        if (timeout == 0U)
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        timeout--;
    }

    /*
     * DMA 已经搬完 512 字节，
     * 等待 SDIO 数据块结束
     */
    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    while (RESET ==
           sdio_flag_get(SDIO_FLAG_DTBLKEND))
    {
        if (SET == sdio_flag_get(SDIO_FLAG_DTCRCERR) ||
            SET == sdio_flag_get(SDIO_FLAG_DTTMOUT) ||
            SET == sdio_flag_get(SDIO_FLAG_RXORE) ||
            SET == sdio_flag_get(SDIO_FLAG_STBITE))
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        if (timeout == 0U)
        {
            board_sdio_data_cleanup();
            board_sdio_dma_cleanup();

            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        timeout--;
    }

    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_write_block_dma_polling(uint32_t block_number,const uint8_t *buffer)
{
    uint32_t response;
    uint32_t timeout;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }
    
    if((((uint32_t)buffer) & 0x03U) != 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    s_board_sdio_dma_irq_events = 0U;
    s_board_sdio_irq_events = 0U;

    board_sdio_data_cleanup();

    board_sdio_dma_write_config(buffer);

    response = 0;

    status = board_sdio_command(24U, block_number, SDIO_RESPONSETYPE_SHORT, &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        goto dma_write_cleanup;
    }
    
    if (sdio_command_index_get() != 24U)
    {
        status = BOARD_SDIO_STATUS_COMMAND_ERROR;
        goto dma_write_cleanup;
    }
    
    if ((response & BOARD_SDIO_R1_ERROR_MASK) != 0)
    {
        status = BOARD_SDIO_STATUS_COMMAND_ERROR;
        goto dma_write_cleanup;
    }

    sdio_data_config(0xFFFFFFFFU, BOARD_SDIO_BLOCK_SIZE, SDIO_DATABLOCKSIZE_512BYTES);

    sdio_data_transfer_config(SDIO_TRANSMODE_BLOCK, SDIO_TRANSDIRECTION_TOCARD);
    
    sdio_interrupt_enable(SDIO_INT_DTEND | SDIO_INT_DTBLKEND);

    sdio_dma_enable();

    sdio_dsm_enable();

    /*
     * CMD24 已经返回，且 SDIO 数据状态机和 DMA 请求均已打开，
     * 此时再使能 DMA 通道，避免提前填充发送 FIFO。
     */
    dma_channel_enable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL);

    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    while (RESET == dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_FTF) &&
            ((s_board_sdio_dma_irq_events & BOARD_SDIO_DMA_IRQ_EVENT_FTF) == 0U))
    {
        status = board_sdio_dma_fifo_check();

        if (status != BOARD_SDIO_STATUS_OK)
        {
            goto dma_write_cleanup;
        }
        if (SET == dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_SDE))
        {
            status = BOARD_SDIO_STATUS_DATA_ERROR;
            goto dma_write_cleanup;
        }
        if (SET == dma_flag_get(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_FLAG_TAE))
        {
            status = BOARD_SDIO_STATUS_DATA_ERROR;
            goto dma_write_cleanup;
        }
        if (SET == sdio_flag_get(SDIO_FLAG_DTTMOUT))
        {
            status = BOARD_SDIO_STATUS_TIMEOUT;
            goto dma_write_cleanup;
        }
        if ((SET == sdio_flag_get(SDIO_FLAG_DTCRCERR)) ||
            (SET == sdio_flag_get(SDIO_FLAG_TXURE))    ||
            (SET == sdio_flag_get(SDIO_FLAG_STBITE)))    
        {
            status = BOARD_SDIO_STATUS_DATA_ERROR;
            goto dma_write_cleanup;
        }

        if (timeout == 0U)
        {
            status = BOARD_SDIO_STATUS_TIMEOUT;
            goto dma_write_cleanup;
        }
        timeout--;
    }
    
    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    while (RESET == sdio_flag_get(SDIO_FLAG_DTBLKEND))
    {
        if (SET == sdio_flag_get(SDIO_FLAG_DTTMOUT))
        {
            status = BOARD_SDIO_STATUS_TIMEOUT;
            goto dma_write_cleanup;
        }

        if ((SET == sdio_flag_get(SDIO_FLAG_DTCRCERR)) ||
            (SET == sdio_flag_get(SDIO_FLAG_TXURE))    ||
            (SET == sdio_flag_get(SDIO_FLAG_STBITE)))    
        {
            status = BOARD_SDIO_STATUS_DATA_ERROR;
            goto dma_write_cleanup;
        }

        if (timeout == 0U)
        {
            status = BOARD_SDIO_STATUS_TIMEOUT;
            goto dma_write_cleanup;
        }
        timeout--;
    }
    
    status = BOARD_SDIO_STATUS_OK;

dma_write_cleanup:
    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();

    return status;
}

board_sdio_status_t board_sdio_dma_read_start(
    uint32_t block_number,
    uint8_t *buffer)
{
    uint32_t response;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if ((((uint32_t)buffer) & 0x03U) != 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if (s_board_sdio_dma_busy != 0U)
    {
        return BOARD_SDIO_STATUS_BUSY;
    }

    s_board_sdio_dma_irq_events = 0U;
    s_board_sdio_irq_events = 0U;
    s_board_sdio_dma_fee_seen = 0U;
    s_board_sdio_dma_fee_chen_snapshot = 0U;
    s_board_sdio_dma_fee_chen_off_seen = 0U;
    s_board_sdio_dma_busy = 1U;

    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();

    board_sdio_dma_config(buffer, DMA_PERIPH_TO_MEMORY);
    board_sdio_dma_async_interrupt_enable();

    sdio_data_config(
        0xFFFFFFFFU,
        BOARD_SDIO_BLOCK_SIZE,
        SDIO_DATABLOCKSIZE_512BYTES);

    sdio_data_transfer_config(
        SDIO_TRANSMODE_BLOCK,
        SDIO_TRANSDIRECTION_TOSDIO);

    board_sdio_data_async_interrupt_enable();

    sdio_dma_enable();
    sdio_dsm_enable();

    response = 0U;
    status = board_sdio_command(
        17U,
        block_number,
        SDIO_RESPONSETYPE_SHORT,
        &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_dma_transfer_abort();
        return status;
    }

    status = board_sdio_validate_r1(17U, response);
    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_dma_transfer_abort();
        return status;
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_dma_write_start(
    uint32_t block_number,
    const uint8_t *buffer)
{
    uint32_t response;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if ((((uint32_t)buffer) & 0x03U) != 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    if (s_board_sdio_dma_busy != 0U)
    {
        return BOARD_SDIO_STATUS_BUSY;
    }

    s_board_sdio_dma_irq_events = 0U;
    s_board_sdio_irq_events = 0U;
    s_board_sdio_dma_fee_seen = 0U;
    s_board_sdio_dma_fee_chen_snapshot = 0U;
    s_board_sdio_dma_fee_chen_off_seen = 0U;
    s_board_sdio_dma_busy = 1U;

    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();

    board_sdio_dma_write_config(buffer);
    board_sdio_dma_async_interrupt_enable();

    response = 0U;
    status = board_sdio_command(
        24U,
        block_number,
        SDIO_RESPONSETYPE_SHORT,
        &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_dma_transfer_abort();
        return status;
    }

    status = board_sdio_validate_r1(24U, response);
    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_dma_transfer_abort();
        return status;
    }

    sdio_data_config(
        0xFFFFFFFFU,
        BOARD_SDIO_BLOCK_SIZE,
        SDIO_DATABLOCKSIZE_512BYTES);

    sdio_data_transfer_config(
        SDIO_TRANSMODE_BLOCK,
        SDIO_TRANSDIRECTION_TOCARD);

    board_sdio_data_async_interrupt_enable();

    sdio_dma_enable();
    sdio_dsm_enable();

    dma_channel_enable(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL);

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_dma_transfer_finish(void)
{
    uint32_t dma_error_events;
    uint32_t sdio_error_events;
    uint32_t data_counter;
    uint8_t dma_complete;
    uint8_t sdio_complete;
    uint8_t data_complete;
    uint8_t fifo_chen_off;
    board_sdio_status_t status;

    if (s_board_sdio_dma_busy == 0U)
    {
        return BOARD_SDIO_STATUS_NOT_READY;
    }

    dma_error_events =
        BOARD_SDIO_DMA_IRQ_EVENT_FATAL |
        BOARD_SDIO_DMA_IRQ_EVENT_SDE   |
        BOARD_SDIO_DMA_IRQ_EVENT_TAE;

    sdio_error_events =
        BOARD_SDIO_IRQ_EVENT_DTCRCERR |
        BOARD_SDIO_IRQ_EVENT_DTTMOUT  |
        BOARD_SDIO_IRQ_EVENT_TXURE    |
        BOARD_SDIO_IRQ_EVENT_RXORE    |
        BOARD_SDIO_IRQ_EVENT_STBITE;

    dma_complete = (uint8_t)
        ((s_board_sdio_dma_irq_events &
          BOARD_SDIO_DMA_IRQ_EVENT_FTF) != 0U);
    sdio_complete = (uint8_t)
        ((s_board_sdio_irq_events &
          BOARD_SDIO_IRQ_EVENT_DTBLKEND) != 0U);
    data_counter = sdio_data_counter_get();
    data_complete = (uint8_t)(data_counter == 0U);
    fifo_chen_off = (uint8_t)
        ((s_board_sdio_dma_fee_seen != 0U) &&
         (s_board_sdio_dma_fee_chen_off_seen != 0U));

    if ((s_board_sdio_dma_irq_events & dma_error_events) != 0U)
    {
        status = BOARD_SDIO_STATUS_DATA_ERROR;
    }
    else if ((s_board_sdio_irq_events & sdio_error_events) != 0U)
    {
        if ((s_board_sdio_irq_events & BOARD_SDIO_IRQ_EVENT_DTTMOUT) != 0U)
        {
            status = BOARD_SDIO_STATUS_TIMEOUT;
        }
        else
        {
            status = BOARD_SDIO_STATUS_DATA_ERROR;
        }
    }
    else if ((fifo_chen_off != 0U) &&
             (dma_complete == 0U))
    {
        /*
         * FEE 到达时 CHEN 已关闭且 DMA 尚未报告完成，
         * 判定为真实 FIFO 错误。
         *
         * 如果 DMA 已经报告 FTF，但 SDIO 的 DTBLKEND 还没有
         * 到达，则先继续等待，避免中断先后顺序造成误判；
         * 后续由 DTBLKEND/DATACNT 或超时完成收口。
         */
        status = BOARD_SDIO_STATUS_DATA_ERROR;
    }
    else if (dma_complete == 0U)
    {
        return BOARD_SDIO_STATUS_BUSY;
    }
    else if ((sdio_complete == 0U) ||
             (data_complete == 0U))
    {
        return BOARD_SDIO_STATUS_BUSY;
    }
    else
    {
        /*
         * FEE 已经记录在诊断事件中，但在 DMA/SDIO 均报告完成且
         * 没有其他错误时，不将它单独判为传输失败。
         */
        status = BOARD_SDIO_STATUS_OK;
    }

    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();
    s_board_sdio_dma_busy = 0U;

    return status;
}

void board_sdio_dma_transfer_abort(void)
{
    board_sdio_data_cleanup();
    board_sdio_dma_cleanup();
    s_board_sdio_dma_busy = 0U;
}

uint8_t board_sdio_dma_transfer_busy(void)
{
    return s_board_sdio_dma_busy;
}

board_sdio_status_t board_sdio_write_block(uint32_t block_number, const uint8_t *buffer)
{
    uint32_t response;
    uint32_t bytes_sent;
    uint32_t timeout;
    uint32_t word_index;
    board_sdio_status_t status;

    if (buffer == NULL)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    /*
     * 清除上一次数据传输配置
     */
    board_sdio_data_cleanup();

    /*
     * 发送 CMD24
     *
     * 当前卡是高容量卡，
     * 因此 block_number 直接作为命令参数。
     */
    response = 0U;

    status = board_sdio_command(
        24U,
        block_number,
        SDIO_RESPONSETYPE_SHORT,
        &response);

    if (status != BOARD_SDIO_STATUS_OK)
    {
        board_sdio_data_cleanup();
        return status;
    }

    /*
     * 确认 CMD24 响应命令编号
     */
    if (sdio_command_index_get() != 24U)
    {
        board_sdio_data_cleanup();
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    /*
     * 检查 R1 错误位
     */
    if ((response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
    {
        board_sdio_data_cleanup();
        return BOARD_SDIO_STATUS_COMMAND_ERROR;
    }

    /*
     * 配置 512 字节数据块
     */
    sdio_data_config(
        0xFFFFFFFFU,
        BOARD_SDIO_BLOCK_SIZE,
        SDIO_DATABLOCKSIZE_512BYTES);

    /*
     * 主机向 TF 卡发送数据
     */
    sdio_data_transfer_config(
        SDIO_TRANSMODE_BLOCK,
        SDIO_TRANSDIRECTION_TOCARD);

    bytes_sent = 0U;
    timeout = BOARD_SDIO_DATA_POLL_TIMEOUT;

    /*
     * 启动数据状态机
     */
    sdio_dsm_enable();

    /*
     * 轮询写入 FIFO
     */
    while (RESET ==
           sdio_flag_get(SDIO_FLAG_DTBLKEND))
    {
        /*
         * 数据 CRC 错误
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_DTCRCERR))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * 数据超时
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_DTTMOUT))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        /*
         * FIFO 下溢
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_TXURE))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * 起始位错误
         */
        if (SET ==
            sdio_flag_get(SDIO_FLAG_STBITE))
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_DATA_ERROR;
        }

        /*
         * FIFO 可以写入数据
         */
        if ((SET ==
             sdio_flag_get(SDIO_FLAG_TFH)) ||
            (SET ==
             sdio_flag_get(SDIO_FLAG_TFE)))
        {
            for (word_index = 0U;
                 word_index < BOARD_SDIO_FIFO_WORD_COUNT;
                 word_index++)
            {
                if (bytes_sent >= BOARD_SDIO_BLOCK_SIZE)
                {
                    break;
                }

                status =
                    board_sdio_write_fifo_word(
                        buffer,
                        &bytes_sent);

                if (status != BOARD_SDIO_STATUS_OK)
                {
                    board_sdio_data_cleanup();
                    return status;
                }
            }

            continue;
        }

        /*
         * 等待 FIFO 状态变化或数据结束
         */
        if (timeout == 0U)
        {
            board_sdio_data_cleanup();
            return BOARD_SDIO_STATUS_TIMEOUT;
        }

        timeout--;
    }

    board_sdio_data_cleanup();

    if (bytes_sent != BOARD_SDIO_BLOCK_SIZE)
    {
        return BOARD_SDIO_STATUS_DATA_ERROR;
    }

    return BOARD_SDIO_STATUS_OK;
}

board_sdio_status_t board_sdio_wait_card_ready(uint16_t rca, uint32_t *response)
{
    uint32_t retry;
    uint32_t card_response;
    board_sdio_status_t status;

    if (rca == 0U)
    {
        return BOARD_SDIO_STATUS_INVALID_ARGUMENT;
    }

    card_response = 0;
    
    for (retry = 0; retry < BOARD_SDIO_CARD_READY_RETRY_COUNT; retry++)
    {
        status = board_sdio_command(13U, ((uint32_t)rca << 16), SDIO_RESPONSETYPE_SHORT, &card_response);

        if (status != BOARD_SDIO_STATUS_OK)
        {
            return status;
        }

        if (sdio_command_index_get() != 13U)
        {
            return BOARD_SDIO_STATUS_COMMAND_ERROR;
        }
        
        if ((card_response & BOARD_SDIO_R1_ERROR_MASK) != 0U)
        {
            return BOARD_SDIO_STATUS_COMMAND_ERROR;
        }
        
        if (((card_response & BOARD_SDIO_R1_READY_FOR_DATA) != 0U) && ((card_response & BOARD_SDIO_R1_CURRENT_STATE_MASK) == BOARD_SDIO_CARD_STATE_TRANSFER))
        {
            if (response != NULL)
            {
                *response = card_response;
            }
            return BOARD_SDIO_STATUS_OK;
        }
    }

    if (response != NULL)
    {
        *response = card_response;
    }
    
    return BOARD_SDIO_STATUS_TIMEOUT;
}

void board_sdio_dma_irq_handler(void)
{
    uint32_t events;
    uint32_t channel_control;
    FlagStatus ftf_flag;
    FlagStatus fee_flag;
    FlagStatus sde_flag;
    FlagStatus tae_flag;

    events = 0U;
    channel_control = DMA_CHCTL(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL);

    /*
     * 先一次性采样所有 DMA 中断状态，再清除硬件标志。
     * 这样 FEE 到达时的 CHEN 快照不会被前面的 FTF 清除动作影响。
     */
    ftf_flag = dma_interrupt_flag_get(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FLAG_FTF);
    fee_flag = dma_interrupt_flag_get(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FLAG_FEE);
    sde_flag = dma_interrupt_flag_get(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FLAG_SDE);
    tae_flag = dma_interrupt_flag_get(
        BOARD_SDIO_DMA_PERIPH,
        BOARD_SDIO_DMA_CHANNEL,
        DMA_INT_FLAG_TAE);

    if (SET == ftf_flag)
    {
        dma_interrupt_flag_clear(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_INT_FLAG_FTF);

        events |= BOARD_SDIO_DMA_IRQ_EVENT_FTF;
    }

    if (SET == fee_flag)
    {
        dma_interrupt_flag_clear(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_INT_FLAG_FEE);

        events |= BOARD_SDIO_DMA_IRQ_EVENT_FEE;
        s_board_sdio_dma_fee_seen = 1U;
        s_board_sdio_dma_fee_chen_snapshot = (uint8_t)
            ((channel_control & DMA_CHXCTL_CHEN) != 0U);

        if (s_board_sdio_dma_fee_chen_snapshot == 0U)
        {
            s_board_sdio_dma_fee_chen_off_seen = 1U;
        }

        /*
         * FEE 只在这里记录，并保存当时的 CHEN 快照；
         * 是否为真正 FIFO 错误由任务上下文结合完整传输条件判断。
         */
    }

    if (SET == sde_flag)
    {
        dma_interrupt_flag_clear(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_INT_FLAG_SDE);

        events |= BOARD_SDIO_DMA_IRQ_EVENT_SDE;
    }

    if (SET == tae_flag)
    {
        dma_interrupt_flag_clear(BOARD_SDIO_DMA_PERIPH, BOARD_SDIO_DMA_CHANNEL, DMA_INT_FLAG_TAE);

        events |= BOARD_SDIO_DMA_IRQ_EVENT_TAE;
    }

    if (events != 0)
    {
        s_board_sdio_dma_irq_events |= events;
        s_board_sdio_dma_irq_count++;

        if (s_board_sdio_irq_callback != NULL)
        {
            s_board_sdio_irq_callback(events, 0U);
        }
    }
    
}

void board_sdio_irq_handler(void)
{
    uint32_t events;

    events = 0U;

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_DTEND))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_DTEND);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_DTEND);
        }

        events |= BOARD_SDIO_IRQ_EVENT_DTEND;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_DTBLKEND))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_DTBLKEND);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_DTBLKEND);
        }

        events |= BOARD_SDIO_IRQ_EVENT_DTBLKEND;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_DTCRCERR))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_DTCRCERR);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_DTCRCERR);
        }

        events |= BOARD_SDIO_IRQ_EVENT_DTCRCERR;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_DTTMOUT))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_DTTMOUT);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_DTTMOUT);
        }

        events |= BOARD_SDIO_IRQ_EVENT_DTTMOUT;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_TXURE))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_TXURE);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_TXURE);
        }

        events |= BOARD_SDIO_IRQ_EVENT_TXURE;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_RXORE))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_RXORE);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_RXORE);
        }

        events |= BOARD_SDIO_IRQ_EVENT_RXORE;
    }

    if (SET == sdio_interrupt_flag_get(SDIO_INT_FLAG_STBITE))
    {
        if (s_board_sdio_dma_busy != 0U)
        {
            sdio_interrupt_flag_clear(SDIO_INT_FLAG_STBITE);
        }
        else
        {
            sdio_interrupt_disable(SDIO_INT_STBITE);
        }

        events |= BOARD_SDIO_IRQ_EVENT_STBITE;
    }

    if (events != 0U)
    {
        s_board_sdio_irq_events |= events;
        s_board_sdio_irq_count++;

        if (s_board_sdio_irq_callback != NULL)
        {
            s_board_sdio_irq_callback(0U, events);
        }
    }
}
