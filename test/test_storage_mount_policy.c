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

void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_failed_mount_retries_while_card_present);
    RUN_TEST(test_retry_guard_conditions);
    return UNITY_END();
}
