#include "app_config_ini.h"

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
