#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

#include "task_queues.h"

/*
 * RS485 自定义协议业务分发（《01》七-5）。
 * 与 CLI 命令表分开维护，仅在业务层复用 app_config / sample_task。
 * 由 ControlTask 上下文调用；入参请求、出参结果，不做任何收发。
 */
void app_protocol_execute(const protocol_request_t *request, protocol_result_t *result);

/* 广播帧允许静默执行的命令字判定（《01》七-6：查询/升级/改 ID 禁止广播） */
uint8_t app_protocol_broadcast_allowed(uint16_t operation);

/* 自动上报使能（七-8 忙碌策略框架，M4-4d 自动上报本体启用） */
void app_protocol_auto_report_set(uint8_t enabled);
uint8_t app_protocol_auto_report_enabled(void);

void app_protocol_report_interval_set(uint16_t seconds);
uint16_t app_protocol_report_interval_get(void);

#define APP_PROTOCOL_CMD_SET_ALARM_MODE       0x0601U
#define APP_PROTOCOL_CMD_QUERY_ALARM_RECORDS  0x0602U
#define APP_PROTOCOL_CMD_CLEAR_ALARM_RECORDS  0x0603U
#define APP_PROTOCOL_CMD_ALARM_EVENT          0x0681U
#define APP_PROTOCOL_ALARM_MODE_ACTIVE        0x01U
#define APP_PROTOCOL_ALARM_MODE_PASSIVE       0x02U

uint8_t app_protocol_alarm_report_enabled(void);

/* 0x0101/0x0500：应答发出后由 ProtocolTask 轮询并执行复位 */
uint8_t app_protocol_reboot_pending(void);

#define APP_PROTOCOL_CMD_ENTER_BOOT          0x0500U
#define APP_PROTOCOL_ERROR_UPGRADE_STATE    0x66U
#define APP_PROTOCOL_ERROR_UPGRADE_STORAGE  0x67U

/* 数据区大端序写助手（ProtocolTask 构造事件帧复用） */
void app_protocol_store_u16_be(uint8_t *dst, uint16_t value);
void app_protocol_store_u32_be(uint8_t *dst, uint32_t value);
void app_protocol_store_float_be(uint8_t *dst, float value);

#endif /* APP_PROTOCOL_H */
