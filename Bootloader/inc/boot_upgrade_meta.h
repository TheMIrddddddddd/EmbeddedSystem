#ifndef BOOT_UPGRADE_META_H
#define BOOT_UPGRADE_META_H

#include "board_spi_flash.h"
#include "common_flash_layout.h"
#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_META_SLOT_A = 0,
    BOOT_UPGRADE_META_SLOT_B = 1,
    BOOT_UPGRADE_META_SLOT_NONE = 0xFF
} boot_upgrade_meta_slot_t;

typedef enum
{
    BOOT_UPGRADE_META_SLOT_INVALID_ARGUMENT = 0,
    BOOT_UPGRADE_META_SLOT_EMPTY,
    BOOT_UPGRADE_META_SLOT_VALID,
    BOOT_UPGRADE_META_SLOT_INVALID_RECORD,
    BOOT_UPGRADE_META_SLOT_IO_ERROR
} boot_upgrade_meta_slot_status_t;

typedef enum
{
    BOOT_UPGRADE_META_SELECT_OK = 0,
    BOOT_UPGRADE_META_SELECT_OK_DEGRADED,
    BOOT_UPGRADE_META_SELECT_NO_VALID_SLOT,
    BOOT_UPGRADE_META_SELECT_EXTERNAL_IO_ERROR,
    BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT
} boot_upgrade_meta_select_status_t;

typedef enum
{
    BOOT_UPGRADE_META_WRITE_OK = 0,
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT,
    BOOT_UPGRADE_META_WRITE_INVALID_SLOT,
    BOOT_UPGRADE_META_WRITE_ENCODE_FAILED,
    BOOT_UPGRADE_META_WRITE_ERASE_FAILED,
    BOOT_UPGRADE_META_WRITE_ERASE_VERIFY_FAILED,
    BOOT_UPGRADE_META_WRITE_FIELDS_PROGRAM_FAILED,
    BOOT_UPGRADE_META_WRITE_FIELDS_VERIFY_FAILED,
    BOOT_UPGRADE_META_WRITE_CRC_PROGRAM_FAILED,
    BOOT_UPGRADE_META_WRITE_CRC_VERIFY_FAILED,
    BOOT_UPGRADE_META_WRITE_MARKER_PROGRAM_FAILED,
    BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED,
    BOOT_UPGRADE_META_WRITE_GENERATION_OVERFLOW
} boot_upgrade_meta_write_status_t;

boot_upgrade_meta_slot_status_t boot_upgrade_meta_read_slot(boot_upgrade_meta_slot_t slot, upgrade_meta_t *meta);
boot_upgrade_meta_select_status_t boot_upgrade_meta_select(upgrade_meta_t *selected_meta, boot_upgrade_meta_slot_t *selected_slot);
boot_upgrade_meta_write_status_t boot_upgrade_meta_write_inactive(boot_upgrade_meta_slot_t active_slot, const upgrade_meta_t *meta, boot_upgrade_meta_slot_t *written_slot);
boot_upgrade_meta_write_status_t boot_upgrade_meta_update(boot_upgrade_meta_slot_t active_slot, const upgrade_meta_t *active_meta, const upgrade_meta_t *updated_meta, upgrade_meta_t *committed_meta, boot_upgrade_meta_slot_t *written_slot);

extern volatile boot_upgrade_meta_slot_status_t g_boot_upgrade_meta_slot_a_status;

extern volatile boot_upgrade_meta_slot_status_t g_boot_upgrade_meta_slot_b_status;

extern volatile boot_upgrade_meta_slot_t g_boot_upgrade_meta_selected_slot;

extern volatile boot_upgrade_meta_select_status_t g_boot_upgrade_meta_select_status;

extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_meta_write_status;

extern volatile boot_upgrade_meta_slot_t g_boot_upgrade_meta_written_slot;

#endif /* BOOT_UPGRADE_META_H */
