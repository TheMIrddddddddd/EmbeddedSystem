#include "health_task.h"

#include "gd32f4xx_fwdgt.h"

#include "FreeRTOS.h"
#include "task.h"

#include "protocol_task.h"
#include "sample_task.h"
#include "alarm_task.h"
#include "control_task.h"
#include "display_task.h"
#include "storage_task.h"

#define HEALTH_WATCHDOG_RELOAD          4095U
#define HEALTH_WATCHDOG_PRESCALER       FWDGT_PSC_DIV256

#define HEALTH_TASK_PRIORITY       5U
#define HEALTH_TASK_STACK_DEPTH    128U
#define HEALTH_MISSED_LIMIT        3U

typedef uint32_t (*health_metric_getter_t)(void);

typedef struct
{
    health_metric_getter_t heartbeat_getter;
    health_metric_getter_t stack_getter;

    uint32_t last_heartbeat;
    uint32_t missed_count;
    uint32_t stack_high_water_mark;

    uint8_t initialized;
    uint8_t healthy;
} health_monitor_item_t;

static StaticTask_t s_health_task_tcb;
static StackType_t s_health_task_stack[HEALTH_TASK_STACK_DEPTH];

static volatile uint32_t s_health_task_heartbeat;
static volatile uint8_t s_all_tasks_healthy;
static volatile uint32_t s_health_task_stack_high_water_mark;

static volatile uint8_t s_health_fault_latched;

static health_monitor_item_t s_health_monitor_items[] =
{
    { protocol_task_get_heartbeat, protocol_task_get_stack_high_water_mark, 0U, 0U, 0U, 0U, 1U },
    { sample_task_get_heartbeat  , sample_task_get_stack_high_water_mark  , 0U, 0U, 0U, 0U, 1U },
    { alarm_task_get_heartbeat   , alarm_task_get_stack_high_water_mark, 0U, 0U, 0U, 0U, 1U },
    { control_task_get_heartbeat , control_task_get_stack_high_water_mark, 0U, 0U, 0U, 0U, 1U },
    { display_task_get_heartbeat , display_task_get_stack_high_water_mark, 0U, 0U, 0U, 0U, 1U },
    { storage_task_get_heartbeat , storage_task_get_stack_high_water_mark, 0U, 0U, 0U, 0U, 1U }
};

static uint8_t health_watchdog_init(void)
{
    if (fwdgt_config(HEALTH_WATCHDOG_RELOAD, HEALTH_WATCHDOG_PRESCALER) != SUCCESS)
    {
        return 0U;
    }
    return 1U;
}

#define HEALTH_MONITOR_COUNT \
    (sizeof(s_health_monitor_items) / \
     sizeof(s_health_monitor_items[0]))

static uint8_t health_monitor_item_poll(
    health_monitor_item_t *item)
{
    uint32_t current_heartbeat;

    current_heartbeat = item->heartbeat_getter();
    item->stack_high_water_mark = item->stack_getter();

    if (item->initialized == 0U) {
        item->last_heartbeat = current_heartbeat;
        item->missed_count = 0U;
        item->initialized = 1U;
        item->healthy = 1U;
    }
    else if (current_heartbeat != item->last_heartbeat) {
        item->last_heartbeat = current_heartbeat;
        item->missed_count = 0U;
        item->healthy = 1U;
    }
    else {
        if (item->missed_count < HEALTH_MISSED_LIMIT) {
            item->missed_count++;
        }

        if (item->missed_count >= HEALTH_MISSED_LIMIT) {
            item->healthy = 0U;
        }
    }

    return item->healthy;
}

static uint8_t health_monitor_poll(void)
{
    uint32_t index;
    uint8_t all_healthy;

    all_healthy = 1U;

    for (index = 0U; index < HEALTH_MONITOR_COUNT; index++) {
        if (health_monitor_item_poll(
                &s_health_monitor_items[index]) == 0U) {
            all_healthy = 0U;
        }
    }

    return all_healthy;
}

static void health_task(void *argument)
{
    (void)argument;

    for (;;) {

        if (health_watchdog_init() == 0U)
        {
            taskDISABLE_INTERRUPTS();

            for (;;) {

            }
        }
        
        for (;;) {
            s_health_task_heartbeat++;
            s_all_tasks_healthy = health_monitor_poll();
            s_health_task_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark2(NULL); 

            if (s_all_tasks_healthy == 0U)
            {
                s_health_fault_latched = 1;
            }
            if (s_health_fault_latched == 0U)
            {
                fwdgt_counter_reload();
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }
}

int health_task_create(void)
{
    TaskHandle_t health_task_handle;

    health_task_handle = xTaskCreateStatic(
        health_task,
        "Health",
        HEALTH_TASK_STACK_DEPTH,
        NULL,
        HEALTH_TASK_PRIORITY,
        s_health_task_stack,
        &s_health_task_tcb);

    if (health_task_handle == NULL) {
        return 0;
    }

    return 1;
}

uint32_t health_task_get_heartbeat(void)
{
    return s_health_task_heartbeat;
}

uint8_t health_task_all_healthy(void)
{
    return s_all_tasks_healthy;
}
