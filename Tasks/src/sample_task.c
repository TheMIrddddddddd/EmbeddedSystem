#include "sample_task.h"
#include "board_adc.h"

#include "FreeRTOS.h"
#include "task.h"

#define SAMPLE_TASK_PRIORITY          4U
#define SAMPLE_TASK_STACK_DEPTH       256U

#define SAMPLE_PERIOD_MS              100U
#define SAMPLE_ADC_TIMEOUT_MS         20U

static StaticTask_t s_sample_task_tcb;
static StackType_t  s_sample_task_stack[SAMPLE_TASK_STACK_DEPTH];

static TaskHandle_t s_sample_task_handle;

static volatile uint32_t s_sample_task_heartbeat;
static volatile uint32_t s_sample_task_stack_high_water_mark;
static volatile uint32_t s_sample_task_adc_error_count;

static sample_snapshot_t s_sample_snapshot;

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

    if (board_adc_sample_get(&adc_sample) == 0)
    {
        return;
    }

    next_snapshot.sequence = s_sample_snapshot.sequence + 1U;
    next_snapshot.timestamp_ms = (uint32_t)( xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
    next_snapshot.raw_ch0 = adc_sample.ch0;
    next_snapshot.raw_ch1 = adc_sample.ch1;

    taskENTER_CRITICAL();

    s_sample_snapshot = next_snapshot;

    taskEXIT_CRITICAL();

}

static void sample_task(void *argument)
{
    TickType_t next_wake_tick;
    uint32_t notification_value;

    (void)argument;

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
