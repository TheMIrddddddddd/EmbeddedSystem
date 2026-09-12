#include <string.h>
#include "unity.h"
#include "storage_record_format.h"

static board_rtc_time_t test_time(void)
{
    board_rtc_time_t time = {2026U, 9U, 12U, 2U, 30U, 45U};
    return time;
}

/* 验证告警跨任务事件使用的秒级时间戳与 Unix 纪元一致。 */
static void test_time_to_unix(void)
{
    board_rtc_time_t time = {1970U, 1U, 1U, 0U, 0U, 0U};

    TEST_ASSERT_EQUAL_UINT32(0U, storage_record_time_to_unix(&time));
    time.year = 2000U;
    TEST_ASSERT_EQUAL_UINT32(946684800U, storage_record_time_to_unix(&time));
    time.year = 2026U;
    time.month = 1U;
    TEST_ASSERT_EQUAL_UINT32(1767225600U, storage_record_time_to_unix(&time));
}

/* 验证时间、采样 CSV 和 CRLF 结尾完全符合项目格式。 */
static void test_sample_text(void)
{
    board_rtc_time_t time = test_time();
    char buffer[STORAGE_RECORD_TEXT_MAX];
    uint16_t length = 0U;
    TEST_ASSERT_EQUAL_INT(1, storage_record_format_time(&time, buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_STRING("2026-09-12 02:30:45", buffer);
    TEST_ASSERT_EQUAL_UINT16(19U, length);
    TEST_ASSERT_EQUAL_INT(1, storage_record_format_sample(&time, 1.25f, 330.0f,
        buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_STRING("2026-09-12 02:30:45,1.25,330.00\r\n", buffer);
}

/* 验证告警通道标签、阈值和实际值格式。 */
static void test_alarm_text(void)
{
    board_rtc_time_t time = test_time();
    char buffer[STORAGE_RECORD_TEXT_MAX];
    uint16_t length = 0U;
    TEST_ASSERT_EQUAL_INT(1, storage_record_format_alarm(&time, 0U, 2.50f, 2.51f,
        buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_STRING("2026-09-12 02:30:45,CH0,2.50,2.51\r\n", buffer);
    TEST_ASSERT_EQUAL_INT(1, storage_record_format_alarm(&time, 1U, 500.0f, 0.0f,
        buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_STRING("2026-09-12 02:30:45,CH1,500.00,0.00\r\n", buffer);
}

/* 验证非法时间/通道/浮点、容量不足及输出原子性。 */
static void test_format_errors(void)
{
    board_rtc_time_t time = test_time();
    char buffer[STORAGE_RECORD_TEXT_MAX];
    char before[STORAGE_RECORD_TEXT_MAX];
    uint16_t length = 77U;
    memset(buffer, 0xA5, sizeof(buffer));
    memcpy(before, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(0, storage_record_format_sample(&time, -1.0f, 1.0f,
        buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_MEMORY(before, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT16(77U, length);
    TEST_ASSERT_EQUAL_INT(0, storage_record_format_alarm(&time, 2U, 1.0f, 1.0f,
        buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_INT(0, storage_record_format_time(&time, buffer, 10U, &length));
    TEST_ASSERT_EQUAL_INT(0, storage_record_format_time(NULL, buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_INT(0, storage_record_format_time(&time, buffer, sizeof(buffer), NULL));
}

void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_to_unix);
    RUN_TEST(test_sample_text);
    RUN_TEST(test_alarm_text);
    RUN_TEST(test_format_errors);
    return UNITY_END();
}
