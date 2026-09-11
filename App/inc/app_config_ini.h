#ifndef APP_CONFIG_INI_H
#define APP_CONFIG_INI_H

#include <stdint.h>
#include "app_config.h"

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

/* 将已去除空白的 value 片段解析为设备 ID：恰好 4 位十六进制，
 * 范围 0001~FFFE，允许大小写，不要求 NUL 终止；不验证键名。
 * 成功返回 1 并写入 out；失败返回 0 且保持 out 不变。
 * value/out 必须非 NULL。协议相关的 ID 约束由整组配置校验负责。
 */
int app_config_ini_device_id_parse(const char *value, uint16_t length,
                                    uint16_t *out);

/* 解析已去除空白的十进制周期值，仅接受数值 5/10/15，允许前导零。
 * value 无需 NUL 终止，length 为实际字节数（1~LINE_MAX）。
 * 成功返回 1 并写入 out；失败返回 0 且 out 不变；两指针须非 NULL。
 */
int app_config_ini_sample_period_parse(const char *value, uint16_t length,
                                      uint8_t *out);

/* 解析已去除空白的十进制协议模式，0=自定义协议，1=Modbus RTU。
 * 允许前导零，value 无需 NUL 终止，length 为 1~LINE_MAX 字节。
 * 成功返回 1 并写入 out；失败返回 0 且 out 不变；指针须非 NULL。
 * 此函数不切换协议，设备 ID 与模式的联合约束由完整配置校验负责。
 */
int app_config_ini_protocol_mode_parse(const char *value, uint16_t length,
                                      uint8_t *out);

/* 解析已去除空白的十进制告警模式，1=主动上报，2=被动存储。
 * 允许前导零，value 无需 NUL 终止，length 为 1~LINE_MAX 字节。
 * 成功返回 1 并写入 out；失败返回 0 且 out 不变；指针须非 NULL。
 * 此函数只产生候选值，不改变 AlarmTask 行为或运行配置。
 */
int app_config_ini_alarm_mode_parse(const char *value, uint16_t length,
                                   uint8_t *out);

/* 解析已去除空白的变比值：十进制整数或带 1~6 位小数，范围 0~100。
 * 允许前导零，不接受符号、指数或省略整数/小数位的写法。
 * value 无需 NUL 终止，length 为 1~LINE_MAX 字节，指针须非 NULL。
 * 先精确检查十进制值域，再转换为 float；成功返回 1 并写 out，
 * 失败返回 0 且 out 不变。本函数不选通道、不修改运行配置。
 */
int app_config_ini_ratio_parse(const char *value, uint16_t length, float *out);

/* 解析已去除空白的阈值：十进制整数或带 1~6 位小数，范围 0~500。
 * 允许前导零，不接受符号、指数或省略整数/小数位的写法。
 * value 无需 NUL 终止，length 为 1~LINE_MAX 字节，指针须非 NULL。
 * 成功返回 1 并写出 float；失败返回 0 且 out 不变。
 * 此函数不选通道、不修改运行配置；十进制值域在浮点转换前检查。
 */
int app_config_ini_limit_parse(const char *value, uint16_t length, float *out);

/* 解析一行并将一个字段写入调用者独占的临时配置 candidate。
 * line 为不含 CR/LF 的 length 字节（无需 NUL 终止），不与 candidate 重叠。
 * PAIR 表示对应字段已更新；SKIP/ERROR 时 candidate 完全不变。
 * 仅接受契约的 8 个键，大小写敏感；不调用 app_config_apply/setter。
 * 不检查重复/缺失键及跨字段约束，调用者须在文件解析完毕后整组校验。
 */
app_config_ini_line_status_t app_config_ini_line_apply(
    const char *line, uint16_t length, app_config_t *candidate);

#endif /* APP_CONFIG_INI_H */
