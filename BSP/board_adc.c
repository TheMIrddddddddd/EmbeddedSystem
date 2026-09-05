#include "gd32f4xx.h"
#include "gd32f4xx_adc.h"
#include "gd32f4xx_dma.h"
#include "gd32f4xx_rcu.h"

#include "board_adc.h"
#include "board_config.h"
#include "board_dma_map.h"

#if defined(__CC_ARM)

__align(4)
static volatile uint16_t s_adc_dma_buffer[BOARD_ADC1_DMA_TRANSFER_COUNT];

#elif defined(__GNUC__)

static volatile uint16_t s_adc_dma_buffer[BOARD_ADC1_DMA_TRANSFER_COUNT]
    __attribute__((aligned(4)));

#else

static volatile uint16_t s_adc_dma_buffer[BOARD_ADC1_DMA_TRANSFER_COUNT];

#endif

static board_adc_irq_callback_t s_board_adc_irq_callback;

static volatile uint8_t s_board_adc_initialized;
static volatile uint8_t s_board_adc_started;

static volatile uint32_t s_board_adc_dma_event_count;
static volatile uint32_t s_board_adc_dma_error_count;

static void board_adc_dma_init(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RCU_DMA1);

    dma_deinit(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL);

    dma_single_data_para_struct_init(&dma_init_struct);

    dma_init_struct.periph_addr = (uint32_t)&ADC_RDATA(ADC1);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr = (uint32_t)s_adc_dma_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;

    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_16BIT;
    dma_init_struct.circular_mode = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number = BOARD_ADC1_DMA_TRANSFER_COUNT;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, &dma_init_struct);

    dma_channel_subperipheral_select(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, BOARD_ADC1_DMA_SUBPERI);

    dma_interrupt_enable(
        BOARD_ADC1_DMA_PERIPH,
        BOARD_ADC1_DMA_CHANNEL,
        DMA_INT_FTF | DMA_INT_SDE | DMA_INT_TAE);
    dma_interrupt_enable(
        BOARD_ADC1_DMA_PERIPH,
        BOARD_ADC1_DMA_CHANNEL,
        DMA_INT_FEE);

    nvic_irq_enable(BOARD_ADC1_DMA_IRQn, 7U, 0U);
}

int board_adc_init(void)
{
    uint32_t adc_pins;
    uint32_t index;

    adc_pins =BOARD_ADC1_CH0_PIN | BOARD_ADC1_CH1_PIN;

    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_ADC1);

    /*
     * PC0、PC1 配置为模拟输入。
     */
    gpio_mode_set(GPIOC, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, adc_pins);

    /*
     * 清空 DMA 缓冲区。
     */
    for (index = 0U; index < BOARD_ADC1_DMA_TRANSFER_COUNT; index++)
    {
        s_adc_dma_buffer[index] = 0U;
    }

    /*
     * ADC1 全局复位和时钟配置。
     *
     * 当前 PCLK2 = 120 MHz，
     * ADC 时钟选择 PCLK2 / 4 = 30 MHz。
     */
    adc_deinit();
    adc_clock_config(ADC_ADCCK_PCLK2_DIV4);

    /*
     * 两个普通规则通道扫描：
     *   rank 0 -> PC0 / ADC channel 10
     *   rank 1 -> PC1 / ADC channel 11
     */
    adc_special_function_config(ADC1, ADC_SCAN_MODE, ENABLE);

    /*
     * 第一步采用软件触发，不打开 ADC 连续转换。
     * SampleTask 每 100 ms 触发一次。
     */
    adc_special_function_config(ADC1, ADC_CONTINUOUS_MODE, DISABLE);

    adc_data_alignment_config(ADC1, ADC_DATAALIGN_RIGHT);

    adc_resolution_config(ADC1, ADC_RESOLUTION_12B);

    adc_channel_length_config(ADC1, ADC_ROUTINE_CHANNEL, 2U);

    adc_routine_channel_config(ADC1, 0U, BOARD_ADC1_CH0_CHANNEL, ADC_SAMPLETIME_84);

    adc_routine_channel_config(ADC1, 1U, BOARD_ADC1_CH1_CHANNEL, ADC_SAMPLETIME_84);

    adc_external_trigger_config(ADC1, ADC_ROUTINE_CHANNEL, EXTERNAL_TRIGGER_DISABLE);

    adc_end_of_conversion_config(ADC1, ADC_EOC_SET_SEQUENCE);

    /*
     * ADC 每完成一次规则序列，就通过 DMA 搬运两个 16 bit 数据。
     */
    adc_dma_mode_enable(ADC1);
    adc_dma_request_after_last_enable(ADC1);

    board_adc_dma_init();

    adc_enable(ADC1);
    adc_calibration_enable(ADC1);

    s_board_adc_dma_event_count = 0U;
    s_board_adc_dma_error_count = 0U;
    s_board_adc_started = 0U;
    s_board_adc_initialized = 1U;

    return 1;
}

int board_adc_start(void)
{
    if (s_board_adc_initialized == 0U)
    {
        return 0U;
    }

    dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FTF);
    dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_SDE);
    dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_TAE);
    dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FEE);

    dma_transfer_number_config(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, BOARD_ADC1_DMA_TRANSFER_COUNT);

    dma_channel_enable(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL);

    s_board_adc_started = 1U;

    return 1;
}

int board_adc_trigger(void)
{
    if (s_board_adc_started == 0U)
    {
        return 0U;
    }

    adc_software_trigger_enable(ADC1, ADC_ROUTINE_CHANNEL);

    return 1;
}

int board_adc_sample_get(board_adc_sample_t *sample)
{
    if ((sample == NULL) || (s_board_adc_started == 0U))
    {
        return 0;
    }

    sample->ch0 = s_adc_dma_buffer[0];
    sample->ch1 = s_adc_dma_buffer[1];

    return 1;
}
void board_adc_stop(void)
{
    dma_channel_disable(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL);

    s_board_adc_started = 0U;
}

void board_adc_irq_handler(void)
{
    uint32_t events;

    events = 0U;

    if (SET == dma_interrupt_flag_get(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FTF);

        events |= BOARD_ADC_EVENT_DATA_READY;
        s_board_adc_dma_event_count++;
    }

    if (SET == dma_interrupt_flag_get(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_SDE))
    {
        dma_interrupt_flag_clear( BOARD_ADC1_DMA_PERIPH,BOARD_ADC1_DMA_CHANNEL,DMA_INT_FLAG_SDE);

        events |= BOARD_ADC_EVENT_DMA_ERROR;
    }

    if (SET == dma_interrupt_flag_get(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_TAE))
    {
        dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_TAE);

        events |= BOARD_ADC_EVENT_DMA_ERROR;
    }

    if (SET == dma_interrupt_flag_get(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FEE))
    {
        dma_interrupt_flag_clear(BOARD_ADC1_DMA_PERIPH, BOARD_ADC1_DMA_CHANNEL, DMA_INT_FLAG_FEE);

        events |= BOARD_ADC_EVENT_DMA_ERROR;
    }

    if ((events & BOARD_ADC_EVENT_DMA_ERROR) != 0U)
    {
        s_board_adc_dma_error_count++;
    }

    if ((events != 0U) && (s_board_adc_irq_callback != NULL))
    {
        s_board_adc_irq_callback(events);
    }
}

void board_adc_irq_callback_register(board_adc_irq_callback_t callback)
{
    s_board_adc_irq_callback = callback;
}

uint32_t board_adc_dma_event_count_get(void)
{
    return s_board_adc_dma_event_count;
}

uint32_t board_adc_dma_error_count_get(void)
{
    return s_board_adc_dma_error_count;
}
