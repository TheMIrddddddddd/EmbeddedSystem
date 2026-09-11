#include <string.h>
#include "unity.h"
#include "app_config_ini.h"

/* Unity 前置钩子：本测试无全局运行配置或硬件依赖。 */
void setUp(void) {}

/* Unity 后置钩子：被测函数无资源需要释放。 */
void tearDown(void) {}

/* 验证非 NUL 终止输入可拆分，空白被移除且输入不被改写。 */
static void test_pair_and_whitespace(void)
{
    const char line[] = " \tdevice_id \t= 0001\t ";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(2U, pair.key_offset);
    TEST_ASSERT_EQUAL_UINT16(9U, pair.key_length);
    TEST_ASSERT_EQUAL_UINT16(15U, pair.value_offset);
    TEST_ASSERT_EQUAL_UINT16(4U, pair.value_length);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证空行/注释跳过，失败或跳过时不发布半成品输出。 */
static void test_skip_and_errors_preserve_output(void)
{
    const char *lines[] = {"", " \t", "  # comment", "key", "=1",
        "key= ", "key=1=2", "key=1\n", "key=1\r"};
    unsigned i;
    app_config_ini_pair_t pair;
    app_config_ini_pair_t before;
    memset(&pair, 0x5A, sizeof(pair));
    memcpy(&before, &pair, sizeof(pair));
    for (i = 0U; i < sizeof(lines) / sizeof(lines[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(i < 3U ? APP_CONFIG_INI_LINE_SKIP :
            APP_CONFIG_INI_LINE_ERROR,
            app_config_ini_line_parse(lines[i], (uint16_t)strlen(lines[i]), &pair));
        TEST_ASSERT_EQUAL_MEMORY(&before, &pair, sizeof(pair));
    }
}

/* 验证单行 128/129 字节界限、嵌入 NUL 和无效指针。 */
static void test_boundaries_and_arguments(void)
{
    char line[129];
    app_config_ini_pair_t pair;
    memset(line, '1', sizeof(line));
    line[0] = 'k';
    line[1] = '=';
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, 128U, &pair));
    TEST_ASSERT_EQUAL_UINT16(126U, pair.value_length);
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_ERROR,
        app_config_ini_line_parse(line, 129U, &pair));
    line[5] = '\0';
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_ERROR,
        app_config_ini_line_parse(line, 10U, &pair));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_ERROR,
        app_config_ini_line_parse(NULL, 0U, &pair));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_ERROR,
        app_config_ini_line_parse("k=1", 3U, NULL));
}

/* 明确本层只拆分：未知键与非法数值保留原文供下一层拒绝。 */
static void test_semantics_are_deferred(void)
{
    app_config_ini_pair_t pair;
    const char line[] = "unknown=NaN";
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(7U, pair.key_length);
    TEST_ASSERT_EQUAL_UINT16(3U, pair.value_length);
}

/* 验证十六进制转换、大小写及上下界；本层允许超过 Modbus 上限的 ID。 */
static void test_device_id_valid_values(void)
{
    const char *values[] = {"0001", "0010", "00F7", "00f8", "aBcD", "FFFE"};
    const uint16_t expected[] = {1U, 16U, 247U, 248U, 0xABCDU, 0xFFFEU};
    uint16_t out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(1,
            app_config_ini_device_id_parse(values[i], 4U, &out));
        TEST_ASSERT_EQUAL_UINT16(expected[i], out);
    }
}

/* 验证宽度、字符及值域错误均失败，且中途错误不覆盖已有输出。 */
static void test_device_id_invalid_values_preserve_output(void)
{
    const char *values[] = {"", "1", "001", "00001", "0x01", "0000", "FFFF",
        "+001", "-001", " 001", "001 ", "00G1", "123g", "1.00", "01\t1", "001\n"};
    uint16_t out = 0x4321U;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_device_id_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT16(0x4321U, out);
    }
}

/* 验证精确 4 字节数组无需结束符，嵌入 NUL 与空指针被拒绝。 */
static void test_device_id_buffer_arguments(void)
{
    const char raw[4] = {'0', '1', '2', '3'};
    const char embedded_nul[4] = {'0', '1', '\0', '3'};
    uint16_t out = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_device_id_parse(raw, 4U, &out));
    TEST_ASSERT_EQUAL_UINT16(0x0123U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_device_id_parse(embedded_nul, 4U, &out));
    TEST_ASSERT_EQUAL_UINT16(0x0123U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_device_id_parse(NULL, 4U, &out));
    TEST_ASSERT_EQUAL_UINT16(0x0123U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_device_id_parse(raw, 4U, NULL));
}

/* 串联单行拆分与 ID 解析，确认值片段可以直接消费且原行保持不变。 */
static void test_device_id_from_line(void)
{
    const char line[] = " \tdevice_id = 00f7 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    uint16_t out = 0U;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(9U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("device_id", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_device_id_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_UINT16(247U, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证三个周期档位与前导零，所有输入按十进制解释。 */
static void test_sample_period_valid_values(void)
{
    const char *values[] = {"5", "10", "15", "005", "010", "00015"};
    const uint8_t expected[] = {5U, 10U, 15U, 5U, 10U, 15U};
    uint8_t out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(1, app_config_ini_sample_period_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(expected[i], out);
    }
}

/* 验证非法档位、格式及会窄化回绕的整数均失败且不改输出。 */
static void test_sample_period_invalid_preserves_output(void)
{
    const char *values[] = {"", "0", "4", "6", "14", "16", "261", "65541",
        "4294967301", "+5", "-5", "5.0", "0x05", "5e0", " 5", "5 ", "1\t0", "15x"};
    uint8_t out = 10U;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_sample_period_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(10U, out);
    }
}

/* 验证非终止片段、长度上限、嵌入 NUL 与无效参数，失败输出保持。 */
static void test_sample_period_buffer_arguments(void)
{
    const char raw[2] = {'1', '5'};
    const char nul[2] = {'5', '\0'};
    char long_value[129];
    uint8_t out = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_sample_period_parse(raw, 2U, &out));
    TEST_ASSERT_EQUAL_UINT8(15U, out);
    memset(long_value, '0', sizeof(long_value));
    long_value[127] = '5';
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_sample_period_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_UINT8(5U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_sample_period_parse(long_value, 129U, &out));
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_sample_period_parse(nul, 2U, &out));
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_sample_period_parse(NULL, 1U, &out));
    TEST_ASSERT_EQUAL_UINT8(5U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_sample_period_parse(raw, 2U, NULL));
}

/* 串联单行拆分与周期转换，确认键名后取值，原始行不被修改。 */
static void test_sample_period_from_line(void)
{
    const char line[] = " \tsample_period = 010 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    uint8_t out = 0U;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(13U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("sample_period", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_sample_period_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_UINT8(10U, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证协议模式 0/1 及前导零，零值必须作为有效配置接受。 */
static void test_protocol_mode_valid_values(void)
{
    const char *values[] = {"0", "1", "00", "01", "0001"};
    const uint8_t expected[] = {0U, 1U, 0U, 1U, 1U};
    uint8_t out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        out = 0xA5U;
        TEST_ASSERT_EQUAL_INT(1, app_config_ini_protocol_mode_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(expected[i], out);
    }
}

/* 验证非法模式、尾随字符及大整数被拒绝，失败保持原输出。 */
static void test_protocol_mode_invalid_preserves_output(void)
{
    const char *values[] = {"", "2", "10", "11", "256", "257", "65537",
        "4294967297", "+1", "-0", "1.0", "0x01", "1e0", " 1", "1 ", "0\t1", "1x"};
    uint8_t out = 0xA5U;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_protocol_mode_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(0xA5U, out);
    }
}

/* 验证非终止输入、128/129 字节界限、嵌入 NUL 及空指针。 */
static void test_protocol_mode_buffer_arguments(void)
{
    const char raw[1] = {'1'};
    const char nul[2] = {'1', '\0'};
    char long_value[129];
    uint8_t out = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_protocol_mode_parse(raw, 1U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    memset(long_value, '0', sizeof(long_value));
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_protocol_mode_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_UINT8(0U, out);
    long_value[127] = '1';
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_protocol_mode_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_protocol_mode_parse(long_value, 129U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_protocol_mode_parse(nul, 2U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_protocol_mode_parse(NULL, 1U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_protocol_mode_parse(raw, 1U, NULL));
}

/* 验证单行拆分后确认键名并解析协议值，原始文本保持不变。 */
static void test_protocol_mode_from_line(void)
{
    const char line[] = " \tprotocol_mode = 01 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    uint8_t out = 0U;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(13U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("protocol_mode", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_protocol_mode_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证告警模式 1/2 和前导零，成功输出对应模式数值。 */
static void test_alarm_mode_valid_values(void)
{
    const char *values[] = {"1", "2", "01", "02", "0002"};
    const uint8_t expected[] = {1U, 2U, 1U, 2U, 2U};
    uint8_t out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        out = 0xA5U;
        TEST_ASSERT_EQUAL_INT(1, app_config_ini_alarm_mode_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(expected[i], out);
    }
}

/* 验证零值、非法模式、括号说明及溢出输入均失败且保持输出。 */
static void test_alarm_mode_invalid_preserves_output(void)
{
    const char *values[] = {"", "0", "00", "3", "10", "12", "257", "258",
        "65538", "4294967298", "+2", "-1", "2.0", "0x02", "2e0", " 2",
        "2 ", "0\t2", "2x", "2(01 active / 02 passive)", "2 # comment"};
    uint8_t out = 0xA5U;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_UINT8(0xA5U, out);
    }
}

/* 验证非终止片段、长度界限、全零、嵌入 NUL 与空指针。 */
static void test_alarm_mode_buffer_arguments(void)
{
    const char raw[1] = {'2'};
    const char nul[2] = {'2', '\0'};
    char long_value[129];
    uint8_t out = 0U;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_alarm_mode_parse(raw, 1U, &out));
    TEST_ASSERT_EQUAL_UINT8(2U, out);
    memset(long_value, '0', sizeof(long_value));
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_UINT8(2U, out);
    long_value[127] = '1';
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_alarm_mode_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(long_value, 129U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(nul, 2U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(NULL, 1U, &out));
    TEST_ASSERT_EQUAL_UINT8(1U, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_alarm_mode_parse(raw, 1U, NULL));
}

/* 串联单行拆分与告警模式解析，确认键名后取值且原始行不变。 */
static void test_alarm_mode_from_line(void)
{
    const char line[] = " \talarm_mode = 02 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    uint8_t out = 0U;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(10U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("alarm_mode", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_alarm_mode_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_UINT8(2U, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证整数、小数、前导零及 0/100 边界，按 float32 精度比较结果。 */
static void test_ratio_valid_values(void)
{
    const char *values[] = {"0", "100", "1", "1.25", "0002.50",
        "0.000001", "12.345678", "99.999999", "100.000000", "000.000000"};
    const float expected[] = {0.0f, 100.0f, 1.0f, 1.25f, 2.5f,
        0.000001f, 12.345678f, 99.999999f, 100.0f, 0.0f};
    float out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        out = -1.0f;
        TEST_ASSERT_EQUAL_INT(1, app_config_ini_ratio_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_FLOAT(expected[i], out);
    }
}

/* 验证越界值在转 float 前被拒绝，语法错误及超长小数保持原输出。 */
static void test_ratio_invalid_preserves_output(void)
{
    const char *values[] = {"", "101", "100.000001", "100.1", "4294967296",
        "-0", "-1", "+1", "1e0", "NaN", "Inf", "inf", ".5", "1.", ".",
        "1.2.3", "1,5", "0x10", "1.0000000", "0.0000001", " 1", "1 ",
        "1\t2", "1\r", "1\n", "1.25x", "1.0 # comment"};
    float out = 7.25f;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_ratio_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_FLOAT(7.25f, out);
    }
}

/* 验证非终止片段、128/129 字节长度、嵌入 NUL 及无效指针。 */
static void test_ratio_buffer_arguments(void)
{
    const char raw[4] = {'1', '.', '2', '5'};
    const char nul[4] = {'1', '.', '\0', '5'};
    char long_value[129];
    float out = 0.0f;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_ratio_parse(raw, 4U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.25f, out);
    memset(long_value, '0', sizeof(long_value));
    long_value[127] = '1';
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_ratio_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_ratio_parse(long_value, 129U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_ratio_parse(nul, 4U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_ratio_parse(NULL, 1U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_ratio_parse(raw, 4U, NULL));
}

/* 串联单行拆分和单值变比解析，确认键名后取值，原始文本保持不变。 */
static void test_ratio_from_line(void)
{
    const char line[] = " \tch0_ratio = 002.50 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    float out = 0.0f;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(9U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("ch0_ratio", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_ratio_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* 验证 0/500 边界、超过变比上限的合法阈值及六位小数。 */
static void test_limit_valid_values(void)
{
    const char *values[] = {"0", "500", "100.000001", "125.5", "002.50",
        "0.000001", "123.456789", "499.999999", "500.000000", "000.000000"};
    const float expected[] = {0.0f, 500.0f, 100.000001f, 125.5f, 2.5f,
        0.000001f, 123.456789f, 499.999999f, 500.0f, 0.0f};
    float out;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        out = -1.0f;
        TEST_ASSERT_EQUAL_INT(1, app_config_ini_limit_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_FLOAT(expected[i], out);
    }
}

/* 验证微小越界先于浮点转换被拒绝，格式及精度错误保持旧值。 */
static void test_limit_invalid_preserves_output(void)
{
    const char *values[] = {"", "501", "500.000001", "500.1", "4294967296",
        "-0", "-1", "+1", "1e0", "NaN", "Inf", "inf", ".5", "1.", ".",
        "1.2.3", "1,5", "0x10", "1.0000000", "0.0000001", " 1", "1 ",
        "1\t2", "1\r", "1\n", "1.25x", "1.0 # comment"};
    float out = 123.25f;
    unsigned i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(0, app_config_ini_limit_parse(
            values[i], (uint16_t)strlen(values[i]), &out));
        TEST_ASSERT_EQUAL_FLOAT(123.25f, out);
    }
}

/* 验证非终止片段、128/129 字节界限、嵌入 NUL 和空指针。 */
static void test_limit_buffer_arguments(void)
{
    const char raw[5] = {'2', '5', '0', '.', '5'};
    const char nul[5] = {'2', '5', '0', '\0', '5'};
    char long_value[129];
    float out = 0.0f;
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_limit_parse(raw, 5U, &out));
    TEST_ASSERT_EQUAL_FLOAT(250.5f, out);
    memset(long_value, '0', sizeof(long_value));
    long_value[127] = '1';
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_limit_parse(long_value, 128U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_limit_parse(long_value, 129U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_limit_parse(nul, 5U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_limit_parse(NULL, 1U, &out));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out);
    TEST_ASSERT_EQUAL_INT(0, app_config_ini_limit_parse(raw, 5U, NULL));
}

/* 串联单行拆分和阈值解析，确认键名后取值且原始行不变。 */
static void test_limit_from_line(void)
{
    const char line[] = " \tch1_limit = 0250.50 \t";
    char before[sizeof(line)];
    app_config_ini_pair_t pair;
    float out = 0.0f;
    memcpy(before, line, sizeof(line));
    TEST_ASSERT_EQUAL_INT(APP_CONFIG_INI_LINE_PAIR,
        app_config_ini_line_parse(line, sizeof(line) - 1U, &pair));
    TEST_ASSERT_EQUAL_UINT16(9U, pair.key_length);
    TEST_ASSERT_EQUAL_MEMORY("ch1_limit", line + pair.key_offset, pair.key_length);
    TEST_ASSERT_EQUAL_INT(1, app_config_ini_limit_parse(
        line + pair.value_offset, pair.value_length, &out));
    TEST_ASSERT_EQUAL_FLOAT(250.5f, out);
    TEST_ASSERT_EQUAL_MEMORY(before, line, sizeof(line));
}

/* PC 测试入口：返回 Unity 失败数供命令行判断。 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pair_and_whitespace);
    RUN_TEST(test_skip_and_errors_preserve_output);
    RUN_TEST(test_boundaries_and_arguments);
    RUN_TEST(test_semantics_are_deferred);
    RUN_TEST(test_device_id_valid_values);
    RUN_TEST(test_device_id_invalid_values_preserve_output);
    RUN_TEST(test_device_id_buffer_arguments);
    RUN_TEST(test_device_id_from_line);
    RUN_TEST(test_sample_period_valid_values);
    RUN_TEST(test_sample_period_invalid_preserves_output);
    RUN_TEST(test_sample_period_buffer_arguments);
    RUN_TEST(test_sample_period_from_line);
    RUN_TEST(test_protocol_mode_valid_values);
    RUN_TEST(test_protocol_mode_invalid_preserves_output);
    RUN_TEST(test_protocol_mode_buffer_arguments);
    RUN_TEST(test_protocol_mode_from_line);
    RUN_TEST(test_alarm_mode_valid_values);
    RUN_TEST(test_alarm_mode_invalid_preserves_output);
    RUN_TEST(test_alarm_mode_buffer_arguments);
    RUN_TEST(test_alarm_mode_from_line);
    RUN_TEST(test_ratio_valid_values);
    RUN_TEST(test_ratio_invalid_preserves_output);
    RUN_TEST(test_ratio_buffer_arguments);
    RUN_TEST(test_ratio_from_line);
    RUN_TEST(test_limit_valid_values);
    RUN_TEST(test_limit_invalid_preserves_output);
    RUN_TEST(test_limit_buffer_arguments);
    RUN_TEST(test_limit_from_line);
    return UNITY_END();
}
