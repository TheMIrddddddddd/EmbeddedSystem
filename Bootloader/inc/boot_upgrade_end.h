#ifndef BOOT_UPGRADE_END_H
#define BOOT_UPGRADE_END_H

#include <stdint.h>

#include "board_internal_flash.h"
#include "boot_upgrade_meta.h"
#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_END_STATUS_OK = 0,
    BOOT_UPGRADE_END_STATUS_INVALID_ARGUMENT,
    BOOT_UPGRADE_END_STATUS_STATE_NOT_ALLOWED,
    BOOT_UPGRADE_END_STATUS_LENGTH_MISMATCH,
    BOOT_UPGRADE_END_STATUS_IMAGE_CRC_INVALID,
    BOOT_UPGRADE_END_STATUS_MSP_INVALID,
    BOOT_UPGRADE_END_STATUS_RESET_INVALID,
    BOOT_UPGRADE_END_STATUS_MANIFEST_ENCODE_FAILED,
    BOOT_UPGRADE_END_STATUS_MANIFEST_PROGRAM_FAILED,
    BOOT_UPGRADE_END_STATUS_MANIFEST_VERIFY_FAILED,
    BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED
} boot_upgrade_end_status_t;

boot_upgrade_end_status_t boot_upgrade_end_accept(void);

extern volatile boot_upgrade_end_status_t g_boot_upgrade_end_status;
extern volatile uint32_t g_boot_upgrade_end_calculated_crc32;
extern volatile uint32_t g_boot_upgrade_end_msp;
extern volatile uint32_t g_boot_upgrade_end_reset_handler;
extern volatile image_manifest_t g_boot_upgrade_end_manifest;
extern volatile board_internal_flash_status_t g_boot_upgrade_end_flash_status;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_end_meta_status;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_end_cleanup_status;

#endif /* BOOT_UPGRADE_END_H */
