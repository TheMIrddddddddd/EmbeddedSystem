#include <string.h>

#include "app_protocol.h"
#include "app_config.h"
#include "sample_task.h"
#include "board_dac.h"
#include "board_rtc.h"
#include "board_spi_flash.h"
#include "storage_task.h"

/* 《01》七-4 错误码 */
#define APP_PROTOCOL_STATUS_OK              0x00U
#define APP_PROTOCOL_ERROR_LENGTH           0x02U
#define APP_PROTOCOL_ERROR_ILLEGAL_COMMAND  0x03U
#define APP_PROTOCOL_ERROR_ILLEGAL_VALUE    0x04U
#define APP_PROTOCOL_ERROR_BUSY             0x05U

/* 协议版本号 4B：主.次.修订.构建（与 APP_CLI_VERSION 同步演进） */
#define APP_PROTOCOL_VERSION_MAJOR          0x00U
#define APP_PROTOCOL_VERSION_MINOR          0x01U
#define APP_PROTOCOL_VERSION_REVISION       0x00U
#define APP_PROTOCOL_VERSION_BUILD          0x01U

/* 本实现覆盖的命令字（M4-4c；0x03xx 上报类归 M4-4d，0x05xx/0x06xx 归 M6/M5） */
#define APP_PROTOCOL_CMD_REBOOT             0x0101U
#define APP_PROTOCOL_CMD_QUERY_VERSION      0x0102U
#define APP_PROTOCOL_CMD_QUERY_ID           0x0103U
#define APP_PROTOCOL_CMD_SET_ID             0x0104U
#define APP_PROTOCOL_CMD_QUERY_BAUD         0x0105U
#define APP_PROTOCOL_CMD_SET_BAUD           0x0106U
#define APP_PROTOCOL_CMD_QUERY_CH0          0x0201U
#define APP_PROTOCOL_CMD_QUERY_CH1          0x0202U
#define APP_PROTOCOL_CMD_QUERY_BOTH         0x0203U
#define APP_PROTOCOL_CMD_SET_DAC            0x0301U
#define APP_PROTOCOL_CMD_READ_LIMITS        0x0401U
#define APP_PROTOCOL_CMD_SET_LIMIT_CH0      0x0402U
#define APP_PROTOCOL_CMD_SET_LIMIT_CH1      0x0403U
#define APP_PROTOCOL_CMD_SET_RATIO_CH0      0x0404U
#define APP_PROTOCOL_CMD_SET_RATIO_CH1      0x0405U
#define APP_PROTOCOL_CMD_TF_STATUS          0x0701U
#define APP_PROTOCOL_CMD_SELF_TEST          0x0801U

#define APP_PROTOCOL_CMD_START_REPORT       0x0302U
#define APP_PROTOCOL_CMD_STOP_REPORT        0x0303U
#define APP_PROTOCOL_CMD_SET_REPORT_INTERVAL 0x0304U
#define APP_PROTOCOL_REPORT_INTERVAL_MIN_S   1U
#define APP_PROTOCOL_REPORT_INTERVAL_MAX_S     86400U
#define APP_PROTOCOL_REPORT_INTERVAL_DEFAULT_S 5U


/* 广播允许静默执行的写命令（《01》七-6；查询/改 ID/升级禁止） */
static const uint16_t s_broadcast_allowed[] =
{
    APP_PROTOCOL_CMD_REBOOT,
    APP_PROTOCOL_CMD_SET_DAC,
    APP_PROTOCOL_CMD_SET_LIMIT_CH0,
    APP_PROTOCOL_CMD_SET_LIMIT_CH1,
    APP_PROTOCOL_CMD_SET_RATIO_CH0,
    APP_PROTOCOL_CMD_SET_RATIO_CH1
};

#define APP_PROTOCOL_BROADCAST_ALLOWED_COUNT \
    (sizeof(s_broadcast_allowed) / sizeof(s_broadcast_allowed[0]))

/* 七-8 忙碌策略框架：自动上报期间仅 0x0303/0x03AA 放行（4d 启用） */
static uint8_t s_auto_report_enabled;

/* 0x0101 应答发出后由 ProtocolTask 复位 */
static uint8_t s_reboot_pending;

static uint16_t s_report_interval_s = APP_PROTOCOL_REPORT_INTERVAL_DEFAULT_S;

void app_protocol_store_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t app_protocol_load_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

void app_protocol_store_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)(value & 0xFFU);
}

static uint32_t app_protocol_load_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

/* 文档规定数据区大端，Cortex-M 内存是小端，读写都翻字节 */
void app_protocol_store_float_be(uint8_t *dst, float value)
{
    uint8_t little[4];

    (void)memcpy(little, &value, 4U);
    dst[0] = little[3];
    dst[1] = little[2];
    dst[2] = little[1];
    dst[3] = little[0];
}

static float app_protocol_load_float_be(const uint8_t *src)
{
    uint8_t little[4];
    float value;

    little[0] = src[3];
    little[1] = src[2];
    little[2] = src[1];
    little[3] = src[0];
    (void)memcpy(&value, little, 4U);

    return value;
}

static void app_protocol_query_channel(uint8_t channel, uint8_t *dst,
                                       uint16_t *length, protocol_result_t *result)
{
    sample_snapshot_t snapshot;
    float value;

    if (sample_task_snapshot_get(&snapshot) == 0)
    {
        result->status = APP_PROTOCOL_ERROR_BUSY;
        return;
    }

    value = (channel == 0U) ? snapshot.value_ch0 : snapshot.value_ch1;

    app_protocol_store_float_be(&dst[*length], value);
    *length = (uint16_t)(*length + 4U);
}

uint8_t app_protocol_broadcast_allowed(uint16_t operation)
{
    uint8_t index;

    for (index = 0U; index < APP_PROTOCOL_BROADCAST_ALLOWED_COUNT; index++)
    {
        if (s_broadcast_allowed[index] == operation)
        {
            return 1U;
        }
    }

    return 0U;
}

void app_protocol_auto_report_set(uint8_t enabled)
{
    s_auto_report_enabled = (enabled != 0U) ? 1U : 0U;
}

uint8_t app_protocol_auto_report_enabled(void)
{
    return s_auto_report_enabled;
}

uint8_t app_protocol_reboot_pending(void)
{
    return s_reboot_pending;
}

void app_protocol_report_interval_set(uint16_t seconds)
{
    s_report_interval_s = seconds;
}

uint16_t app_protocol_report_interval_get(void)
{
    return s_report_interval_s;
}

void app_protocol_execute(const protocol_request_t *request, protocol_result_t *result)
{
    app_config_t config;
    uint16_t operation = request->operation;
    uint16_t payload_length = request->payload_length;
    const uint8_t *payload = request->payload;

    (void)memset(result, 0, sizeof(*result));
    result->request_id = request->request_id;
    result->mode_epoch = request->mode_epoch;
    result->device_address = request->device_address;
    result->operation = request->operation;
    result->protocol_kind = request->protocol_kind;
    result->reply_required = (request->device_address == 0xFFFFU) ? 0U : 1U;
    result->apply_flags = PROTOCOL_RESULT_APPLY_NONE;
    result->protocol_sequence = request->protocol_sequence;
    result->status = APP_PROTOCOL_STATUS_OK;
    result->payload_length = 0U;

    /* 七-8：自动上报期间仅 0x0303/0x03AA 放行（4d 启用上报后生效） */
    if ((s_auto_report_enabled != 0U) &&
        (operation != 0x0303U) && (operation != 0x03AAU))
    {
        result->status = APP_PROTOCOL_ERROR_BUSY;
        return;
    }

    switch (operation)
    {
    case APP_PROTOCOL_CMD_REBOOT:
        /* 应答帧发出后 ProtocolTask 执行复位 */
        s_reboot_pending = 1U;
        break;

    case APP_PROTOCOL_CMD_QUERY_VERSION:
        result->payload[0] = APP_PROTOCOL_VERSION_MAJOR;
        result->payload[1] = APP_PROTOCOL_VERSION_MINOR;
        result->payload[2] = APP_PROTOCOL_VERSION_REVISION;
        result->payload[3] = APP_PROTOCOL_VERSION_BUILD;
        result->payload_length = 4U;
        break;

    case APP_PROTOCOL_CMD_QUERY_ID:
        (void)app_config_get(&config);
        app_protocol_store_u16_be(result->payload, config.device_id);
        result->payload_length = 2U;
        break;

    case APP_PROTOCOL_CMD_SET_ID:
        if (payload_length != 2U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }

        if (app_config_device_id_set(app_protocol_load_u16_be(payload)) == 0)
        {
            result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
        }
        else
        {
            (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_DEVICE_ID_SET,
                                                  0U, 0.0f, 0.0f,
                                                  app_protocol_load_u16_be(payload));
        }
        break;

    case APP_PROTOCOL_CMD_QUERY_BAUD:
        (void)app_config_get(&config);
        app_protocol_store_u32_be(result->payload, config.rs485_baudrate);
        result->payload_length = 4U;
        break;

    case APP_PROTOCOL_CMD_SET_BAUD:
        /* 应答在旧波特率发完后，由 ProtocolTask 再应用硬件参数；持久化归 M5。 */
        if (payload_length != 4U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }

        {
            uint32_t baudrate = app_protocol_load_u32_be(payload);

            if (app_config_baudrate_set(baudrate) == 0)
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
            }
            else
            {
                result->next_baudrate = baudrate;
                result->apply_flags |= PROTOCOL_RESULT_APPLY_BAUD;
                (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_BAUDRATE_SET,
                                                      0U, 0.0f, 0.0f, baudrate);
            }
        }
        break;

    case APP_PROTOCOL_CMD_QUERY_CH0:
        app_protocol_query_channel(0U, result->payload, &result->payload_length, result);
        break;

    case APP_PROTOCOL_CMD_QUERY_CH1:
        app_protocol_query_channel(1U, result->payload, &result->payload_length, result);
        break;

    case APP_PROTOCOL_CMD_QUERY_BOTH:
        app_protocol_query_channel(0U, result->payload, &result->payload_length, result);
        if (result->status == APP_PROTOCOL_STATUS_OK)
        {
            app_protocol_query_channel(1U, result->payload, &result->payload_length, result);
        }
        break;

    case APP_PROTOCOL_CMD_SET_DAC:
        if (payload_length != 2U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }
        {
            uint16_t dac_value = app_protocol_load_u16_be(payload);

            /* 《01》七-5：2B 0x0000~0x0FFF（12 位 DAC） */
            if (dac_value > 0x0FFFU)
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
                break;
            }

            if (board_dac_output_set(dac_value) == 0)
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
            }
        }
        break;

    case APP_PROTOCOL_CMD_READ_LIMITS:
        (void)app_config_get(&config);
        app_protocol_store_float_be(&result->payload[0], config.limit[0]);
        app_protocol_store_float_be(&result->payload[4], config.limit[1]);
        result->payload_length = 8U;
        break;

    case APP_PROTOCOL_CMD_SET_LIMIT_CH0:
    case APP_PROTOCOL_CMD_SET_LIMIT_CH1:
        if (payload_length != 4U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }
        {
            uint8_t channel = (operation == APP_PROTOCOL_CMD_SET_LIMIT_CH0) ? 0U : 1U;

            if (app_config_limit_set(channel,
                                     app_protocol_load_float_be(payload)) == 0)
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
            }
            else
            {
                (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_LIMIT_SET,
                                                      channel,
                                                      app_protocol_load_float_be(payload),
                                                      0.0f, 0U);
            }
        }
        break;

    case APP_PROTOCOL_CMD_SET_RATIO_CH0:
    case APP_PROTOCOL_CMD_SET_RATIO_CH1:
        if (payload_length != 4U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }
        {
            uint8_t channel = (operation == APP_PROTOCOL_CMD_SET_RATIO_CH0) ? 0U : 1U;
            float ratio = app_protocol_load_float_be(payload);

            if (app_config_ratio_set(channel, ratio) == 0)
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
                break;
            }

            /* 与 CLI 同款双写：配置模型留底，SampleTask 立即生效 */
            (void)sample_task_ratio_set(channel, ratio);
            (void)storage_task_audit_event_submit(STORAGE_TASK_AUDIT_RATIO_SET,
                                                  channel, ratio, 0.0f, 0U);
        }
        break;

    case APP_PROTOCOL_CMD_TF_STATUS:
    {
        storage_task_sdio_diag_t diag;

        /* 1B：1=卡在位且已挂载，0=不可用 */
        result->payload[0] = 0U;

        (void)storage_task_sdio_diag_get(&diag);
        result->payload[0] = storage_task_fatfs_mounted_get();

        result->payload_length = 1U;
        break;
    }

    case APP_PROTOCOL_CMD_SELF_TEST:
    {
        storage_task_sdio_diag_t diag;
        board_rtc_time_t time;
        uint8_t jedec_id[3];
        uint8_t flash_pass = 0U;
        uint8_t tf_pass = 0U;
        uint8_t rtc_pass = 0U;

        if (board_spi_flash_read_jedec_id(jedec_id) != 0)
        {
            flash_pass = ((jedec_id[0] == 0xC8U) &&
                          (jedec_id[1] == 0x40U) &&
                          (jedec_id[2] == 0x13U)) ? 1U : 0U;
        }

        (void)storage_task_sdio_diag_get(&diag);
        tf_pass = storage_task_fatfs_mounted_get();

        /* 布局：[0]=OLED(启动已验证) [1]=Flash [2]=TF [3]=RTC */
        result->payload[0] = 1U;
        result->payload[1] = flash_pass;
        result->payload[2] = tf_pass;
        result->payload[3] = 0U;

        rtc_pass = 0U;

        if (board_rtc_time_get(&time) != 0)
        {
            rtc_pass = ((time.year != 2000U) || (time.month != 1U) ||
                        (time.date != 1U) || (time.hour != 0U) ||
                        (time.minute != 0U) || (time.second != 0U)) ? 1U : 0U;
        }

        result->payload[3] = rtc_pass;
        result->payload_length = 4U;
        break;
    }

    case APP_PROTOCOL_CMD_START_REPORT:
        if (payload_length != 0U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }
        
        app_protocol_auto_report_set(1U);
        break;

    case APP_PROTOCOL_CMD_STOP_REPORT:
        if (payload_length != 0U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }

        app_protocol_auto_report_set(0U);
        break;

        case APP_PROTOCOL_CMD_SET_REPORT_INTERVAL:
        if (payload_length != 2U)
        {
            result->status = APP_PROTOCOL_ERROR_LENGTH;
            break;
        }
        {
            uint16_t seconds = app_protocol_load_u16_be(payload);

            if ((seconds < APP_PROTOCOL_REPORT_INTERVAL_MIN_S) ||
                (seconds > APP_PROTOCOL_REPORT_INTERVAL_MAX_S))
            {
                result->status = APP_PROTOCOL_ERROR_ILLEGAL_VALUE;
                break;
            }

            app_protocol_report_interval_set(seconds);
        }
        break;

    /* 0x0381/0x0382 事件帧由 ProtocolTask 主动发送；0x03AA 归 M7；
     * 0x05xx 归 M6；0x06xx/0x0702 归 M5——落到 default 按非法命令字处理 */
    default:
        result->status = APP_PROTOCOL_ERROR_ILLEGAL_COMMAND;
        result->payload_length = 0U;
        break;
    }
}
