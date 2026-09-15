#ifndef BOOT_UPGRADE_INSTALL_H
#define BOOT_UPGRADE_INSTALL_H

#include <stdint.h>

#include "board_internal_flash.h"
#include "boot_upgrade_meta.h"
#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_INSTALL_STATUS_OK = 0,
    BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION,
    BOOT_UPGRADE_INSTALL_STATUS_INVALID_ARGUMENT,
    BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED,
    BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID,
    BOOT_UPGRADE_INSTALL_STATUS_ACTIVE_INVALID,
    BOOT_UPGRADE_INSTALL_STATUS_BACKUP_ERASE_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_BACKUP_COPY_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_BACKUP_VERIFY_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_APP_ERASE_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_APP_COPY_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_APP_VERIFY_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED,
    BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED
} boot_upgrade_install_status_t;

/* 接受 INSTALL，并只持久化 INSTALLING/BACKUP_START。 */
boot_upgrade_install_status_t boot_upgrade_install_start(void);

/* 在 Boot 安全升级主循环中推进一个安装子阶段。 */
boot_upgrade_install_status_t boot_upgrade_install_process(void);

/* 供试运行失败后的 ROLLBACK_REQUIRED 状态复用 Backup 回滚。 */
boot_upgrade_install_status_t boot_upgrade_install_rollback_now(void);

extern volatile boot_upgrade_install_status_t g_boot_upgrade_install_status;
extern volatile install_stage_t g_boot_upgrade_install_stage;
extern volatile board_internal_flash_status_t g_boot_upgrade_install_flash_status;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_install_meta_status;
extern volatile boot_upgrade_meta_slot_t g_boot_upgrade_install_written_slot;
extern volatile uint32_t g_boot_upgrade_install_page_index;
extern volatile uint32_t g_boot_upgrade_install_completed_pages;
extern volatile uint32_t g_boot_upgrade_install_copy_offset;
extern volatile uint32_t g_boot_upgrade_install_copy_length;
extern volatile uint32_t g_boot_upgrade_install_calculated_crc32;
extern volatile uint32_t g_boot_upgrade_install_msp;
extern volatile uint32_t g_boot_upgrade_install_reset_handler;
extern volatile uint8_t g_boot_upgrade_install_started;

#endif /* BOOT_UPGRADE_INSTALL_H */
