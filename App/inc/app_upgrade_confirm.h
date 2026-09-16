#ifndef APP_UPGRADE_CONFIRM_H
#define APP_UPGRADE_CONFIRM_H

#include <stdint.h>

#include "boot_upgrade_meta.h"

typedef enum
{
    APP_UPGRADE_CONFIRM_STATUS_OK = 0,
    APP_UPGRADE_CONFIRM_STATUS_NO_ACTION,
    APP_UPGRADE_CONFIRM_STATUS_INVALID_ARGUMENT,
    APP_UPGRADE_CONFIRM_STATUS_META_SELECT_FAILED,
    APP_UPGRADE_CONFIRM_STATUS_STATE_NOT_ALLOWED,
    APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED
} app_upgrade_confirm_status_t;

/* 在 StorageTask 上下文中提交 TRIAL_PENDING -> CONFIRMED。 */
app_upgrade_confirm_status_t app_upgrade_confirm_execute(void);

extern volatile app_upgrade_confirm_status_t g_app_upgrade_confirm_status;
extern volatile boot_upgrade_meta_select_status_t
    g_app_upgrade_confirm_select_status;
extern volatile boot_upgrade_meta_write_status_t
    g_app_upgrade_confirm_meta_status;
extern volatile boot_upgrade_meta_slot_t g_app_upgrade_confirm_written_slot;
extern volatile uint8_t g_app_upgrade_confirm_committed;
extern volatile uint8_t g_app_upgrade_confirm_trial_detected;

#endif /* APP_UPGRADE_CONFIRM_H */
