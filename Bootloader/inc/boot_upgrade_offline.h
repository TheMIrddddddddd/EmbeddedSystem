#ifndef BOOT_UPGRADE_OFFLINE_H
#define BOOT_UPGRADE_OFFLINE_H

#include <stdint.h>

#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION = 0,
    BOOT_UPGRADE_OFFLINE_STATUS_INSTALL_STARTED,
    BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_DONE,
    BOOT_UPGRADE_OFFLINE_STATUS_CARD_NOT_READY,
    BOOT_UPGRADE_OFFLINE_STATUS_MOUNT_FAILED,
    BOOT_UPGRADE_OFFLINE_STATUS_FILE_NOT_FOUND,
    BOOT_UPGRADE_OFFLINE_STATUS_HEADER_INVALID,
    BOOT_UPGRADE_OFFLINE_STATUS_FILE_SIZE_INVALID,
    BOOT_UPGRADE_OFFLINE_STATUS_VERSION_REJECTED,
    BOOT_UPGRADE_OFFLINE_STATUS_STAGING_PREPARE_FAILED,
    BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_READ_FAILED,
    BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_CRC_INVALID,
    BOOT_UPGRADE_OFFLINE_STATUS_END_FAILED,
    BOOT_UPGRADE_OFFLINE_STATUS_INSTALL_START_FAILED,
    BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED
} boot_upgrade_offline_status_t;

/* 启动阶段检测并把 TF 包送入统一 Staging/INSTALL 流程。 */
boot_upgrade_offline_status_t boot_upgrade_offline_startup_process(void);

/* 启动阶段完成 .applied/.failed 文件清理，失败时保留 TF_OFFLINE 标志。 */
boot_upgrade_offline_status_t boot_upgrade_offline_cleanup_process(void);

extern volatile boot_upgrade_offline_status_t g_boot_upgrade_offline_status;
extern volatile uint32_t g_boot_upgrade_offline_last_fresult;
extern volatile uint32_t g_boot_upgrade_offline_file_size;
extern volatile uint32_t g_boot_upgrade_offline_file_version;
extern volatile uint32_t g_boot_upgrade_offline_file_crc32;
extern volatile uint32_t g_boot_upgrade_offline_read_offset;
extern volatile uint32_t g_boot_upgrade_offline_received_length;
extern volatile uint32_t g_boot_upgrade_offline_calculated_crc32;
extern volatile uint32_t g_boot_upgrade_offline_staging_status;
extern volatile uint32_t g_boot_upgrade_offline_end_status;
extern volatile uint32_t g_boot_upgrade_offline_install_status;
extern volatile uint8_t g_boot_upgrade_offline_mounted;

#endif /* BOOT_UPGRADE_OFFLINE_H */
