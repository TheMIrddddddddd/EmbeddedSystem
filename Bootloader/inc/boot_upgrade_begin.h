#ifndef BOOT_UPGRADE_BEGIN_H
#define BOOT_UPGRADE_BEGIN_H

#include <stdint.h>

#include "board_internal_flash.h"
#include "boot_upgrade_meta.h"

typedef enum
{
    BOOT_UPGRADE_BEGIN_STATUS_OK = 0,
    BOOT_UPGRADE_BEGIN_STATUS_INVALID_ARGUMENT,
    BOOT_UPGRADE_BEGIN_STATUS_INVALID_HEADER,
    BOOT_UPGRADE_BEGIN_STATUS_STATE_NOT_ALLOWED,
    BOOT_UPGRADE_BEGIN_STATUS_META_WRITE_FAILED,
    BOOT_UPGRADE_BEGIN_STATUS_STAGING_PREPARE_FAILED
} boot_upgrade_begin_status_t;

boot_upgrade_begin_status_t boot_upgrade_begin_accept(
    const uint8_t *header_raw,
    uint32_t header_length
);

extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_begin_meta_status;
extern volatile board_internal_flash_status_t g_boot_upgrade_begin_staging_status;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_begin_cleanup_status;

#endif /* BOOT_UPGRADE_BEGIN_H */
