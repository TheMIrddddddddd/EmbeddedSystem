#include "unity.h"
#include "storage_mount_policy.h"

/* 验证卡在位、挂载失败且已尝试过时允许下一轮重试。 */
static void test_failed_mount_retries_while_card_present(void)
{
    TEST_ASSERT_EQUAL_UINT8(1U, storage_mount_policy_retry(1U, 0U, 1U));
}

/* 验证已挂载、无卡或尚未尝试时不触发重试清除。 */
static void test_retry_guard_conditions(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, storage_mount_policy_retry(1U, 1U, 1U));
    TEST_ASSERT_EQUAL_UINT8(0U, storage_mount_policy_retry(0U, 0U, 1U));
    TEST_ASSERT_EQUAL_UINT8(0U, storage_mount_policy_retry(1U, 0U, 0U));
}

/* 验证 TF 写失败的三档退避时序，索引 0/1/2 对应 100/300/900ms。 */
static void test_write_retry_backoff_schedule(void)
{
    TEST_ASSERT_EQUAL_UINT32(100U,
                             storage_mount_policy_write_retry_delay_ms(0U));
    TEST_ASSERT_EQUAL_UINT32(300U,
                             storage_mount_policy_write_retry_delay_ms(1U));
    TEST_ASSERT_EQUAL_UINT32(900U,
                             storage_mount_policy_write_retry_delay_ms(2U));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             storage_mount_policy_write_retry_delay_ms(3U));
}

/* 验证三次重试耗尽后才进入降级，前两次仍允许继续恢复。 */
static void test_write_retry_degrades_after_three_retries(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U,
                            storage_mount_policy_write_retry_exhausted(0U));
    TEST_ASSERT_EQUAL_UINT8(0U,
                            storage_mount_policy_write_retry_exhausted(2U));
    TEST_ASSERT_EQUAL_UINT8(1U,
                            storage_mount_policy_write_retry_exhausted(3U));
    TEST_ASSERT_EQUAL_UINT8(1U,
                            storage_mount_policy_write_retry_exhausted(4U));
}

void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_failed_mount_retries_while_card_present);
    RUN_TEST(test_retry_guard_conditions);
    RUN_TEST(test_write_retry_backoff_schedule);
    RUN_TEST(test_write_retry_degrades_after_three_retries);
    return UNITY_END();
}
