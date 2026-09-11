#include "app_config_ini.h"
#include "app_config.h"

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
