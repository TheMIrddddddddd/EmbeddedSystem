#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_CONFIG_CHANNEL_COUNT        2U
#define APP_CONFIG_RATIO_MIN            0.0f
#define APP_CONFIG_RATIO_MAX            100.0f
#define APP_CONFIG_LIMIT_MIN            0.0f
#define APP_CONFIG_LIMIT_MAX            500.0f
#define APP_CONFIG_SAMPLE_PERIOD_MIN_S  5U
#define APP_CONFIG_SAMPLE_PERIOD_MAX_S  15U
#define APP_CONFIG_DEVICE_ID_MIN        0x0001U
#define APP_CONFIG_DEVICE_ID_MAX        0xFFFEU
#define APP_CONFIG_MODBUS_DEVICE_ID_MAX 247U
#define APP_CONFIG_BAUDRATE_COUNT       5U

/* 配置作为一个 Flash KV value 保存，不能依赖 app_config_t 的 padding。 */
#define APP_CONFIG_PERSISTENCE_VERSION  1U
#define APP_CONFIG_SERIALIZED_SIZE      31U

typedef struct
{
    uint16_t device_id;             /* 0x0001~0xFFFE */
    uint8_t  sample_period_s;       /* 5 / 10 / 15，只控制打印与存储周期 */
    uint8_t  protocol_mode;         /* 0=自定义帧 1=Modbus RTU */
    uint8_t  alarm_mode;            /* alarm_report_mode: 1=主动上报 2=被动存储 */
    uint8_t  local_sample_enabled;  /* 只控制 CLI 打印与 TF 存储，不得停止底层采集 */
    uint8_t  hide_mode;             /* 0=正常格式 1=隐藏 HEX 格式 */
    uint8_t  reserved[3];
    uint32_t rs485_baudrate;        /* 9600/19200/38400/57600/115200 */
    float    ratio[APP_CONFIG_CHANNEL_COUNT];   /* 已乘变比前的倍率 */
    float    limit[APP_CONFIG_CHANNEL_COUNT];   /* 超限阈值，比较对象是乘过变比的工程量 */
} app_config_t;


int app_config_init(void);
int app_config_defaults(app_config_t *out);
int app_config_validate(const app_config_t *config);
int app_config_apply(const app_config_t *config);
int app_config_encode(const app_config_t *config,
                      uint8_t *buffer,
                      uint16_t capacity,
                      uint16_t *length);
int app_config_decode(const uint8_t *buffer,
                      uint16_t length,
                      app_config_t *config);
int app_config_get(app_config_t *out);
int app_config_device_id_set(uint16_t device_id);
int app_config_sample_period_set(uint8_t seconds);
int app_config_sample_period_key_set(uint16_t key_id);
int app_config_protocol_mode_set(uint8_t mode);
int app_config_alarm_mode_set(uint8_t mode);
int app_config_baudrate_set(uint32_t baudrate);
int app_config_sample_enable_set(uint8_t enabled);
int app_config_hide_mode_set(uint8_t hidden);
int app_config_ratio_set(uint8_t channel, float ratio);
int app_config_limit_set(uint8_t channel, float limit);

#endif /* APP_CONFIG_H */
