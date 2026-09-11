#include "app_config_ini.h"
#include "app_config.h"

/* 解析 length 字节的非负十进制片段，语法为整数或整数加 1~6 位小数。
 * maximum 为允许的整数上限；用有界整数保存整数/小数部分，
 * 在 float 舍入前检查 0~maximum 范围，供变比和阈值入口共用。
 * 成功转换为 float 写入 out 并返回 1；失败返回 0 且 out 不变。
 * value 无需 NUL 终止；不选择通道，不修改运行配置或访问存储。
 */
static int app_config_ini_decimal_parse(const char *value, uint16_t length,
                                        uint16_t maximum, float *out)
{
    uint16_t index;
    uint16_t decimal_digits;
    uint32_t integer_part;
    uint32_t fraction_part;
    uint32_t scale;
    char current;

    if ((value == 0) || (out == 0) || (length == 0U) ||
        (length > APP_CONFIG_INI_LINE_MAX))
    {
        return 0;
    }

    index = 0U;
    integer_part = 0U;
    while ((index < length) && (value[index] != '.'))
    {
        current = value[index];
        if ((current < '0') || (current > '9'))
        {
            return 0;
        }
        /* 上轮 <= uint16_t maximum，本次最大 655359，uint32_t 不会溢出。 */
        integer_part = integer_part * 10U + (uint32_t)(current - '0');
        if (integer_part > maximum)
        {
            return 0;
        }
        index++;
    }
    if (index == 0U)
    {
        return 0;
    }

    fraction_part = 0U;
    scale = 1U;
    if (index < length)
    {
        index++;
        decimal_digits = 0U;
        while (index < length)
        {
            current = value[index];
            if ((current < '0') || (current > '9') || (decimal_digits >= 6U))
            {
                return 0;
            }
            /* 最多六位，fraction_part <= 999999，scale <= 1000000。 */
            fraction_part = fraction_part * 10U + (uint32_t)(current - '0');
            scale *= 10U;
            decimal_digits++;
            index++;
        }
        if (decimal_digits == 0U)
        {
            return 0;
        }
    }

    /* 整数达到上限后只能带全零小数，先于浮点转换拒绝微小越界。 */
    if ((integer_part == maximum) &&
        (fraction_part != 0U))
    {
        return 0;
    }

    *out = (float)integer_part + (float)fraction_part / (float)scale;
    return 1;
}

/* 解析 value 的 length 字节为 0~100 变比，沿用公共配置上限。
 * 成功返回 1 并写入 out，失败返回 0 且输出不变；不修改运行配置。
 */
int app_config_ini_ratio_parse(const char *value, uint16_t length, float *out)
{
    return app_config_ini_decimal_parse(value, length,
        (uint16_t)APP_CONFIG_RATIO_MAX, out);
}

/* 解析 value 的 length 字节为 0~500 阈值，沿用公共配置上限。
 * 成功返回 1 并写入 out，失败返回 0 且输出不变；不选通道或触发告警。
 */
int app_config_ini_limit_parse(const char *value, uint16_t length, float *out)
{
    return app_config_ini_decimal_parse(value, length,
        (uint16_t)APP_CONFIG_LIMIT_MAX, out);
}

/* 解析 length 字节的十进制告警模式片段，允许前导零。
 * 逐位限制累计值不超过 2，最后拒绝零值，仅接受 1/2。
 * 成功写入 out 并返回 1；任何失败返回 0，保持 out 原值。
 * 只产生候选模式，不修改运行配置，不访问 AlarmTask 或存储。
 */
int app_config_ini_alarm_mode_parse(const char *value, uint16_t length,
                                   uint8_t *out)
{
    uint16_t parsed;
    uint16_t index;
    char current;

    if ((value == 0) || (out == 0) || (length == 0U) ||
        (length > APP_CONFIG_INI_LINE_MAX))
    {
        return 0;
    }

    parsed = 0U;
    for (index = 0U; index < length; index++)
    {
        current = value[index];
        if ((current < '0') || (current > '9'))
        {
            return 0;
        }
        /* 上轮 parsed <= 2，本次累计最大 29，不会发生整数溢出。 */
        parsed = (uint16_t)(parsed * 10U + (uint16_t)(current - '0'));
        if (parsed > 2U)
        {
            return 0;
        }
    }

    if (parsed == 0U)
    {
        return 0;
    }

    *out = (uint8_t)parsed;
    return 1;
}

/* 解析 length 字节的十进制协议模式片段，允许前导零。
 * 逐位检查数字并限制累计值为 0/1，避免大整数窄化为合法模式。
 * 成功写出模式并返回 1；无效参数/格式/值返回 0，out 保持不变。
 * 此处只产生候选值，不切换硬件协议，也不检查设备 ID 联合约束。
 */
int app_config_ini_protocol_mode_parse(const char *value, uint16_t length,
                                      uint8_t *out)
{
    uint16_t parsed;
    uint16_t index;
    char current;

    if ((value == 0) || (out == 0) || (length == 0U) ||
        (length > APP_CONFIG_INI_LINE_MAX))
    {
        return 0;
    }

    parsed = 0U;
    for (index = 0U; index < length; index++)
    {
        current = value[index];
        if ((current < '0') || (current > '9'))
        {
            return 0;
        }
        /* 上轮 parsed <= 1，本次累计最大 19，不会发生整数溢出。 */
        parsed = (uint16_t)(parsed * 10U + (uint16_t)(current - '0'));
        if (parsed > 1U)
        {
            return 0;
        }
    }

    *out = (uint8_t)parsed;
    return 1;
}

/* 解析 length 字节的十进制周期片段，允许前导零，不接受符号或空白。
 * 用局部 uint16_t 累计并逐位限制上限，避免整数溢出或窄化回绕。
 * 仅数值 5/10/15 成功，成功写入 out 并返回 1；失败返回 0 且 out 不变。
 * 本函数仅处理值，不验证键名，不调用运行配置 setter 或访问存储。
 */
int app_config_ini_sample_period_parse(const char *value, uint16_t length,
                                      uint8_t *out)
{
    uint16_t parsed;
    uint16_t index;
    char current;

    if ((value == 0) || (out == 0) || (length == 0U) ||
        (length > APP_CONFIG_INI_LINE_MAX))
    {
        return 0;
    }

    parsed = 0U;
    for (index = 0U; index < length; index++)
    {
        current = value[index];
        if ((current < '0') || (current > '9'))
        {
            return 0;
        }
        /* 上轮 parsed <= 15，本次累计最大 159，uint16_t 不会溢出。 */
        parsed = (uint16_t)(parsed * 10U + (uint16_t)(current - '0'));
        if (parsed > 15U)
        {
            return 0;
        }
    }

    if ((parsed != 5U) && (parsed != 10U) && (parsed != 15U))
    {
        return 0;
    }

    *out = (uint8_t)parsed;
    return 1;
}

/* 解析已去除空白的 4 字节十六进制值；逐字节转换到局部变量。
 * value 无需 NUL 终止，length 必须为 4，out 接收 0001~FFFE。
 * 任一字符或范围非法返回 0；全部通过后才写出并返回 1。
 * 只解析值，不改运行配置；Modbus 地址范围留给完整配置校验。
 */
int app_config_ini_device_id_parse(const char *value, uint16_t length,
                                    uint16_t *out)
{
    uint16_t parsed;
    uint16_t digit;
    uint16_t index;
    char current;

    if ((value == 0) || (out == 0) || (length != 4U))
    {
        return 0;
    }

    parsed = 0U;
    for (index = 0U; index < 4U; index++)
    {
        current = value[index];
        if ((current >= '0') && (current <= '9'))
        {
            digit = (uint16_t)(current - '0');
        }
        else if ((current >= 'A') && (current <= 'F'))
        {
            digit = (uint16_t)(current - 'A' + 10);
        }
        else if ((current >= 'a') && (current <= 'f'))
        {
            digit = (uint16_t)(current - 'a' + 10);
        }
        else
        {
            return 0;
        }
        /* 固定最多四位，累计值不会超过 uint16_t 的 0xFFFF。 */
        parsed = (uint16_t)((parsed << 4U) | digit);
    }

    if ((parsed < APP_CONFIG_DEVICE_ID_MIN) ||
        (parsed > APP_CONFIG_DEVICE_ID_MAX))
    {
        return 0;
    }

    *out = parsed;
    return 1;
}

/* 判断一个字节是否为契约允许的行内空白；空格/Tab 返回 1，其余返回 0。 */
static int app_config_ini_space(char value)
{
    return ((value == ' ') || (value == '\t')) ? 1 : 0;
}

/* 将 length 字节的单行拆为键值位置，输入不要求 NUL 终止且不会被修改。
 * 先检查长度及控制字节，再跳过空白/注释并查找唯一等号。
 * 仅完整拆分成功时更新 out；返回 PAIR、SKIP 或 ERROR。
 * 字段合法性由下一层处理，本函数不访问运行配置、RTOS 或存储。
 */
app_config_ini_line_status_t app_config_ini_line_parse(
    const char *line, uint16_t length, app_config_ini_pair_t *out)
{
    uint16_t begin;
    uint16_t end;
    uint16_t index;
    uint16_t equal;
    app_config_ini_pair_t pair;

    if ((line == 0) || (out == 0) || (length > APP_CONFIG_INI_LINE_MAX))
    {
        return APP_CONFIG_INI_LINE_ERROR;
    }

    for (index = 0U; index < length; index++)
    {
        if ((line[index] == '\0') || (line[index] == '\r') ||
            (line[index] == '\n'))
        {
            return APP_CONFIG_INI_LINE_ERROR;
        }
    }

    begin = 0U;
    end = length;
    while ((begin < end) && app_config_ini_space(line[begin]))
    {
        begin++;
    }
    if ((begin == end) || (line[begin] == '#'))
    {
        return APP_CONFIG_INI_LINE_SKIP;
    }
    while ((end > begin) && app_config_ini_space(line[end - 1U]))
    {
        end--;
    }

    equal = end;
    for (index = begin; index < end; index++)
    {
        if (line[index] == '=')
        {
            if (equal != end)
            {
                return APP_CONFIG_INI_LINE_ERROR;
            }
            equal = index;
        }
    }
    if (equal == end)
    {
        return APP_CONFIG_INI_LINE_ERROR;
    }

    index = equal;
    while ((index > begin) && app_config_ini_space(line[index - 1U]))
    {
        index--;
    }
    pair.key_offset = begin;
    pair.key_length = (uint16_t)(index - begin);

    index = (uint16_t)(equal + 1U);
    while ((index < end) && app_config_ini_space(line[index]))
    {
        index++;
    }
    pair.value_offset = index;
    pair.value_length = (uint16_t)(end - index);
    if ((pair.key_length == 0U) || (pair.value_length == 0U))
    {
        return APP_CONFIG_INI_LINE_ERROR;
    }

    *out = pair;
    return APP_CONFIG_INI_LINE_PAIR;
}
