#include "app_config_import.h"
#include "app_config_ini.h"

/* 比较配置语义字段，不比较结构体 padding。 */
static int app_config_import_equal(const app_config_t *a, const app_config_t *b)
{
    uint8_t i;
    if ((a == 0) || (b == 0) || (a->device_id != b->device_id) ||
        (a->sample_period_s != b->sample_period_s) ||
        (a->protocol_mode != b->protocol_mode) || (a->alarm_mode != b->alarm_mode) ||
        (a->local_sample_enabled != b->local_sample_enabled) ||
        (a->hide_mode != b->hide_mode) || (a->rs485_baudrate != b->rs485_baudrate)) return 0;
    for (i=0U; i<APP_CONFIG_CHANNEL_COUNT; i++)
        if ((a->ratio[i] != b->ratio[i]) || (a->limit[i] != b->limit[i])) return 0;
    return (a->reserved[0] == b->reserved[0]) && (a->reserved[1] == b->reserved[1]) &&
           (a->reserved[2] == b->reserved[2]);
}

/* 完成 config.ini → candidate → 31 字节 encode → decode 回读闭环。 */
int app_config_import_prepare(const char *file, uint16_t file_length,
                              const app_config_t *base, app_config_t *candidate,
                              uint8_t *encoded, uint16_t capacity,
                              uint16_t *encoded_length, uint16_t *error_line)
{
    app_config_t parsed, decoded;
    uint8_t buffer[APP_CONFIG_SERIALIZED_SIZE];
    uint16_t length, i;
    if ((file == 0) || (base == 0) || (candidate == 0) || (encoded == 0) ||
        (encoded_length == 0) || (error_line == 0) || (capacity < APP_CONFIG_SERIALIZED_SIZE)) return 0;
    parsed = *base;
    if (app_config_ini_parse_file(file, file_length, base, &parsed, error_line) == 0) return 0;
    if (app_config_encode(&parsed, buffer, sizeof(buffer), &length) == 0) return 0;
    if ((length != APP_CONFIG_SERIALIZED_SIZE) || (app_config_decode(buffer, length, &decoded) == 0) ||
        (app_config_import_equal(&parsed, &decoded) == 0)) return 0;
    *candidate = decoded;
    for (i=0U; i<APP_CONFIG_SERIALIZED_SIZE; i++) encoded[i] = buffer[i];
    *encoded_length = APP_CONFIG_SERIALIZED_SIZE;
    return 1;
}
