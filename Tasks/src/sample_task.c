#include "sample_task.h"
#include "board_adc.h"
#include "board_config.h"

#include "FreeRTOS.h"
#include "task.h"
#include "task_events.h"
#include "alarm_task.h"

#define SAMPLE_TASK_PRIORITY          4U
#define SAMPLE_TASK_STACK_DEPTH       256U

#define SAMPLE_PERIOD_MS              100U
#define SAMPLE_ADC_TIMEOUT_MS         20U

#define SAMPLE_RATIO_MIN              0.0f
#define SAMPLE_RATIO_MAX              100.0f

static StaticTask_t s_sample_task_tcb;
static StackType_t  s_sample_task_stack[SAMPLE_TASK_STACK_DEPTH];

static TaskHandle_t s_sample_task_handle;

static volatile uint32_t s_sample_task_heartbeat;
static volatile uint32_t s_sample_task_stack_high_water_mark;
static volatile uint32_t s_sample_task_adc_error_count;

static sample_snapshot_t s_sample_snapshot;

static uint16_t s_filter_window[SAMPLE_CHANNEL_COUNT][SAMPLE_FILTER_DEPTH];
static uint8_t s_filter_index;
static uint8_t s_filter_count;

static float s_channel_ratio[SAMPLE_CHANNEL_COUNT];

static void sample_filter_reset(void)
{
    uint8_t channel;
    uint8_t slot;
 
    for (channel = 0U; channel < SAMPLE_CHANNEL_COUNT; channel++)
    {
        for (slot = 0U; slot < SAMPLE_FILTER_DEPTH; slot++)
        {
            s_filter_window[channel][slot] = 0U;
        }
    }
 
    s_filter_index = 0U;
    s_filter_count = 0U;
}

static void sample_filter_push(uint16_t ch0, uint16_t ch1)
{
    s_filter_window[0][s_filter_index] = ch0;
    s_filter_window[1][s_filter_index] = ch1;
 
    s_filter_index++;
 
    if (s_filter_index >= SAMPLE_FILTER_DEPTH)
    {
        s_filter_index = 0U;
    }
 
    if (s_filter_count < SAMPLE_FILTER_DEPTH)
    {
        s_filter_count++;
    }
}

static uint16_t sample_filter_average(uint8_t channel)
{
    uint32_t sum;
    uint8_t index;
 
    if ((channel >= SAMPLE_CHANNEL_COUNT) || (s_filter_count == 0U))
    {
        return 0U;
    }
 
    sum = 0U;
 
    for (index = 0U; index < s_filter_count; index++)
    {
        sum += s_filter_window[channel][index];
    }
 
    /* 加半个除数做四舍五入，避免均值系统性偏低 */
    return (uint16_t)((sum + ((uint32_t)s_filter_count / 2U)) / (uint32_t)s_filter_count);
}

static uint16_t sample_code_to_millivolt(uint16_t code)
{
    return (uint16_t)(((uint32_t)code * BOARD_ADC_VREF_MV) / BOARD_ADC_FULL_SCALE_CODE);
}

static void sample_adc_irq_callback(uint32_t events)
{
    BaseType_t higher_priority_task_woken;
    higher_priority_task_woken = pdFALSE;

    if ((events & BOARD_ADC_EVENT_DMA_ERROR) != 0U)
    {
        s_sample_task_adc_error_count++;
    }

    if (s_sample_task_handle == NULL)
    {
        return;
    }

    (void)xTaskNotifyFromISR(
        s_sample_task_handle,
        events,
        eSetBits,
        &higher_priority_task_woken
    );

    portYIELD_FROM_ISR(higher_priority_task_woken);

}

static void sample_task_process_one_sample(void)
{
    board_adc_sample_t adc_sample;
    sample_snapshot_t next_snapshot;
    uint16_t filtered[SAMPLE_CHANNEL_COUNT];
    uint16_t millivolt[SAMPLE_CHANNEL_COUNT];
    float ratio[SAMPLE_CHANNEL_COUNT];
    uint8_t channel;

    if (board_adc_sample_get(&adc_sample) == 0)
    {
        return;
    }

    sample_filter_push(adc_sample.ch0, adc_sample.ch1);

    filtered[0] = sample_filter_average(0U);
    filtered[1] = sample_filter_average(1U);

    for (channel = 0U; channel < SAMPLE_CHANNEL_COUNT; channel++)
    {
        millivolt[channel] = sample_code_to_millivolt(filtered[channel]);
    }

    taskENTER_CRITICAL();

    ratio[0] = s_channel_ratio[0];
    ratio[1] = s_channel_ratio[1];

    taskEXIT_CRITICAL();

    next_snapshot.sequence = s_sample_snapshot.sequence + 1U;
    next_snapshot.timestamp_ms = (uint32_t)(xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
 
    next_snapshot.raw_ch0 = adc_sample.ch0;
    next_snapshot.raw_ch1 = adc_sample.ch1;
 
    next_snapshot.filtered_ch0 = filtered[0];
    next_snapshot.filtered_ch1 = filtered[1];
 
    next_snapshot.voltage_mv_ch0 = millivolt[0];
    next_snapshot.voltage_mv_ch1 = millivolt[1];
 
    next_snapshot.value_ch0 = ((float)millivolt[0] / 1000.0f) * ratio[0];
    next_snapshot.value_ch1 = ((float)millivolt[1] / 1000.0f) * ratio[1];
 
    next_snapshot.filter_count = s_filter_count;
    next_snapshot.filter_ready = (uint8_t)(s_filter_count >= SAMPLE_FILTER_DEPTH);
    next_snapshot.reserved[0] = 0U;
    next_snapshot.reserved[1] = 0U;
 
    taskENTER_CRITICAL();
 
    s_sample_snapshot = next_snapshot;
 
    taskEXIT_CRITICAL();

    (void)alarm_task_sample_submit(&next_snapshot);

}

static void sample_task_wait_for_config(void)
{
    while ((xEventGroupGetBits(task_events_get()) &
            TASK_EVENT_CONFIG_READY) == 0U)
    {
        s_sample_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_sample_task_heartbeat++;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void sample_task(void *argument)
{
    TickType_t next_wake_tick;
    uint32_t notification_value;

    (void)argument;

    sample_task_wait_for_config();

    if (board_adc_start() == 0)
    {
        taskDISABLE_INTERRUPTS();

        for (;;)
        {
        }
    }

    next_wake_tick = xTaskGetTickCount();

    for(;;)
    {
        notification_value = 0U;

        if (board_adc_trigger() != 0)
        {
            if (xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(SAMPLE_ADC_TIMEOUT_MS)) == pdTRUE)
            {
                if (((notification_value & BOARD_ADC_EVENT_DMA_ERROR) == 0U) &&
                    ((notification_value & BOARD_ADC_EVENT_DATA_READY) != 0U))
                {
                    sample_task_process_one_sample();
                }
            }
        }

        s_sample_task_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_sample_task_heartbeat++;

        vTaskDelayUntil(&next_wake_tick, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

int sample_task_create(void)
{
    s_sample_task_handle = xTaskCreateStatic(
        sample_task,
        "Sample",
        SAMPLE_TASK_STACK_DEPTH,
        NULL,
        SAMPLE_TASK_PRIORITY,
        s_sample_task_stack,
        &s_sample_task_tcb
    );

    if (s_sample_task_handle == NULL)
    {
        return 0;
    }

    sample_filter_reset();
    s_channel_ratio[0] = 1.0f;
    s_channel_ratio[1] = 1.0f;
    board_adc_irq_callback_register(sample_adc_irq_callback);

    return 1;
}

uint32_t sample_task_get_heartbeat(void)
{
    return s_sample_task_heartbeat;
}

uint32_t sample_task_get_stack_high_water_mark(void)
{
    return s_sample_task_stack_high_water_mark;
}

uint32_t sample_task_get_adc_error_count(void)
{
    return s_sample_task_adc_error_count;
}

int sample_task_snapshot_get(sample_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    *snapshot = s_sample_snapshot;

    taskEXIT_CRITICAL();

    return 1;
}

int sample_task_ratio_set(uint8_t channel, float ratio)
{
    if (channel >= SAMPLE_CHANNEL_COUNT)
    {
        return 0;
    }
 
    /* NaN 与自身不相等，用这个特性一并挡掉 */
    if (!(ratio >= SAMPLE_RATIO_MIN) || !(ratio <= SAMPLE_RATIO_MAX))
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_channel_ratio[channel] = ratio;
 
    taskEXIT_CRITICAL();
 
    return 1;
}

float sample_task_ratio_get(uint8_t channel)
{
    float ratio;
 
    if (channel >= SAMPLE_CHANNEL_COUNT)
    {
        return 0.0f;
    }
 
    taskENTER_CRITICAL();
 
    ratio = s_channel_ratio[channel];
 
    taskEXIT_CRITICAL();
 
    return ratio;
}
