#include "storage_mount_policy.h"

/* 判断当前卡状态是否应清除 attempted 标志并允许下一轮重新挂载。 */
uint8_t storage_mount_policy_retry(uint8_t card_present,
                                   uint8_t mounted,
                                   uint8_t attempted)
{
    return ((card_present != 0U) && (mounted == 0U) &&
            (attempted != 0U)) ? 1U : 0U;
}
