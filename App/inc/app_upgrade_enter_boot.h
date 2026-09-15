#ifndef APP_UPGRADE_ENTER_BOOT_H
#define APP_UPGRADE_ENTER_BOOT_H

#include <stdint.h>

#include "boot_upgrade_meta.h"

typedef enum
{
    APP_UPGRADE_ENTER_BOOT_STATUS_OK = 0,
    APP_UPGRADE_ENTER_BOOT_STATUS_INVALID_ARGUMENT,
    APP_UPGRADE_ENTER_BOOT_STATUS_META_SELECT_FAILED,
    APP_UPGRADE_ENTER_BOOT_STATUS_TF_CLEANUP_PENDING,
    APP_UPGRADE_ENTER_BOOT_STATUS_STATE_NOT_ALLOWED,
    APP_UPGRADE_ENTER_BOOT_STATUS_META_UPDATE_FAILED
} app_upgrade_enter_boot_status_t;

/* 在 StorageTask 上下文中持久化 ENTER_BOOT 请求。 */
app_upgrade_enter_boot_status_t app_upgrade_enter_boot_execute(void);

extern volatile app_upgrade_enter_boot_status_t
    g_app_upgrade_enter_boot_status;
extern volatile boot_upgrade_meta_select_status_t
    g_app_upgrade_enter_boot_select_status;
extern volatile boot_upgrade_meta_write_status_t
    g_app_upgrade_enter_boot_meta_status;
extern volatile boot_upgrade_meta_slot_t
    g_app_upgrade_enter_boot_written_slot;
extern volatile uint8_t g_app_upgrade_enter_boot_committed;

#endif /* APP_UPGRADE_ENTER_BOOT_H */
