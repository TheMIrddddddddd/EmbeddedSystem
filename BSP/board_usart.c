#include <stddef.h>
#include <stdio.h>

#include "board_usart.h"
#include "board_config.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_usart.h"
#include "gd32f4xx_misc.h"
#include "gd32f4xx_dma.h"
#include "ringbuffer.h"


#define BOARD_USART0_RX_RINGBUFFER_SIZE     512U
#define BOARD_USART0_TX_RINGBUFFER_SIZE     512U

#define BOARD_USART1_RS485_RX_RINGBUFFER_SIZE     2048U
#define BOARD_USART1_RS485_TX_RINGBUFFER_SIZE     512U

#define BOARD_USART0_RX_DMA_BUFFER_SIZE     512U
#define BOARD_USART0_RX_DMA_PERIPH          DMA1
#define BOARD_USART0_RX_DMA_CHANNEL         DMA_CH2
#define BOARD_USART0_RX_DMA_SUBPERIPH       DMA_SUBPERI4

#define BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE     512U
#define BOARD_USART1_RS485_RX_DMA_PERIPH          DMA0
#define BOARD_USART1_RS485_RX_DMA_CHANNEL         DMA_CH5
#define BOARD_USART1_RS485_RX_DMA_SUBPERIPH       DMA_SUBPERI4


static struct rt_ringbuffer s_usart0_rx_ringbuffer;
static rt_uint8_t s_usart0_rx_ringbuffer_pool[BOARD_USART0_RX_RINGBUFFER_SIZE];
static struct rt_ringbuffer s_usart0_tx_ringbuffer;
static rt_uint8_t s_usart0_tx_ringbuffer_pool[BOARD_USART0_TX_RINGBUFFER_SIZE];

static struct rt_ringbuffer s_usart1_rs485_rx_ringbuffer;
static rt_uint8_t s_usart1_rs485_rx_ringbuffer_pool[BOARD_USART1_RS485_RX_RINGBUFFER_SIZE];
static struct rt_ringbuffer s_usart1_rs485_tx_ringbuffer;
static rt_uint8_t s_usart1_rs485_tx_ringbuffer_pool[BOARD_USART1_RS485_TX_RINGBUFFER_SIZE];

static rt_uint8_t s_usart0_rx_dma_buffer[BOARD_USART0_RX_DMA_BUFFER_SIZE];
static volatile uint16_t s_usart0_rx_dma_last_pos;

static rt_uint8_t s_usart1_rs485_rx_dma_buffer[BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE];
static volatile uint16_t s_usart1_rs485_rx_dma_last_pos;

static volatile uint32_t s_usart0_rx_overflow_count;
static volatile uint32_t s_usart0_tx_drop_count;
static volatile uint32_t s_usart0_rx_dma_error_count;

static volatile uint32_t s_usart1_rs485_rx_overflow_count;
static volatile uint32_t s_usart1_rs485_tx_drop_count;
static volatile uint32_t s_usart1_rs485_rx_dma_error_count;

static void board_usart0_rx_dma_init(void);
static void board_usart0_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length);
static void board_usart0_rx_dma_commit(void);

static void board_usart1_rs485_rx_dma_init(void);
static void board_usart1_rs485_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length);
static void board_usart1_rs485_rx_dma_commit(void);

#if defined(__ARMCC_VERSION)
#pragma import(__use_no_semihosting)
#endif

struct __FILE
{
    int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
    (void)x;
}

int fputc(int ch, FILE *f)
{
    (void)f;

    board_usart0_send_byte((uint8_t)ch);

    return ch;
}

static void board_usart0_rx_dma_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA1);
    dma_deinit(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL);

    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.periph_addr = (uint32_t)&USART_DATA(USART0);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr = (uint32_t)s_usart0_rx_dma_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number = BOARD_USART0_RX_DMA_BUFFER_SIZE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, BOARD_USART0_RX_DMA_SUBPERIPH);
    dma_interrupt_enable(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_HTF | DMA_INT_FTF);
    dma_interrupt_enable(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_SDE | DMA_INT_TAE);
    dma_interrupt_enable(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FEE);
    dma_channel_enable(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL);
    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
}

void board_usart0_init(void)
{
    rt_ringbuffer_init(&s_usart0_rx_ringbuffer, s_usart0_rx_ringbuffer_pool, BOARD_USART0_RX_RINGBUFFER_SIZE);
    rt_ringbuffer_init(&s_usart0_tx_ringbuffer, s_usart0_tx_ringbuffer_pool, BOARD_USART0_TX_RINGBUFFER_SIZE);

    s_usart0_rx_dma_last_pos = 0U;

    uint32_t usart0_pins;

    usart0_pins = BOARD_USART0_TX_PIN | BOARD_USART0_RX_PIN;

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    gpio_af_set(BOARD_USART0_PORT, BOARD_USART0_AF, usart0_pins);
    gpio_mode_set(BOARD_USART0_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, usart0_pins);
    gpio_output_options_set(BOARD_USART0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, usart0_pins);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_stop_bit_set(USART0, USART_STB_1BIT);

    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);

    usart_enable(USART0);

    board_usart0_rx_dma_init();

    nvic_irq_enable(USART0_IRQn, 1U, 0U);
    nvic_irq_enable(DMA1_Channel2_IRQn, 1U, 1U);
    usart_interrupt_disable(USART0, USART_INT_RBNE);
    usart_interrupt_enable(USART0, USART_INT_IDLE);
}

static void board_usart1_rs485_rx_dma_init(void)
{   
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA0);
    dma_deinit(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL);

    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.periph_addr = (uint32_t)&USART_DATA(USART1);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr = (uint32_t)s_usart1_rs485_rx_dma_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number = BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, &dma_init_struct);
    dma_channel_subperipheral_select(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, BOARD_USART1_RS485_RX_DMA_SUBPERIPH);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_HTF | DMA_INT_FTF);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_SDE | DMA_INT_TAE);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FEE);
    dma_channel_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);

    nvic_irq_enable(USART1_IRQn, 1U, 0U);
    nvic_irq_enable(DMA0_Channel5_IRQn, 1U, 1U);
    usart_interrupt_disable(USART1, USART_INT_RBNE);
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}

void board_usart1_rs485_init(void)
{
    rt_ringbuffer_init(&s_usart1_rs485_rx_ringbuffer, s_usart1_rs485_rx_ringbuffer_pool, BOARD_USART1_RS485_RX_RINGBUFFER_SIZE);
    rt_ringbuffer_init(&s_usart1_rs485_tx_ringbuffer, s_usart1_rs485_tx_ringbuffer_pool, BOARD_USART1_RS485_TX_RINGBUFFER_SIZE);

    s_usart1_rs485_rx_dma_last_pos = 0U;

    uint32_t usart1_pins;

    usart1_pins = BOARD_USART1_TX_PIN | BOARD_USART1_RX_PIN;

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART1);

    gpio_af_set(BOARD_USART1_PORT, BOARD_USART1_AF, usart1_pins);
    gpio_mode_set(BOARD_USART1_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, usart1_pins);
    gpio_output_options_set(BOARD_USART1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, usart1_pins);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200U);
    usart_word_length_set(USART1, USART_WL_8BIT);
    usart_parity_config(USART1, USART_PM_NONE);
    usart_stop_bit_set(USART1, USART_STB_1BIT);

    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);

    gpio_mode_set(BOARD_RS485_DIR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BOARD_RS485_DIR_PIN);
    gpio_output_options_set(BOARD_RS485_DIR_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BOARD_RS485_DIR_PIN);

    gpio_bit_reset(BOARD_RS485_DIR_PORT, BOARD_RS485_DIR_PIN);

    board_usart1_rs485_rx_dma_init();

    usart_enable(USART1);
}

void board_usart0_send_byte(uint8_t data)
{
    (void)board_usart0_send_buffer(&data, 1U);
}

uint16_t board_usart0_send_buffer(const uint8_t *data, uint16_t length)
{
    rt_size_t queued;

    if ((data == NULL) || (length == 0U))
    {
        return 0U;
    }

    queued = rt_ringbuffer_put(&s_usart0_tx_ringbuffer, data, length);

    if (queued != 0U)
    {
        usart_interrupt_enable(USART0, USART_INT_TBE);
    }

    if (queued < (rt_size_t)length)
    {
        s_usart0_tx_drop_count += (uint32_t)((rt_size_t)length - queued);
    }

    return (uint16_t)queued;
}

static void board_usart0_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length)
{
    rt_size_t queued;

    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    queued = rt_ringbuffer_put(&s_usart0_rx_ringbuffer, data, length);

    if (queued < (rt_size_t)length)
    {
        s_usart0_rx_overflow_count += (uint32_t)((rt_size_t)length - queued);
    }
}

static void board_usart1_rs485_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length)
{
    rt_size_t queued;

    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    queued = rt_ringbuffer_put(&s_usart1_rs485_rx_ringbuffer, data, length);

    if (queued < (rt_size_t)length)
    {
        s_usart1_rs485_rx_overflow_count += (uint32_t)((rt_size_t)length - queued);
    }
}

static void board_usart0_rx_dma_commit(void)
{
    uint32_t current_pos;
    uint16_t last_pos;

    current_pos = BOARD_USART0_RX_DMA_BUFFER_SIZE - dma_transfer_number_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL);

    if (current_pos >= BOARD_USART0_RX_DMA_BUFFER_SIZE)
    {
        current_pos = 0U;
    }

    last_pos = s_usart0_rx_dma_last_pos;

    if (current_pos == (uint32_t)last_pos)
    {
        return;
    }
    
    if (current_pos > (uint32_t)last_pos)
    {
        board_usart0_rx_dma_commit_block(&s_usart0_rx_dma_buffer[last_pos], (uint16_t)(current_pos - (uint32_t)last_pos));
    }
    else
    {
        board_usart0_rx_dma_commit_block(&s_usart0_rx_dma_buffer[last_pos], (uint16_t)(BOARD_USART0_RX_DMA_BUFFER_SIZE - last_pos));
        board_usart0_rx_dma_commit_block(&s_usart0_rx_dma_buffer[0], (uint16_t)current_pos);
    }

    s_usart0_rx_dma_last_pos = (uint16_t)current_pos;
}

static void board_usart1_rs485_rx_dma_commit(void)
{
    uint32_t current_pos;
    uint16_t last_pos;

    current_pos = BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE - dma_transfer_number_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL);

    if (current_pos >= BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE)
    {
        current_pos = 0U;
    }

    last_pos = s_usart1_rs485_rx_dma_last_pos;

    if (current_pos == (uint32_t)last_pos)
    {
        return;
    }
    
    if (current_pos > (uint32_t)last_pos)
    {
        board_usart1_rs485_rx_dma_commit_block(&s_usart1_rs485_rx_dma_buffer[last_pos], (uint16_t)(current_pos - (uint32_t)last_pos));
    }
    else
    {
        board_usart1_rs485_rx_dma_commit_block(&s_usart1_rs485_rx_dma_buffer[last_pos], (uint16_t)(BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE - last_pos));
        board_usart1_rs485_rx_dma_commit_block(&s_usart1_rs485_rx_dma_buffer[0], (uint16_t)current_pos);
    }

    s_usart1_rs485_rx_dma_last_pos = (uint16_t)current_pos;
}

uint8_t board_usart0_try_receive_byte(uint8_t *data)
{
    if (data == NULL)
    {
        return 0U;
    }

    return (uint8_t)rt_ringbuffer_getchar(&s_usart0_rx_ringbuffer, data);
}

uint8_t board_usart1_rs485_try_receive_byte(uint8_t *data)
{
    if (data == NULL)
    {
        return 0U;
    }

    return (uint8_t)rt_ringbuffer_getchar(&s_usart1_rs485_rx_ringbuffer, data);
}

uint32_t board_usart0_rx_overflow_count_get(void)
{
    return s_usart0_rx_overflow_count;
}

uint32_t board_usart0_tx_drop_count_get(void)
{
    return s_usart0_tx_drop_count;
}

uint32_t board_usart0_rx_dma_error_count_get(void)
{
    return s_usart0_rx_dma_error_count;
}

void board_usart1_rs485_send_buffer(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    gpio_bit_set(BOARD_RS485_DIR_PORT, BOARD_RS485_DIR_PIN);

    for (i = 0U; i < length; i++)
    {
        while (RESET == usart_flag_get(USART1, USART_FLAG_TBE)){}

        usart_data_transmit(USART1, data[i]);
    }
    
    while (RESET == usart_flag_get(USART1, USART_FLAG_TC)){}
    gpio_bit_reset(BOARD_RS485_DIR_PORT, BOARD_RS485_DIR_PIN); 
    
}

void board_usart0_irq_handler(void)
{
    rt_uint8_t data;

    if (SET == usart_interrupt_flag_get(USART0, USART_INT_FLAG_IDLE))
    {
        (void)usart_data_receive(USART0);

        board_usart0_rx_dma_commit();
    }

    if (SET == usart_interrupt_flag_get(USART0, USART_INT_FLAG_TBE))
    {
        if (rt_ringbuffer_getchar(&s_usart0_tx_ringbuffer, &data) != 0U)
        {
            usart_data_transmit(USART0, (rt_uint16_t)data);
        }
        else
        {
            usart_interrupt_disable(USART0, USART_INT_TBE);
        }
    }
}

void board_usart0_rx_dma_irq_handler(void)
{
    uint8_t dma_event;
    uint8_t dma_error;

    dma_event = 0U;
    dma_error = 0U;

    if (SET == dma_interrupt_flag_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_SDE))
    {
        dma_interrupt_flag_clear(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_SDE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_TAE))
    {
        dma_interrupt_flag_clear(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_TAE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_FEE))
    {
        dma_interrupt_flag_clear(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_FEE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_HTF);

        dma_event = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF);

        dma_event = 1U;
    }

    if (dma_event != 0U)
    {
        board_usart0_rx_dma_commit();
    }

    if (dma_error != 0U)
    {
        s_usart0_rx_dma_error_count++;
    }
}

void board_usart1_rs485_irq_handler(void)
{
    rt_uint8_t data;

    if (SET == usart_interrupt_flag_get(USART1, USART_INT_FLAG_IDLE))
    {
        (void)usart_data_receive(USART1);

        board_usart1_rs485_rx_dma_commit();
    }

    if (SET == usart_interrupt_flag_get(USART1, USART_INT_FLAG_TBE))
    {
        if (rt_ringbuffer_getchar(&s_usart1_rs485_tx_ringbuffer, &data) != 0U)
        {
            usart_data_transmit(USART1, (rt_uint16_t)data);
        }
        else
        {
            usart_interrupt_disable(USART1, USART_INT_TBE);
        }
    }
}

void board_usart1_rs485_rx_dma_irq_handler(void)
{
    uint8_t dma_event;
    uint8_t dma_error;

    dma_event = 0U;
    dma_error = 0U;

    if (SET == dma_interrupt_flag_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_SDE))
    {
        dma_interrupt_flag_clear(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_SDE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_TAE))
    {
        dma_interrupt_flag_clear(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_TAE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_FEE))
    {
        dma_interrupt_flag_clear(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_FEE);

        dma_error = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_HTF))
    {
        dma_interrupt_flag_clear(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_HTF);

        dma_event = 1U;
    }

    if (SET == dma_interrupt_flag_get(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FLAG_FTF);

        dma_event = 1U;
    }

    if (dma_event != 0U)
    {
        board_usart1_rs485_rx_dma_commit();
    }

    if (dma_error != 0U)
    {
        s_usart1_rs485_rx_dma_error_count++;
    }
}
