#include "app_config.h"

#include <string.h>

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

static int app_config_baudrate_valid(uint32_t baudrate)
{
    uint8_t index;

    for (index = 0U; index < APP_CONFIG_BAUDRATE_COUNT; index++)
    {
        if (s_baudrate_table[index] == baudrate)
        {
            return 1;
        }
    }

    return 0;
}

static void app_config_u16_store_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t app_config_u16_load_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

static void app_config_u32_store_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFUL);
    buffer[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    buffer[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    buffer[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint32_t app_config_u32_load_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static uint32_t app_config_float_to_u32(float value)
{
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float app_config_u32_to_float(uint32_t bits)
{
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

int app_config_defaults(app_config_t *out)
{
    if (out == NULL)
    {
        return 0;
    }

    out->device_id = 0x0001U;
    out->sample_period_s = 5U;
    out->protocol_mode = 0U;
    out->alarm_mode = 2U;
    out->local_sample_enabled = 0U;
    out->hide_mode = 0U;
    out->reserved[0] = 0U;
    out->reserved[1] = 0U;
    out->reserved[2] = 0U;
    out->rs485_baudrate = 115200U;

    out->ratio[0] = 1.0f;
    out->ratio[1] = 1.0f;

    out->limit[0] = 2.50f;
    out->limit[1] = 10.50f;

    return 1;
}

int app_config_init(void)
{
    return app_config_defaults(&s_app_config);
}

int app_config_validate(const app_config_t *config)
{
    uint8_t index;

    if (config == NULL)
    {
        return 0;
    }

    if ((config->device_id < APP_CONFIG_DEVICE_ID_MIN) ||
        (config->device_id > APP_CONFIG_DEVICE_ID_MAX))
    {
        return 0;
    }

    if ((config->sample_period_s < APP_CONFIG_SAMPLE_PERIOD_MIN_S) ||
        (config->sample_period_s > APP_CONFIG_SAMPLE_PERIOD_MAX_S))
    {
        return 0;
    }

    if (config->protocol_mode > 1U)
    {
        return 0;
    }

    if ((config->protocol_mode == 1U) &&
        (config->device_id > APP_CONFIG_MODBUS_DEVICE_ID_MAX))
    {
        return 0;
    }

    if ((config->alarm_mode != 1U) && (config->alarm_mode != 2U))
    {
        return 0;
    }

    if ((config->local_sample_enabled > 1U) ||
        (config->hide_mode > 1U))
    {
        return 0;
    }

    if (config->reserved[0] != 0U ||
        config->reserved[1] != 0U ||
        config->reserved[2] != 0U)
    {
        return 0;
    }

    if (app_config_baudrate_valid(config->rs485_baudrate) == 0)
    {
        return 0;
    }

    for (index = 0U; index < APP_CONFIG_CHANNEL_COUNT; index++)
    {
        if (app_config_float_valid(config->ratio[index],
                                   APP_CONFIG_RATIO_MIN,
                                   APP_CONFIG_RATIO_MAX) == 0)
        {
            return 0;
        }

        if (app_config_float_valid(config->limit[index],
                                   APP_CONFIG_LIMIT_MIN,
                                   APP_CONFIG_LIMIT_MAX) == 0)
        {
            return 0;
        }
    }

    return 1;
}

int app_config_apply(const app_config_t *config)
{
    if (app_config_validate(config) == 0)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    s_app_config = *config;

    taskEXIT_CRITICAL();

    return 1;
}

int app_config_encode(const app_config_t *config,
                      uint8_t *buffer,
                      uint16_t capacity,
                      uint16_t *length)
{
    uint8_t index;

    if (length == NULL)
    {
        return 0;
    }

    *length = 0U;

    if ((config == NULL) ||
        (buffer == NULL) ||
        (capacity < APP_CONFIG_SERIALIZED_SIZE) ||
        (app_config_validate(config) == 0))
    {
        return 0;
    }

    buffer[0] = APP_CONFIG_PERSISTENCE_VERSION;
    app_config_u16_store_le(&buffer[1], config->device_id);
    buffer[3] = config->sample_period_s;
    buffer[4] = config->protocol_mode;
    buffer[5] = config->alarm_mode;
    buffer[6] = config->local_sample_enabled;
    buffer[7] = config->hide_mode;
    buffer[8] = config->reserved[0];
    buffer[9] = config->reserved[1];
    buffer[10] = config->reserved[2];
    app_config_u32_store_le(&buffer[11], config->rs485_baudrate);

    for (index = 0U; index < APP_CONFIG_CHANNEL_COUNT; index++)
    {
        app_config_u32_store_le(
            &buffer[15U + (uint16_t)(index * 4U)],
            app_config_float_to_u32(config->ratio[index]));
        app_config_u32_store_le(
            &buffer[23U + (uint16_t)(index * 4U)],
            app_config_float_to_u32(config->limit[index]));
    }

    *length = APP_CONFIG_SERIALIZED_SIZE;
    return 1;
}

int app_config_decode(const uint8_t *buffer,
                      uint16_t length,
                      app_config_t *config)
{
    app_config_t decoded;
    uint8_t index;

    if ((buffer == NULL) ||
        (config == NULL) ||
        (length != APP_CONFIG_SERIALIZED_SIZE) ||
        (buffer[0] != APP_CONFIG_PERSISTENCE_VERSION))
    {
        return 0;
    }

    (void)memset(&decoded, 0, sizeof(decoded));

    decoded.device_id = app_config_u16_load_le(&buffer[1]);
    decoded.sample_period_s = buffer[3];
    decoded.protocol_mode = buffer[4];
    decoded.alarm_mode = buffer[5];
    decoded.local_sample_enabled = buffer[6];
    decoded.hide_mode = buffer[7];
    decoded.reserved[0] = buffer[8];
    decoded.reserved[1] = buffer[9];
    decoded.reserved[2] = buffer[10];
    decoded.rs485_baudrate = app_config_u32_load_le(&buffer[11]);

    for (index = 0U; index < APP_CONFIG_CHANNEL_COUNT; index++)
    {
        decoded.ratio[index] = app_config_u32_to_float(
            app_config_u32_load_le(
                &buffer[15U + (uint16_t)(index * 4U)]));
        decoded.limit[index] = app_config_u32_to_float(
            app_config_u32_load_le(
                &buffer[23U + (uint16_t)(index * 4U)]));
    }

    if (app_config_validate(&decoded) == 0)
    {
        return 0;
    }

    *config = decoded;
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

int app_config_sample_period_key_set(uint16_t key_id)
{
    uint8_t seconds;

    switch (key_id)
    {
    case 2U:
        seconds = 5U;
        break;

    case 3U:
        seconds = 10U;
        break;

    case 4U:
        seconds = 15U;
        break;

    default:
        return 0;
    }

    return app_config_sample_period_set(seconds);
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
