#ifndef APP_CLI_H
#define APP_CLI_H

#include <stdint.h>

#define APP_CLI_VERSION    "0.1.0-m4.3a"

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
 */
void app_cli_execute_line(const char *line);

#endif /* APP_CLI_H */
