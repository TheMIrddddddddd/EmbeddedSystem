#ifndef ALARM_TASK_H
#define ALARM_TASK_H

#include <stdint.h>
#include "sample_task.h"

#define ALARM_TASK_EVENT_QUEUE_LENGTH 8U

typedef struct
{
    uint32_t timestamp;
    uint8_t channel;
    uint8_t reserved[3];
    float threshold;
    float actual;
} alarm_task_event_t;

int alarm_task_create(void);
int alarm_task_sample_submit(const sample_snapshot_t *snapshot);
int alarm_task_event_receive(alarm_task_event_t *event, uint32_t timeout_ms);
uint32_t alarm_task_get_heartbeat(void);
uint32_t alarm_task_get_stack_high_water_mark(void);
uint8_t alarm_task_get_active_mask(void);
uint32_t alarm_task_get_trigger_count(void);
uint32_t alarm_task_get_recovery_count(void);

#endif /* ALARM_TASK_H */
