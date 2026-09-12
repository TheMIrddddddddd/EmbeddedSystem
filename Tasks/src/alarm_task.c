#include "alarm_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_events.h"
#include "alarm_logic.h"
#include "sample_task.h"
#include "app_config.h"
#include "storage_task.h"
#include "board_gpio.h"
#include "queue.h"

#define ALARM_TASK_PRIORITY          3U
#define ALARM_TASK_STACK_DEPTH       256U

static StaticTask_t s_alarm_task_tcb;
static StackType_t  s_alarm_task_stack[ALARM_TASK_STACK_DEPTH];

static volatile uint32_t s_alarm_task_heartbeat;
static volatile uint32_t s_alarm_task_stack_high_water_mark;
static StaticQueue_t s_alarm_sample_queue;
__align(8)
static uint8_t s_alarm_sample_queue_storage[8U * sizeof(sample_snapshot_t)];
static QueueHandle_t s_alarm_sample_queue_handle;
static alarm_logic_channel_t s_alarm_channels[SAMPLE_CHANNEL_COUNT];
static volatile uint8_t s_alarm_active_mask;
static volatile uint32_t s_alarm_trigger_count;
static volatile uint32_t s_alarm_recovery_count;

/* 根据一个按值接收的采样快照更新两个通道状态机。 */
static void alarm_task_process_one_snapshot(const sample_snapshot_t *snapshot)
{
    app_config_t config;
    alarm_logic_event_t event;
    float value;
    uint8_t channel;

    if ((snapshot == 0) || (snapshot->filter_ready == 0U) ||
        (app_config_get(&config) == 0)) return;

    for (channel = 0U; channel < SAMPLE_CHANNEL_COUNT; channel++)
    {
        value = (channel == 0U) ? snapshot->value_ch0 : snapshot->value_ch1;
        event = ALARM_LOGIC_EVENT_NONE;
        if (alarm_logic_update(&s_alarm_channels[channel], value,
                               config.limit[channel], &event) ==
            ALARM_LOGIC_STATUS_ERROR) continue;
        if (event == ALARM_LOGIC_EVENT_TRIGGERED)
        {
            s_alarm_active_mask = (uint8_t)(s_alarm_active_mask | (uint8_t)(1U << channel));
            s_alarm_trigger_count++;
            (void)storage_task_alarm_record_submit(channel, config.limit[channel], value);
            (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_ALARM_ACTIVE,
                                                  channel, value,
                                                  config.limit[channel], 0U);
        }
        else if (event == ALARM_LOGIC_EVENT_RECOVERED)
        {
            s_alarm_active_mask = (uint8_t)(s_alarm_active_mask & (uint8_t)~(1U << channel));
            s_alarm_recovery_count++;
            (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_ALARM_RECOVERED,
                                                  channel, value,
                                                  config.limit[channel], 0U);
        }
    }
    board_led_set(3U, (s_alarm_active_mask != 0U) ? 1U : 0U);
}

/* 清空采样快照队列；每条快照只被状态机消费一次。 */
static void alarm_task_process_snapshots(void)
{
    sample_snapshot_t snapshot;
    if (s_alarm_sample_queue_handle == 0) return;
    while (xQueueReceive(s_alarm_sample_queue_handle, &snapshot, 0U) == pdPASS)
        alarm_task_process_one_snapshot(&snapshot);
}

static void alarm_task(void *argument)
{
    (void)argument;

    (void)alarm_logic_init(&s_alarm_channels[0]);
    (void)alarm_logic_init(&s_alarm_channels[1]);

    while ((xEventGroupGetBits(task_events_get()) &
            TASK_EVENT_CONFIG_READY) == 0U)
    {
        s_alarm_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_alarm_task_heartbeat++;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    for(;;)
    {
        alarm_task_process_snapshots();
        s_alarm_task_stack_high_water_mark =
            (uint32_t)uxTaskGetStackHighWaterMark2(NULL);
        s_alarm_task_heartbeat++;

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int alarm_task_create(void)
{
    TaskHandle_t alarm_task_handle;

    s_alarm_sample_queue_handle = xQueueCreateStatic(
        8U, sizeof(sample_snapshot_t), s_alarm_sample_queue_storage,
        &s_alarm_sample_queue);
    if (s_alarm_sample_queue_handle == NULL)
    {
        return 0;
    }

    alarm_task_handle = xTaskCreateStatic(
        alarm_task,
        "Alarm",
        ALARM_TASK_STACK_DEPTH,
        NULL,
        ALARM_TASK_PRIORITY,
        s_alarm_task_stack,
        &s_alarm_task_tcb
    );

    if (alarm_task_handle == NULL)
    {
        return 0;
    }

    s_alarm_active_mask = 0U;
    s_alarm_trigger_count = 0U;
    s_alarm_recovery_count = 0U;
    (void)alarm_logic_init(&s_alarm_channels[0]);
    (void)alarm_logic_init(&s_alarm_channels[1]);
    
    return 1;
}

/* 将完整采样快照拷贝入 AlarmTask 队列，调用者缓冲区不会跨任务保存。 */
int alarm_task_sample_submit(const sample_snapshot_t *snapshot)
{
    if ((snapshot == 0) || (s_alarm_sample_queue_handle == 0)) return 0;
    return (xQueueSend(s_alarm_sample_queue_handle, snapshot, 0U) == pdPASS) ? 1 : 0;
}

/* 返回当前两个通道的 ACTIVE 位图，供诊断和测试读取。 */
uint8_t alarm_task_get_active_mask(void)
{
    return s_alarm_active_mask;
}

/* 返回已经进入 ACTIVE 的告警次数。 */
uint32_t alarm_task_get_trigger_count(void)
{
    return s_alarm_trigger_count;
}

/* 返回已经离开 ACTIVE 的恢复次数。 */
uint32_t alarm_task_get_recovery_count(void)
{
    return s_alarm_recovery_count;
}

uint32_t alarm_task_get_heartbeat(void)
{
    return s_alarm_task_heartbeat;
}

uint32_t alarm_task_get_stack_high_water_mark(void)
{
    return s_alarm_task_stack_high_water_mark;
}
