#ifndef TEST_APP_PROTOCOL_SAMPLE_TASK_H
#define TEST_APP_PROTOCOL_SAMPLE_TASK_H

#include <stdint.h>

typedef struct
{
    uint32_t sequence;
    float value_ch0;
    float value_ch1;
} sample_snapshot_t;

int sample_task_snapshot_get(sample_snapshot_t *snapshot);
int sample_task_ratio_set(uint8_t channel, float ratio);

#endif
