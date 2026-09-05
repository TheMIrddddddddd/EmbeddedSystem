#ifndef SAMPLE_TASK_H
#define SAMPLE_TASK_H

#include <stdint.h>

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint16_t raw_ch0;
    uint16_t raw_ch1;
} sample_snapshot_t;

int sample_task_create(void);
uint32_t sample_task_get_heartbeat(void);
uint32_t sample_task_get_stack_high_water_mark(void);
uint32_t sample_task_get_adc_error_count(void);

int sample_task_snapshot_get(sample_snapshot_t *snapshot);
#endif /* SAMPLE_TASK_H */
