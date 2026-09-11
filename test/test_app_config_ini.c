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

/* PC 测试入口：返回 Unity 失败数供命令行判断。 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pair_and_whitespace);
    RUN_TEST(test_skip_and_errors_preserve_output);
    RUN_TEST(test_boundaries_and_arguments);
    RUN_TEST(test_semantics_are_deferred);
    return UNITY_END();
}
