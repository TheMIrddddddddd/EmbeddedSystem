#ifndef APP_CLI_H
#define APP_CLI_H

#include <stdint.h>

#include "board_rtc.h"

#define APP_CLI_VERSION    "0.1.0-m4.3d"

/*
 * USART0 控制台输出（M4-3a）。
 *
 * 分块原因：TX 环形缓冲 512B，board_usart0_send_buffer() 非阻塞且满了
 * 直接丢。长输出每次只装当前剩余空间，装不下就等 ISR 排空再装。
 *
 * 上下文约束（《01》四-5）：只允许任务上下文调用，内部会用 vTaskDelay
 * 等待排空。ISR 需要输出时走事件通知任务，不得直接调这里。
 */
int app_cli_write(const char *data, uint16_t length);

/* 输出文本并追加 \r\n */
int app_cli_print(const char *text);

/* 开机 banner：头部 + 命令列表，help 命令复用同一份文本 */
void app_cli_banner_print(void);

/*
 * 解析并执行一行命令。行必须已去掉行尾的 \r / \n，NUL 结尾。
 * 由 ControlTask 在收到完整行后调用。
 * 内部维护两段式命令（ratio/limit/protocol/id/baud）的 pending
 * 输入状态：进入等待后，下一行按数值处理而不是命令。
 */
void app_cli_execute_line(const char *line);

/*
 * 文本格式化 helper（M4-3c 起由 app_cli 统一提供，ControlTask 共用，
 * 避免 snprintf 拉入浮点格式化库）。均返回写入字符数。
 */
uint16_t app_cli_u32_to_dec(char *dst, uint32_t value, uint8_t width);
uint16_t app_cli_append_fixed2(char *dst, uint16_t pos, float value);
uint16_t app_cli_append_hex16(char *dst, uint16_t pos, uint16_t value);

/* "2026-09-06 12:30:45"，19 字符 */
uint16_t app_cli_append_time(char *dst, uint16_t pos, const board_rtc_time_t *time);

/* 日历时间 -> Unix 秒（1970 起），隐藏格式的时间戳字段用 */
uint32_t app_cli_time_to_unix(const board_rtc_time_t *time);

#endif /* APP_CLI_H */
