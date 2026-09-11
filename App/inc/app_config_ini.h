#ifndef APP_CONFIG_INI_H
#define APP_CONFIG_INI_H

#include <stdint.h>

#define APP_CONFIG_INI_LINE_MAX 128U

typedef enum
{
    APP_CONFIG_INI_LINE_ERROR = 0,
    APP_CONFIG_INI_LINE_SKIP,
    APP_CONFIG_INI_LINE_PAIR
} app_config_ini_line_status_t;

/* 当前输入行内的字节位置，不保存缓冲区指针，不用于跨任务传递。 */
typedef struct
{
    uint16_t key_offset;
    uint16_t key_length;
    uint16_t value_offset;
    uint16_t value_length;
} app_config_ini_pair_t;

/* 拆分不含 CR/LF 的单行（无需 NUL 终止）；length 为实际字节数。
 * PAIR 时写入去除空白后的键值位置；SKIP/ERROR 时保持 out 不变。
 * 本层仅检查行结构，键名、数值和 UTF-8 完整校验由后续层负责。
 * line/out 必须非 NULL，输入内存由调用者保持有效且不与 out 重叠。
 */
app_config_ini_line_status_t app_config_ini_line_parse(
    const char *line, uint16_t length, app_config_ini_pair_t *out);

#endif /* APP_CONFIG_INI_H */
