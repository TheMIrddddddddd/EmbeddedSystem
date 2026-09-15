#include "boot_upgrade_end.h"

#include "common_crc.h"
#include "common_flash_layout.h"
#include "gd32f4xx_fwdgt.h"
#include "boot_upgrade_context.h"
#include "boot_upgrade_staging.h"
#include "boot_upgrade_state.h"

volatile boot_upgrade_end_status_t g_boot_upgrade_end_status;
volatile uint32_t g_boot_upgrade_end_calculated_crc32;
volatile uint32_t g_boot_upgrade_end_msp;
volatile uint32_t g_boot_upgrade_end_reset_handler;
volatile image_manifest_t g_boot_upgrade_end_manifest;
volatile board_internal_flash_status_t g_boot_upgrade_end_flash_status;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_end_meta_status;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_end_cleanup_status;

static uint32_t boot_upgrade_end_image_crc32_calculate(
    uint32_t image_address,
    uint32_t image_length)
{
    uint32_t crc;
    uint32_t index;
    uint32_t bit;

    crc = 0xFFFFFFFFUL;

    for (index = 0U; index < image_length; index++)
    {
        crc ^= (uint32_t)(*(volatile const uint8_t *)(image_address + index));

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x00000001UL) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }

        if ((index & 0x0FFFU) == 0x0FFFU)
        {
            fwdgt_counter_reload();
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static boot_upgrade_end_status_t boot_upgrade_end_fail(
    boot_upgrade_end_status_t status)
{
    g_boot_upgrade_end_cleanup_status = boot_upgrade_abort_receiving();
    g_boot_upgrade_end_status = status;
    return status;
}

static boot_upgrade_end_status_t boot_upgrade_end_meta_mark_staged(void)
{
    upgrade_meta_t active_meta;
    upgrade_meta_t next_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t selected_meta;
    boot_upgrade_meta_slot_t active_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;
    boot_upgrade_meta_select_status_t select_status;
    boot_upgrade_meta_write_status_t write_status;

    if ((g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED;
    }

    if ((g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED;
    }

    active_slot = g_boot_meta_scan_slot;
    active_meta = g_boot_meta_selected;
    next_meta = active_meta;
    next_meta.state = FW_STATE_STAGED_VALID;
    next_meta.install_stage = INSTALL_APP_VALID;

    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );
    g_boot_upgrade_end_meta_status = write_status;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED;
    }

    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_boot_upgrade_end_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        return BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;

    return BOOT_UPGRADE_END_STATUS_OK;
}

boot_upgrade_end_status_t boot_upgrade_end_accept(void)
{
    uint8_t manifest_raw[IMAGE_MANIFEST_SIZE];
    uint8_t manifest_readback_raw[IMAGE_MANIFEST_SIZE];
    image_manifest_t readback_manifest;
    fw_format_status_t format_status;
    uint32_t pending_size;
    uint32_t pending_crc32;
    uint32_t pending_version;
    uint32_t reset_address;
    uint32_t index;
    boot_upgrade_end_status_t end_status;

    g_boot_upgrade_end_status = BOOT_UPGRADE_END_STATUS_INVALID_ARGUMENT;
    g_boot_upgrade_end_calculated_crc32 = 0U;
    g_boot_upgrade_end_msp = 0U;
    g_boot_upgrade_end_reset_handler = 0U;
    g_boot_upgrade_end_manifest.magic = 0U;
    g_boot_upgrade_end_manifest.image_version = 0U;
    g_boot_upgrade_end_manifest.image_size = 0U;
    g_boot_upgrade_end_manifest.image_crc32 = 0U;
    g_boot_upgrade_end_manifest.manifest_crc32 = 0U;
    g_boot_upgrade_end_flash_status = BOARD_INTERNAL_FLASH_STATUS_OK;
    g_boot_upgrade_end_meta_status = BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_end_cleanup_status = BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;

    if ((g_boot_meta_selected.state != FW_STATE_RECEIVING) ||
        ((g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_ONLINE) &&
         (g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_TF_OFFLINE)))
    {
        g_boot_upgrade_end_status = BOOT_UPGRADE_END_STATUS_STATE_NOT_ALLOWED;
        return BOOT_UPGRADE_END_STATUS_STATE_NOT_ALLOWED;
    }

    pending_size = g_boot_meta_selected.pending_size;
    pending_crc32 = g_boot_meta_selected.pending_crc32;
    pending_version = g_boot_meta_selected.pending_version;

    if ((pending_size == 0U) || (pending_size > MAX_IMAGE_SIZE))
    {
        return boot_upgrade_end_fail(BOOT_UPGRADE_END_STATUS_INVALID_ARGUMENT);
    }

    if (g_boot_upgrade_staging_received_length != pending_size)
    {
        return boot_upgrade_end_fail(BOOT_UPGRADE_END_STATUS_LENGTH_MISMATCH);
    }

    g_boot_upgrade_end_calculated_crc32 =
        boot_upgrade_end_image_crc32_calculate(STAGING_BASE, pending_size);

    if (g_boot_upgrade_end_calculated_crc32 != pending_crc32)
    {
        return boot_upgrade_end_fail(BOOT_UPGRADE_END_STATUS_IMAGE_CRC_INVALID);
    }

    g_boot_upgrade_end_msp = *(volatile const uint32_t *)STAGING_BASE;
    g_boot_upgrade_end_reset_handler =
        *(volatile const uint32_t *)(STAGING_BASE + 4U);

    if ((g_boot_upgrade_end_msp < (SRAM_BASE + 8U)) ||
        (g_boot_upgrade_end_msp > SRAM_TOP) ||
        ((g_boot_upgrade_end_msp & 0x7U) != 0U))
    {
        return boot_upgrade_end_fail(BOOT_UPGRADE_END_STATUS_MSP_INVALID);
    }

    reset_address = g_boot_upgrade_end_reset_handler & ~1UL;

    if ((reset_address < APP_BASE) ||
        (reset_address >= (APP_BASE + pending_size)) ||
        (reset_address >= APP_MANIFEST_ADDR) ||
        ((g_boot_upgrade_end_reset_handler & 1U) == 0U))
    {
        return boot_upgrade_end_fail(BOOT_UPGRADE_END_STATUS_RESET_INVALID);
    }

    g_boot_upgrade_end_manifest.magic = IMAGE_MANIFEST_MAGIC;
    g_boot_upgrade_end_manifest.image_version = pending_version;
    g_boot_upgrade_end_manifest.image_size = pending_size;
    g_boot_upgrade_end_manifest.image_crc32 =
        g_boot_upgrade_end_calculated_crc32;
    g_boot_upgrade_end_manifest.manifest_crc32 = 0U;

    format_status = image_manifest_encode(
        (const image_manifest_t *)&g_boot_upgrade_end_manifest,
        manifest_raw,
        sizeof(manifest_raw)
    );

    if (format_status != FW_FORMAT_STATUS_OK)
    {
        return boot_upgrade_end_fail(
            BOOT_UPGRADE_END_STATUS_MANIFEST_ENCODE_FAILED
        );
    }

    g_boot_upgrade_end_flash_status = board_internal_flash_page_program(
        BOARD_INTERNAL_FLASH_REGION_STAGING,
        STAGING_PAGE_COUNT - 1U,
        STAGING_MANIFEST_ADDR % INTERNAL_FLASH_PAGE_SIZE,
        manifest_raw,
        IMAGE_MANIFEST_SIZE
    );

    if (g_boot_upgrade_end_flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return boot_upgrade_end_fail(
            BOOT_UPGRADE_END_STATUS_MANIFEST_PROGRAM_FAILED
        );
    }

    for (index = 0U; index < IMAGE_MANIFEST_SIZE; index++)
    {
        manifest_readback_raw[index] =
            *(volatile const uint8_t *)(STAGING_MANIFEST_ADDR + index);
    }

    format_status = image_manifest_decode(
        manifest_readback_raw,
        sizeof(manifest_readback_raw),
        &readback_manifest
    );

    if ((format_status != FW_FORMAT_STATUS_OK) ||
        (readback_manifest.magic != g_boot_upgrade_end_manifest.magic) ||
        (readback_manifest.image_version != g_boot_upgrade_end_manifest.image_version) ||
        (readback_manifest.image_size != g_boot_upgrade_end_manifest.image_size) ||
        (readback_manifest.image_crc32 != g_boot_upgrade_end_manifest.image_crc32))
    {
        return boot_upgrade_end_fail(
            BOOT_UPGRADE_END_STATUS_MANIFEST_VERIFY_FAILED
        );
    }

    g_boot_upgrade_end_manifest.manifest_crc32 =
        readback_manifest.manifest_crc32;

    end_status = boot_upgrade_end_meta_mark_staged();

    if (end_status != BOOT_UPGRADE_END_STATUS_OK)
    {
        return boot_upgrade_end_fail(end_status);
    }

    g_boot_upgrade_end_cleanup_status = BOOT_UPGRADE_META_WRITE_OK;
    g_boot_upgrade_end_status = BOOT_UPGRADE_END_STATUS_OK;
    return BOOT_UPGRADE_END_STATUS_OK;
}
