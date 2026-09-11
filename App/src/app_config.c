#include "app_config.h"

#include "FreeRTOS.h"
#include "task.h"

static const uint32_t s_baudrate_table[APP_CONFIG_BAUDRATE_COUNT] =
{
    9600U,
    19200U,
    38400U,
    57600U,
    115200U
};

static app_config_t s_app_config;

static int app_config_float_valid(float value, float min, float max)
{
    return ((value >= min) && (value <= max)) ? 1 : 0;
}

int app_config_init(void)
{
    s_app_config.device_id = 0x0001U;
    s_app_config.sample_period_s = 5U;
    s_app_config.protocol_mode = 0U;
    s_app_config.alarm_mode = 2U;
    s_app_config.local_sample_enabled = 0U;
    s_app_config.hide_mode = 0U;
    s_app_config.reserved[0] = 0U;
    s_app_config.reserved[1] = 0U;
    s_app_config.reserved[2] = 0U;
    s_app_config.rs485_baudrate = 115200U;
 
    s_app_config.ratio[0] = 1.0f;
    s_app_config.ratio[1] = 1.0f;
 
    s_app_config.limit[0] = 2.50f;
    s_app_config.limit[1] = 10.50f;
 
    return 1;
}

int app_config_get(app_config_t *out)
{
    if (out == NULL)
    {
        return 0;
    }
    
    taskENTER_CRITICAL();

    *out = s_app_config;
    taskEXIT_CRITICAL();

    return 1;
}

int app_config_device_id_set(uint16_t device_id)
{
    int valid;

    if ((device_id < APP_CONFIG_DEVICE_ID_MIN) ||
        (device_id > APP_CONFIG_DEVICE_ID_MAX))
    {
        return 0;
    }

    taskENTER_CRITICAL();

    valid = ((s_app_config.protocol_mode == 0U) ||
             (device_id <= APP_CONFIG_MODBUS_DEVICE_ID_MAX)) ? 1 : 0;

    if (valid == 0)
    {
        taskEXIT_CRITICAL();
        return 0;
    }

    s_app_config.device_id = device_id;

    taskEXIT_CRITICAL();

    return 1;
}

int app_config_sample_period_set(uint8_t seconds)
{
    if ((seconds < APP_CONFIG_SAMPLE_PERIOD_MIN_S) ||
        (seconds > APP_CONFIG_SAMPLE_PERIOD_MAX_S))
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_app_config.sample_period_s = seconds;
 
    taskEXIT_CRITICAL();
 
    return 1;
}

int app_config_protocol_mode_set(uint8_t mode)
{
    int valid;

    if (mode > 1U)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    valid = ((mode == 0U) ||
             ((s_app_config.device_id != 0U) &&
              (s_app_config.device_id <= APP_CONFIG_MODBUS_DEVICE_ID_MAX))) ? 1 : 0;

    if (valid == 0)
    {
        taskEXIT_CRITICAL();
        return 0;
    }

    s_app_config.protocol_mode = mode;

    taskEXIT_CRITICAL();

    return 1;
}

int app_config_alarm_mode_set(uint8_t mode)
{
    if ((mode != 1U) && (mode != 2U))
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_app_config.alarm_mode = mode;
 
    taskEXIT_CRITICAL();
 
    return 1;
}

int app_config_baudrate_set(uint32_t baudrate)
{
    uint8_t index;
    uint8_t found;
 
    found = 0U;
 
    for (index = 0U; index < APP_CONFIG_BAUDRATE_COUNT; index++)
    {
        if (s_baudrate_table[index] == baudrate)
        {
            found = 1U;
            break;
        }
    }
 
    if (found == 0U)
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_app_config.rs485_baudrate = baudrate;
 
    taskEXIT_CRITICAL();
 
    return 1;
}

int app_config_sample_enable_set(uint8_t enabled)
{
    taskENTER_CRITICAL();
 
    s_app_config.local_sample_enabled = (enabled != 0U) ? 1U : 0U;
 
    taskEXIT_CRITICAL();
 
    return 1;
}
 
int app_config_hide_mode_set(uint8_t hidden)
{
    taskENTER_CRITICAL();
 
    s_app_config.hide_mode = (hidden != 0U) ? 1U : 0U;
 
    taskEXIT_CRITICAL();
 
    return 1;
}
 
int app_config_ratio_set(uint8_t channel, float ratio)
{
    if (channel >= APP_CONFIG_CHANNEL_COUNT)
    {
        return 0;
    }
 
    if (app_config_float_valid(ratio, APP_CONFIG_RATIO_MIN, APP_CONFIG_RATIO_MAX) == 0)
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_app_config.ratio[channel] = ratio;
 
    taskEXIT_CRITICAL();
 
    return 1;
}
 
int app_config_limit_set(uint8_t channel, float limit)
{
    if (channel >= APP_CONFIG_CHANNEL_COUNT)
    {
        return 0;
    }
 
    if (app_config_float_valid(limit, APP_CONFIG_LIMIT_MIN, APP_CONFIG_LIMIT_MAX) == 0)
    {
        return 0;
    }
 
    taskENTER_CRITICAL();
 
    s_app_config.limit[channel] = limit;
 
    taskEXIT_CRITICAL();
 
    return 1;
}
