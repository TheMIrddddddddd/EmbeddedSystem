#ifndef SAMPLE_TASK_H
#define SAMPLE_TASK_H

#include <stdint.h>

#define SAMPLE_CHANNEL_COUNT        2U
#define SAMPLE_FILTER_DEPTH         3U

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
 
    /* 本次 DMA 搬回的原始码，未滤波 */
    uint16_t raw_ch0;
    uint16_t raw_ch1;
 
    /* 滑动窗口均值后的码值 */
    uint16_t filtered_ch0;
    uint16_t filtered_ch1;
 
    /* filtered 换算出的引脚电压，单位 mV，未乘变比 */
    uint16_t voltage_mv_ch0;
    uint16_t voltage_mv_ch1;
 
    /* voltage_mv / 1000 * ratio，已乘变比的工程量：CLI 打印、阈值比较、0x0201 查询都用这个 */
    float value_ch0;
    float value_ch1;
 
    /* 窗口内有效点数，上电后从 1 递增到 SAMPLE_FILTER_DEPTH */
    uint8_t filter_count;
    /* filter_count 达到 SAMPLE_FILTER_DEPTH 后置 1，之前均值是不足 3 点的部分平均 */
    uint8_t filter_ready;
    uint8_t reserved[2];
} sample_snapshot_t;

int sample_task_create(void);
uint32_t sample_task_get_heartbeat(void);
uint32_t sample_task_get_stack_high_water_mark(void);
uint32_t sample_task_get_adc_error_count(void);

int sample_task_snapshot_get(sample_snapshot_t *snapshot);

int sample_task_ratio_set(uint8_t channel, float ratio);
float sample_task_ratio_get(uint8_t channel);

#endif /* SAMPLE_TASK_H */
