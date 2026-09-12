#include "storage_mount_policy.h"

#define STORAGE_WRITE_RETRY_COUNT 3U

static const uint32_t s_storage_write_retry_delay_ms[] =
{
    100U, 300U, 900U
};

/* 判断当前卡状态是否应清除 attempted 标志并允许下一轮重新挂载。 */
uint8_t storage_mount_policy_retry(uint8_t card_present,
                                   uint8_t mounted,
                                   uint8_t attempted)
{
    return ((card_present != 0U) && (mounted == 0U) &&
            (attempted != 0U)) ? 1U : 0U;
}

uint32_t storage_mount_policy_write_retry_delay_ms(uint8_t retry_index)
{
    if (retry_index >= STORAGE_WRITE_RETRY_COUNT)
    {
        return 0U;
    }

    return s_storage_write_retry_delay_ms[retry_index];
}

uint8_t storage_mount_policy_write_retry_exhausted(uint8_t retry_count)
{
    return (retry_count >= STORAGE_WRITE_RETRY_COUNT) ? 1U : 0U;
}
