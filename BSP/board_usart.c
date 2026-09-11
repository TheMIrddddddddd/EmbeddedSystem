#include <stddef.h>
#include <stdio.h>

#include "board_usart.h"
#include "board_config.h"
#include "board_dma_map.h"
#include "board_timebase.h"
#include "board_rtu_timing.h"
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

#define BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE     512U

#define BOARD_USART1_RS485_RX_FRAME_EVENT_QUEUE_SIZE  8U

/*
 * Modbus RTU：
 * baud > 19200 时，使用固定 t3.5 = 1750 us。
 * baud <= 19200 时，按照 11 bit 字符计算。
 */
#define BOARD_USART1_RS485_RX_RTU_FIXED_T3_5_US      1750U
#define BOARD_USART1_RS485_RX_RTU_CALC_BAUD_MAX      19200U

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

typedef struct
{
    uint16_t length;
    uint8_t  overflow;
    uint8_t  gap_error;
} board_usart1_rs485_rtu_frame_event_t;

static volatile uint32_t s_usart1_rs485_rx_dma_activity_sequence;       /* DMA 活动序号，每次发现新的 DMA 数据时递增 */
static volatile uint8_t s_usart1_rs485_rtu_enabled;                     /* RTU 接收开关 */
static board_rtu_timing_t s_usart1_rs485_rtu_timing;
static volatile uint32_t s_usart1_rs485_rtu_t1_5_us;
static volatile uint32_t s_usart1_rs485_rtu_char_time_us;
static volatile uint32_t s_usart1_rs485_rtu_gap_error_count;
static volatile uint32_t s_usart1_rs485_rtu_rx_byte_count;
static uint32_t s_usart1_rs485_baudrate = 115200U;
static volatile uint8_t s_usart1_rs485_rtu_frame_gap_error;
static volatile uint32_t s_usart1_rs485_rtu_t3_5_us;                    /* 线上的静默阈值 */
static volatile uint32_t s_usart1_rs485_rtu_alarm_delay_us;
static volatile uint16_t s_usart1_rs485_rtu_frame_byte_count;           /* 当前尚未发布帧的统计 */
static volatile uint8_t s_usart1_rs485_rtu_frame_overflow;
static board_usart1_rs485_rtu_frame_event_t s_usart1_rs485_rtu_event_queue[BOARD_USART1_RS485_RX_FRAME_EVENT_QUEUE_SIZE];    /* 帧事件环形队列 */
static volatile uint8_t s_usart1_rs485_rtu_event_read;
static volatile uint8_t s_usart1_rs485_rtu_event_write;
/* 调试计数 */
static volatile uint32_t s_usart1_rs485_rtu_frame_end_count;
static volatile uint32_t s_usart1_rs485_rtu_frame_overflow_count;
static volatile uint32_t s_usart1_rs485_rtu_alarm_error_count;

static void board_usart0_rx_dma_init(void);
static void board_usart0_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length);
static void board_usart0_rx_dma_commit(void);

static void board_usart1_rs485_rx_dma_init(void);
static void board_usart1_rs485_rx_dma_commit_block(const rt_uint8_t *data, uint16_t length);
static void board_usart1_rs485_rx_dma_commit(void);

static uint16_t board_usart1_rs485_rx_dma_current_pos_get(void);
static uint32_t board_usart1_rs485_rtu_char_time_us_calculate(uint32_t baudrate);
static uint32_t board_usart1_rs485_rtu_t3_5_us_calculate(uint32_t baudrate);
static void board_usart1_rs485_rtu_timing_update(uint32_t baudrate);
static uint8_t board_usart1_rs485_rtu_event_index_next(uint8_t index);
static void board_usart1_rs485_rtu_flush(void);
static void board_usart1_rs485_rtu_alarm_arm(void);
static void board_usart1_rs485_rtu_frame_end_publish(void);
static void board_usart1_rs485_rtu_alarm_callback(void);

#pragma import(__use_no_semihosting)

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
    dma_channel_subperipheral_select(BOARD_USART0_RX_DMA_PERIPH, BOARD_USART0_RX_DMA_CHANNEL, BOARD_USART0_RX_DMA_SUBPERI);
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

    nvic_irq_enable(USART0_IRQn, 6U, 0U);
    nvic_irq_enable(BOARD_USART0_RX_DMA_IRQn, 7U, 0U);
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
    dma_channel_subperipheral_select(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, BOARD_USART1_RS485_RX_DMA_SUBPERI);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_HTF | DMA_INT_FTF);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_SDE | DMA_INT_TAE);
    dma_interrupt_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL, DMA_INT_FEE);
    dma_channel_enable(BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);

    nvic_irq_enable(USART1_IRQn, 6U, 0U);
    nvic_irq_enable(BOARD_USART1_RS485_RX_DMA_IRQn, 7U, 0U);
    usart_interrupt_disable(USART1, USART_INT_RBNE);
    usart_interrupt_enable(USART1, USART_INT_IDLE);
}

void board_usart1_rs485_init(void)
{
    rt_ringbuffer_init(&s_usart1_rs485_rx_ringbuffer, s_usart1_rs485_rx_ringbuffer_pool, BOARD_USART1_RS485_RX_RINGBUFFER_SIZE);
    rt_ringbuffer_init(&s_usart1_rs485_tx_ringbuffer, s_usart1_rs485_tx_ringbuffer_pool, BOARD_USART1_RS485_TX_RINGBUFFER_SIZE);

    s_usart1_rs485_rx_dma_last_pos = 0U;
    s_usart1_rs485_rx_dma_activity_sequence = 0U;

    s_usart1_rs485_rtu_enabled = 0U;
    s_usart1_rs485_rtu_gap_error_count = 0U;
    s_usart1_rs485_rtu_rx_byte_count = 0U;
    s_usart1_rs485_rtu_frame_gap_error = 0U;

    s_usart1_rs485_rtu_frame_byte_count = 0U;
    s_usart1_rs485_rtu_frame_overflow = 0U;

    s_usart1_rs485_rtu_event_read = 0U;
    s_usart1_rs485_rtu_event_write = 0U;

    s_usart1_rs485_rtu_frame_end_count = 0U;
    s_usart1_rs485_rtu_frame_overflow_count = 0U;
    s_usart1_rs485_rtu_alarm_error_count = 0U;

    board_usart1_rs485_rtu_timing_update(115200U);

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
    /*
     * RTU 模式下，记录当前静默窗口内实际进入
     * RingBuffer 的字节数量。
     */
    if (s_usart1_rs485_rtu_enabled != 0U)
    {
        if (queued < (rt_size_t)length)
        {
            s_usart1_rs485_rtu_frame_overflow = 1U;
            s_usart1_rs485_rtu_frame_overflow_count++;
        }

        if (((uint32_t)s_usart1_rs485_rtu_frame_byte_count + (uint32_t)queued) > 0xFFFFUL)
        {
            s_usart1_rs485_rtu_frame_byte_count = 0xFFFFU;
            s_usart1_rs485_rtu_frame_overflow = 1U;
        }
        else
        {
            s_usart1_rs485_rtu_frame_byte_count = (uint16_t)((uint32_t)s_usart1_rs485_rtu_frame_byte_count +(uint32_t)queued);
        }
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

    current_pos = board_usart1_rs485_rx_dma_current_pos_get();

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

    /*
     * 只要 DMA 写入位置发生变化，就认为接收活动发生。
     * 定时器回调通过该序号判断 t3.5 期间是否有新数据。
     */
    s_usart1_rs485_rx_dma_activity_sequence++;
}

static uint16_t board_usart1_rs485_rx_dma_current_pos_get(void)
{
    uint32_t current_pos;

    current_pos =BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE - dma_transfer_number_get( BOARD_USART1_RS485_RX_DMA_PERIPH, BOARD_USART1_RS485_RX_DMA_CHANNEL);

    if (current_pos >= BOARD_USART1_RS485_RX_DMA_BUFFER_SIZE)
    {
        current_pos = 0U;
    }

    return (uint16_t)current_pos;
}

static uint32_t board_usart1_rs485_rtu_char_time_us_calculate(uint32_t baudrate)
{
    if (baudrate == 0U)
    {
        return 1U;
    }

    /*
     * Modbus 8E1 按 11 bit 一个字符计算：
     *
     * t_char = 11 / baudrate
     *
     * 使用向上取整，避免静默窗口过短。
     */
    return (11000000UL + baudrate - 1UL) / baudrate;
}

static uint32_t board_usart1_rs485_rtu_t3_5_us_calculate(uint32_t baudrate)
{
    if ((baudrate == 0U) || (baudrate > BOARD_USART1_RS485_RX_RTU_CALC_BAUD_MAX))
    {
        return BOARD_USART1_RS485_RX_RTU_FIXED_T3_5_US;
    }

    /*
     * t3.5 = 3.5 * 11 / baudrate
     *       = 38.5 / baudrate
     */
    return (38500000UL + baudrate - 1UL) / baudrate;
}

static void board_usart1_rs485_rtu_timing_update(uint32_t baudrate)
{
    s_usart1_rs485_rtu_char_time_us =
        board_usart1_rs485_rtu_char_time_us_calculate(baudrate);
    s_usart1_rs485_rtu_t1_5_us = (baudrate > 19200U) ?
        750U : (16500000UL + baudrate - 1U) / baudrate;
    s_usart1_rs485_rtu_t3_5_us =
        board_usart1_rs485_rtu_t3_5_us_calculate(baudrate);
    /* From byte completion; extra character prevents in-flight-byte split. */
    s_usart1_rs485_rtu_alarm_delay_us =
        s_usart1_rs485_rtu_t3_5_us + s_usart1_rs485_rtu_char_time_us;
    board_rtu_timing_init(&s_usart1_rs485_rtu_timing,
        s_usart1_rs485_rtu_t1_5_us, s_usart1_rs485_rtu_t3_5_us,
        s_usart1_rs485_rtu_char_time_us, board_timebase_now_us());
}

static uint8_t board_usart1_rs485_rtu_event_index_next(uint8_t index)
{
    index++;

    if (index >= BOARD_USART1_RS485_RX_FRAME_EVENT_QUEUE_SIZE)
    {
        index = 0U;
    }

    return index;
}

/*
 * 调用该函数前，调用方应已关闭中断。
 */
static void board_usart1_rs485_rtu_flush(void)
{
    rt_ringbuffer_reset(&s_usart1_rs485_rx_ringbuffer);

    s_usart1_rs485_rx_dma_last_pos = board_usart1_rs485_rx_dma_current_pos_get();

    s_usart1_rs485_rtu_frame_byte_count = 0U;
    s_usart1_rs485_rtu_frame_overflow = 0U;

    /*
     * 丢弃旧的帧事件。
     */
    s_usart1_rs485_rtu_event_read = s_usart1_rs485_rtu_event_write;
    s_usart1_rs485_rtu_frame_gap_error = 0U;
}

static void board_usart1_rs485_rtu_frame_end_publish(void)
{
    uint8_t write_index;
    uint8_t next_index;

    if (s_usart1_rs485_rtu_frame_byte_count == 0U)
    {
        /* First-byte UART errors/startup discard may have no buffered data.
         * Their error flags belong to this sequence, never the next frame. */
        s_usart1_rs485_rtu_frame_overflow = 0U;
        s_usart1_rs485_rtu_frame_gap_error = 0U;
        return;
    }

    write_index = s_usart1_rs485_rtu_event_write;
    next_index = board_usart1_rs485_rtu_event_index_next(write_index);

    if (next_index == s_usart1_rs485_rtu_event_read)
    {
        /*
         * 事件队列已满。
         * 清空当前接收状态，等待下一帧重新同步。
         */
        s_usart1_rs485_rtu_frame_overflow_count++;
        board_usart1_rs485_rtu_flush();
        return;
    }

    s_usart1_rs485_rtu_event_queue[write_index].length = s_usart1_rs485_rtu_frame_byte_count;
    s_usart1_rs485_rtu_event_queue[write_index].overflow = s_usart1_rs485_rtu_frame_overflow;
    s_usart1_rs485_rtu_event_queue[write_index].gap_error = s_usart1_rs485_rtu_frame_gap_error;

    /*
     * 最后再更新 write，保证消费者看到完整事件。
     */
    s_usart1_rs485_rtu_event_write = next_index;

    s_usart1_rs485_rtu_frame_byte_count = 0U;
    s_usart1_rs485_rtu_frame_overflow = 0U;
    s_usart1_rs485_rtu_frame_gap_error = 0U;
    s_usart1_rs485_rtu_frame_end_count++;
}

static void board_usart1_rs485_rtu_alarm_arm(void)
{
    if (s_usart1_rs485_rtu_enabled == 0U)
    {
        return;
    }
    if (board_timebase_alarm_start(s_usart1_rs485_rtu_alarm_delay_us,
            board_usart1_rs485_rtu_alarm_callback) == 0)
    {
        s_usart1_rs485_rtu_alarm_error_count++;
        s_usart1_rs485_rtu_timing.dropping = 1U;
        s_usart1_rs485_rtu_frame_overflow = 1U;
    }
}

static void board_usart1_rs485_rtu_alarm_callback(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_usart1_rs485_rtu_enabled != 0U)
    {
        /* Drain a completed byte before deciding a boundary, even if the
         * USART IRQ is pending behind TIMER1 at the same priority. */
        board_usart1_rs485_irq_handler();
        if (board_rtu_timing_poll(&s_usart1_rs485_rtu_timing,
                                 board_timebase_now_us()) != 0U)
        {
            board_usart1_rs485_rtu_frame_end_publish();
        }
    }
    __set_PRIMASK(primask);
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
    uint8_t received = 0U;
    uint32_t primask;
    if (data == NULL) return 0U;
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_usart1_rs485_rtu_enabled == 0U)
        received = (uint8_t)rt_ringbuffer_getchar(&s_usart1_rs485_rx_ringbuffer, data);
    __set_PRIMASK(primask);
    return received;
}

void board_usart1_rs485_rtu_receive_enable(uint8_t enabled)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* Owner calls only when TX is complete. DMA stays allocated to CH5;
     * its USART request is disabled in RTU so CPU and DMA never read DATA
     * concurrently. Pending DMA IRQs cannot commit RTU bytes. */
    usart_disable(USART1);
    usart_interrupt_disable(USART1, USART_INT_IDLE);
    usart_interrupt_disable(USART1, USART_INT_RBNE);
    usart_interrupt_disable(USART1, USART_INT_PERR);
    usart_interrupt_disable(USART1, USART_INT_ERR);
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_DISABLE);
    board_timebase_alarm_cancel();
    (void)USART_STAT0(USART1);
    (void)usart_data_receive(USART1);
    board_usart1_rs485_rtu_flush();
    s_usart1_rs485_rtu_enabled = (enabled != 0U) ? 1U : 0U;

    if (enabled != 0U)
    {
        /* GD32 WL includes parity: 9-bit word + even parity = 8E1. */
        usart_word_length_set(USART1, USART_WL_9BIT);
        usart_parity_config(USART1, USART_PM_EVEN);
        board_usart1_rs485_rtu_timing_update(s_usart1_rs485_baudrate);
        usart_interrupt_enable(USART1, USART_INT_RBNE);
        usart_interrupt_enable(USART1, USART_INT_PERR);
        usart_interrupt_enable(USART1, USART_INT_ERR);
        board_usart1_rs485_rtu_alarm_arm();
    }
    else
    {
        usart_word_length_set(USART1, USART_WL_8BIT);
        usart_parity_config(USART1, USART_PM_NONE);
        usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
        usart_interrupt_enable(USART1, USART_INT_IDLE);
    }
    usart_enable(USART1);
    __set_PRIMASK(primask);
}

uint8_t board_usart1_rs485_rtu_frame_receive(uint8_t *buffer,
    uint16_t buffer_size, uint16_t *length)
{
    board_usart1_rs485_rtu_frame_event_t event;
    uint8_t status = BOARD_USART1_RS485_RX_FRAME_NONE;
    uint32_t primask;
    if (length == NULL) return BOARD_USART1_RS485_RX_FRAME_BUFFER_SMALL;
    *length = 0U;
    if ((buffer == NULL) || (buffer_size == 0U))
        return BOARD_USART1_RS485_RX_FRAME_BUFFER_SMALL;

    /* At most 256 bytes copied. Protect against ISR queue-full flush and
     * mode changes, including the event snapshot and read-index update. */
    primask = __get_PRIMASK();
    __disable_irq();
    if ((s_usart1_rs485_rtu_enabled != 0U) &&
        (s_usart1_rs485_rtu_event_read != s_usart1_rs485_rtu_event_write))
    {
        event = s_usart1_rs485_rtu_event_queue[s_usart1_rs485_rtu_event_read];
        if (event.length > buffer_size)
        {
            status = BOARD_USART1_RS485_RX_FRAME_BUFFER_SMALL;
        }
        else if (rt_ringbuffer_data_len(&s_usart1_rs485_rx_ringbuffer) < event.length)
        {
            board_usart1_rs485_rtu_flush();
            s_usart1_rs485_rtu_timing.dropping = 1U;
            status = BOARD_USART1_RS485_RX_FRAME_OVERFLOW;
        }
        else
        {
            *length = (uint16_t)rt_ringbuffer_get(
                &s_usart1_rs485_rx_ringbuffer, buffer, event.length);
            s_usart1_rs485_rtu_event_read = board_usart1_rs485_rtu_event_index_next(
                s_usart1_rs485_rtu_event_read);
            status = event.gap_error ? BOARD_USART1_RS485_RX_FRAME_GAP_ERROR :
                (event.overflow ? BOARD_USART1_RS485_RX_FRAME_OVERFLOW :
                                  BOARD_USART1_RS485_RX_FRAME_READY);
            if (status != BOARD_USART1_RS485_RX_FRAME_READY) *length = 0U;
        }
    }
    __set_PRIMASK(primask);
    return status;
}

uint32_t board_usart1_rs485_rtu_frame_end_count_get(void)
{
    return s_usart1_rs485_rtu_frame_end_count;
}

uint32_t board_usart1_rs485_rtu_frame_overflow_count_get(void)
{
    return s_usart1_rs485_rtu_frame_overflow_count;
}

uint32_t board_usart1_rs485_rtu_alarm_error_count_get(void)
{
    return s_usart1_rs485_rtu_alarm_error_count;
}

uint32_t board_usart1_rs485_rtu_t3_5_us_get(void)
{
    return s_usart1_rs485_rtu_t3_5_us;
}

uint32_t board_usart0_rx_overflow_count_get(void)
{
    return s_usart0_rx_overflow_count;
}

uint32_t board_usart0_tx_drop_count_get(void)
{
    return s_usart0_tx_drop_count;
}

uint16_t board_usart0_tx_free_get(void)
{
    return (uint16_t)rt_ringbuffer_space_len(&s_usart0_tx_ringbuffer);
}

uint32_t board_usart0_rx_dma_error_count_get(void)
{
    return s_usart0_rx_dma_error_count;
}

void board_usart1_rs485_baudrate_set(uint32_t baudrate)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    if (baudrate == 0U)
    {
        __set_PRIMASK(primask);
        return;
    }
    s_usart1_rs485_baudrate = baudrate;
    usart_baudrate_set(USART1, baudrate);
    board_usart1_rs485_rtu_timing_update(baudrate);
    if (s_usart1_rs485_rtu_enabled != 0U)
    {
        board_usart1_rs485_rtu_flush();
        board_usart1_rs485_rtu_alarm_arm();
    }

    __set_PRIMASK(primask);
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
    uint32_t status;
    uint32_t now;
    uint32_t gap_errors;

    if (s_usart1_rs485_rtu_enabled != 0U)
    {
        status = USART_STAT0(USART1);
        if ((status & (USART_STAT0_RBNE | USART_STAT0_ORERR | USART_STAT0_PERR | USART_STAT0_FERR | USART_STAT0_NERR)) != 0U)
        {
            now = board_timebase_now_us();
            data = (uint8_t)usart_data_receive(USART1);
            gap_errors = s_usart1_rs485_rtu_timing.gap_errors;
            if (board_rtu_timing_byte(&s_usart1_rs485_rtu_timing, now) != 0U)
                board_usart1_rs485_rtu_frame_end_publish();
            if (s_usart1_rs485_rtu_timing.gap_errors != gap_errors)
            {
                s_usart1_rs485_rtu_gap_error_count++;
                s_usart1_rs485_rtu_frame_gap_error = 1U;
            }
            if ((status & (USART_STAT0_ORERR | USART_STAT0_PERR |
                           USART_STAT0_FERR | USART_STAT0_NERR)) != 0U)
            {
                s_usart1_rs485_rtu_timing.dropping = 1U;
                s_usart1_rs485_rtu_frame_overflow = 1U;
                s_usart1_rs485_rtu_frame_overflow_count++;
            }
            if ((status & USART_STAT0_RBNE) != 0U)
            {
                s_usart1_rs485_rtu_rx_byte_count++;
                if (s_usart1_rs485_rtu_timing.dropping == 0U)
                {
                    if (s_usart1_rs485_rtu_frame_byte_count >= 256U)
                    {
                        s_usart1_rs485_rtu_frame_overflow = 1U;
                        s_usart1_rs485_rtu_frame_overflow_count++;
                        s_usart1_rs485_rtu_timing.dropping = 1U;
                    }
                    else
                    {
                        board_usart1_rs485_rx_dma_commit_block(&data, 1U);
                        if (s_usart1_rs485_rtu_frame_overflow != 0U)
                            s_usart1_rs485_rtu_timing.dropping = 1U;
                    }
                }
            }
            board_usart1_rs485_rtu_alarm_arm();
        }
    }
    else if (SET == usart_interrupt_flag_get(USART1, USART_INT_FLAG_IDLE))
    {
        (void)usart_data_receive(USART1);
        board_usart1_rs485_rx_dma_commit();
    }

    if (SET == usart_interrupt_flag_get(USART1, USART_INT_FLAG_TBE))
    {
        if (rt_ringbuffer_getchar(&s_usart1_rs485_tx_ringbuffer, &data) != 0U)
            usart_data_transmit(USART1, (rt_uint16_t)data);
        else
            usart_interrupt_disable(USART1, USART_INT_TBE);
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

    if ((dma_event != 0U) && (s_usart1_rs485_rtu_enabled == 0U))
    {
        board_usart1_rs485_rx_dma_commit();
    }

    if (dma_error != 0U)
    {
        s_usart1_rs485_rx_dma_error_count++;
    }
}


uint32_t board_usart1_rs485_rtu_t1_5_us_get(void)
{
    return s_usart1_rs485_rtu_t1_5_us;
}

uint32_t board_usart1_rs485_rtu_gap_error_count_get(void)
{
    return s_usart1_rs485_rtu_gap_error_count;
}
