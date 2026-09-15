#ifndef BOOT_UPGRADE_TRIAL_H
#define BOOT_UPGRADE_TRIAL_H

#include <stdint.h>

#include "boot_upgrade_meta.h"
#include "common_reset_contract.h"

typedef enum
{
    BOOT_UPGRADE_TRIAL_STATUS_NO_ACTION = 0,
    BOOT_UPGRADE_TRIAL_STATUS_APP_ALLOWED,
    BOOT_UPGRADE_TRIAL_STATUS_FAILURE_RECORDED,
    BOOT_UPGRADE_TRIAL_STATUS_ROLLBACK_REQUIRED,
    BOOT_UPGRADE_TRIAL_STATUS_CONFIRMED_COMMITTED,
    BOOT_UPGRADE_TRIAL_STATUS_META_UPDATE_FAILED,
    BOOT_UPGRADE_TRIAL_STATUS_INVALID_STATE
} boot_upgrade_trial_status_t;

/* 启动早期消费 HardFault 标记，并处理 TRIAL_PENDING 失败计数。 */
boot_upgrade_trial_status_t boot_upgrade_trial_startup_process(
    common_reset_reason_t reset_reason);

/* 在安全升级循环中提交 CONFIRMED -> IDLE，或处理确认后的复位。 */
boot_upgrade_trial_status_t boot_upgrade_trial_process(void);

extern volatile boot_upgrade_trial_status_t g_boot_upgrade_trial_status;
extern volatile common_reset_reason_t g_boot_upgrade_trial_reset_reason;
extern volatile uint32_t g_boot_upgrade_trial_crash_marker;
extern volatile uint8_t g_boot_upgrade_trial_crash_marker_valid;
extern volatile uint8_t g_boot_upgrade_trial_failure_count_before;
extern volatile uint8_t g_boot_upgrade_trial_failure_count_after;
extern volatile boot_upgrade_meta_write_status_t
    g_boot_upgrade_trial_meta_status;
extern volatile boot_upgrade_meta_slot_t g_boot_upgrade_trial_written_slot;

#endif /* BOOT_UPGRADE_TRIAL_H */
