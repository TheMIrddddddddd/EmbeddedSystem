#ifndef TEST_APP_PROTOCOL_APP_CONFIG_H
#define TEST_APP_PROTOCOL_APP_CONFIG_H

#include <stdint.h>

#define APP_CONFIG_CHANNEL_COUNT 2U

typedef struct
{
    uint16_t device_id;
    uint32_t rs485_baudrate;
    float ratio[APP_CONFIG_CHANNEL_COUNT];
    float limit[APP_CONFIG_CHANNEL_COUNT];
    uint8_t alarm_mode;
} app_config_t;

int app_config_get(app_config_t *out);
int app_config_device_id_set(uint16_t device_id);
int app_config_baudrate_set(uint32_t baudrate);
int app_config_limit_set(uint8_t channel, float limit);
int app_config_ratio_set(uint8_t channel, float ratio);
int app_config_alarm_mode_set(uint8_t mode);

#endif
