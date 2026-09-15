#include "boot_upgrade_install.h"

#include "common_flash_layout.h"
#include "gd32f4xx.h"
#include "gd32f4xx_fwdgt.h"

#include "boot_upgrade_context.h"

#define BOOT_UPGRADE_INSTALL_COPY_CHUNK_SIZE   256U
#define BOOT_UPGRADE_INSTALL_CRC_FEED_INTERVAL 0x1000U

static uint8_t s_boot_upgrade_install_copy_buffer[
    BOOT_UPGRADE_INSTALL_COPY_CHUNK_SIZE
];
static uint8_t s_boot_upgrade_install_started_this_boot;

volatile boot_upgrade_install_status_t g_boot_upgrade_install_status =
    BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION;
volatile install_stage_t g_boot_upgrade_install_stage = INSTALL_APP_VALID;
volatile board_internal_flash_status_t g_boot_upgrade_install_flash_status =
    BOARD_INTERNAL_FLASH_STATUS_OK;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_install_meta_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile boot_upgrade_meta_slot_t g_boot_upgrade_install_written_slot =
    BOOT_UPGRADE_META_SLOT_NONE;
volatile uint32_t g_boot_upgrade_install_page_index;
volatile uint32_t g_boot_upgrade_install_completed_pages;
volatile uint32_t g_boot_upgrade_install_copy_offset;
volatile uint32_t g_boot_upgrade_install_copy_length;
volatile uint32_t g_boot_upgrade_install_calculated_crc32;
volatile uint32_t g_boot_upgrade_install_msp;
volatile uint32_t g_boot_upgrade_install_reset_handler;
volatile uint8_t g_boot_upgrade_install_started;

static void boot_upgrade_install_set_status(
    boot_upgrade_install_status_t status)
{
    g_boot_upgrade_install_status = status;
    g_boot_upgrade_install_stage =
        (install_stage_t)g_boot_meta_selected.install_stage;
}

static uint8_t boot_upgrade_install_meta_context_valid(void)
{
    if ((g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (g_boot_meta_scan_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        return 0U;
    }

    if ((g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t boot_upgrade_install_region_info_get(
    board_internal_flash_region_t region,
    uint32_t *base,
    uint32_t *page_count,
    uint32_t *manifest_address)
{
    if ((base == 0) || (page_count == 0) || (manifest_address == 0))
    {
        return 0U;
    }

    switch (region)
    {
        case BOARD_INTERNAL_FLASH_REGION_APP:
            *base = APP_BASE;
            *page_count = APP_PAGE_COUNT;
            *manifest_address = APP_MANIFEST_ADDR;
            return 1U;

        case BOARD_INTERNAL_FLASH_REGION_BACKUP:
            *base = BACKUP_BASE;
            *page_count = BACKUP_PAGE_COUNT;
            *manifest_address = BACKUP_MANIFEST_ADDR;
            return 1U;

        case BOARD_INTERNAL_FLASH_REGION_STAGING:
            *base = STAGING_BASE;
            *page_count = STAGING_PAGE_COUNT;
            *manifest_address = STAGING_MANIFEST_ADDR;
            return 1U;

        default:
            return 0U;
    }
}

static uint32_t boot_upgrade_install_crc32_calculate(
    uint32_t image_address,
    uint32_t image_length)
{
    uint32_t crc;
    uint32_t index;
    uint32_t bit;

    if ((image_address == 0U) || (image_length == 0U))
    {
        return 0U;
    }

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

        if ((index & (BOOT_UPGRADE_INSTALL_CRC_FEED_INTERVAL - 1U)) ==
            (BOOT_UPGRADE_INSTALL_CRC_FEED_INTERVAL - 1U))
        {
            fwdgt_counter_reload();
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t boot_upgrade_install_vector_validate(
    uint32_t storage_base,
    uint32_t image_size)
{
    uint32_t reset_address;

    if ((image_size == 0U) || (image_size > MAX_IMAGE_SIZE))
    {
        return 0U;
    }

    g_boot_upgrade_install_msp =
        *(volatile const uint32_t *)storage_base;
    g_boot_upgrade_install_reset_handler =
        *(volatile const uint32_t *)(storage_base + 4U);

    if ((g_boot_upgrade_install_msp < (SRAM_BASE + 8U)) ||
        (g_boot_upgrade_install_msp > SRAM_TOP) ||
        ((g_boot_upgrade_install_msp & 0x7U) != 0U))
    {
        return 0U;
    }

    /* 映像按 APP_BASE 链接，存放到 Backup/Staging 不改变向量地址。 */
    reset_address = g_boot_upgrade_install_reset_handler & ~1UL;

    if ((reset_address < APP_BASE) ||
        (reset_address >= (APP_BASE + image_size)) ||
        (reset_address >= APP_MANIFEST_ADDR) ||
        ((g_boot_upgrade_install_reset_handler & 1U) == 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t boot_upgrade_install_manifest_read_validate(
    board_internal_flash_region_t region,
    uint32_t expected_version,
    uint32_t expected_size,
    uint32_t expected_crc32)
{
    uint32_t base;
    uint32_t page_count;
    uint32_t manifest_address;
    uint8_t manifest_raw[IMAGE_MANIFEST_SIZE];
    image_manifest_t manifest;
    fw_format_status_t format_status;
    uint32_t index;

    if ((expected_size == 0U) || (expected_size > MAX_IMAGE_SIZE))
    {
        return 0U;
    }

    if (boot_upgrade_install_region_info_get(
            region,
            &base,
            &page_count,
            &manifest_address) == 0U)
    {
        return 0U;
    }

    (void)base;
    (void)page_count;

    for (index = 0U; index < IMAGE_MANIFEST_SIZE; index++)
    {
        manifest_raw[index] =
            *(volatile const uint8_t *)(manifest_address + index);
    }

    format_status = image_manifest_decode(
        manifest_raw,
        sizeof(manifest_raw),
        &manifest
    );

    if (format_status != FW_FORMAT_STATUS_OK)
    {
        return 0U;
    }

    if ((manifest.magic != IMAGE_MANIFEST_MAGIC) ||
        (manifest.image_version != expected_version) ||
        (manifest.image_size != expected_size) ||
        (manifest.image_crc32 != expected_crc32))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t boot_upgrade_install_image_validate(
    board_internal_flash_region_t region,
    uint32_t image_version,
    uint32_t image_size,
    uint32_t image_crc32,
    uint8_t require_manifest)
{
    uint32_t base;
    uint32_t page_count;
    uint32_t manifest_address;

    if (boot_upgrade_install_region_info_get(
            region,
            &base,
            &page_count,
            &manifest_address) == 0U)
    {
        return 0U;
    }

    (void)page_count;
    (void)manifest_address;

    if ((image_size == 0U) || (image_size > MAX_IMAGE_SIZE))
    {
        return 0U;
    }

    if ((require_manifest != 0U) &&
        (boot_upgrade_install_manifest_read_validate(
            region,
            image_version,
            image_size,
            image_crc32) == 0U))
    {
        return 0U;
    }

    g_boot_upgrade_install_calculated_crc32 =
        boot_upgrade_install_crc32_calculate(base, image_size);

    if (g_boot_upgrade_install_calculated_crc32 != image_crc32)
    {
        return 0U;
    }

    return boot_upgrade_install_vector_validate(base, image_size);
}

static board_internal_flash_status_t boot_upgrade_install_region_erase(
    board_internal_flash_region_t region,
    uint32_t page_count)
{
    uint32_t page_index;
    board_internal_flash_status_t flash_status;

    g_boot_upgrade_install_page_index = 0U;
    g_boot_upgrade_install_completed_pages = 0U;

    for (page_index = 0U; page_index < page_count; page_index++)
    {
        g_boot_upgrade_install_page_index = page_index;

        flash_status = board_internal_flash_page_erase(region, page_index);
        g_boot_upgrade_install_flash_status = flash_status;

        if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
        {
            return flash_status;
        }

        g_boot_upgrade_install_completed_pages++;
        fwdgt_counter_reload();
    }

    g_boot_upgrade_install_page_index = page_count;

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

static board_internal_flash_status_t boot_upgrade_install_image_copy(
    uint32_t source_base,
    board_internal_flash_region_t destination_region,
    uint32_t image_size)
{
    uint32_t offset;
    uint32_t remaining;
    uint32_t page_index;
    uint32_t page_offset;
    uint32_t page_remaining;
    uint32_t copy_length;
    uint32_t index;
    board_internal_flash_status_t flash_status;

    if ((image_size == 0U) || (image_size > MAX_IMAGE_SIZE))
    {
        return BOARD_INTERNAL_FLASH_STATUS_LENGTH_INVALID;
    }

    offset = 0U;
    g_boot_upgrade_install_copy_offset = 0U;
    g_boot_upgrade_install_copy_length = image_size;

    while (offset < image_size)
    {
        remaining = image_size - offset;
        page_index = offset / INTERNAL_FLASH_PAGE_SIZE;
        page_offset = offset % INTERNAL_FLASH_PAGE_SIZE;
        page_remaining = INTERNAL_FLASH_PAGE_SIZE - page_offset;

        copy_length = remaining;
        if (copy_length > BOOT_UPGRADE_INSTALL_COPY_CHUNK_SIZE)
        {
            copy_length = BOOT_UPGRADE_INSTALL_COPY_CHUNK_SIZE;
        }
        if (copy_length > page_remaining)
        {
            copy_length = page_remaining;
        }

        for (index = 0U; index < copy_length; index++)
        {
            s_boot_upgrade_install_copy_buffer[index] =
                *(volatile const uint8_t *)(source_base + offset + index);
        }

        flash_status = board_internal_flash_page_program(
            destination_region,
            page_index,
            page_offset,
            s_boot_upgrade_install_copy_buffer,
            copy_length
        );

        g_boot_upgrade_install_flash_status = flash_status;

        if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
        {
            g_boot_upgrade_install_copy_offset = offset;
            return flash_status;
        }

        offset += copy_length;
        g_boot_upgrade_install_copy_offset = offset;
        fwdgt_counter_reload();
    }

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}

static boot_upgrade_install_status_t boot_upgrade_install_manifest_write(
    board_internal_flash_region_t region,
    uint32_t image_version,
    uint32_t image_size,
    uint32_t image_crc32)
{
    uint32_t base;
    uint32_t page_count;
    uint32_t manifest_address;
    uint8_t manifest_raw[IMAGE_MANIFEST_SIZE];
    uint8_t manifest_readback_raw[IMAGE_MANIFEST_SIZE];
    image_manifest_t manifest;
    image_manifest_t readback_manifest;
    fw_format_status_t format_status;
    uint32_t index;

    if (boot_upgrade_install_region_info_get(
            region,
            &base,
            &page_count,
            &manifest_address) == 0U)
    {
        return BOOT_UPGRADE_INSTALL_STATUS_INVALID_ARGUMENT;
    }

    manifest.magic = IMAGE_MANIFEST_MAGIC;
    manifest.image_version = image_version;
    manifest.image_size = image_size;
    manifest.image_crc32 = image_crc32;
    manifest.manifest_crc32 = 0U;

    format_status = image_manifest_encode(
        &manifest,
        manifest_raw,
        sizeof(manifest_raw)
    );

    if (format_status != FW_FORMAT_STATUS_OK)
    {
        return (region == BOARD_INTERNAL_FLASH_REGION_BACKUP) ?
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED :
            BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED;
    }

    g_boot_upgrade_install_flash_status = board_internal_flash_page_program(
        region,
        page_count - 1U,
        manifest_address % INTERNAL_FLASH_PAGE_SIZE,
        manifest_raw,
        IMAGE_MANIFEST_SIZE
    );

    if (g_boot_upgrade_install_flash_status !=
        BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        return (region == BOARD_INTERNAL_FLASH_REGION_BACKUP) ?
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED :
            BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED;
    }

    for (index = 0U; index < IMAGE_MANIFEST_SIZE; index++)
    {
        manifest_readback_raw[index] =
            *(volatile const uint8_t *)(manifest_address + index);
    }

    format_status = image_manifest_decode(
        manifest_readback_raw,
        sizeof(manifest_readback_raw),
        &readback_manifest
    );

    if ((format_status != FW_FORMAT_STATUS_OK) ||
        (readback_manifest.magic != manifest.magic) ||
        (readback_manifest.image_version != manifest.image_version) ||
        (readback_manifest.image_size != manifest.image_size) ||
        (readback_manifest.image_crc32 != manifest.image_crc32))
    {
        return (region == BOARD_INTERNAL_FLASH_REGION_BACKUP) ?
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED :
            BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED;
    }

    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

static boot_upgrade_install_status_t boot_upgrade_install_meta_commit(
    const upgrade_meta_t *next_meta)
{
    upgrade_meta_t active_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t selected_meta;
    boot_upgrade_meta_slot_t active_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t selected_slot;
    boot_upgrade_meta_write_status_t write_status;
    boot_upgrade_meta_select_status_t select_status;

    g_boot_upgrade_install_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_install_written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    if ((next_meta == 0) ||
        (boot_upgrade_install_meta_context_valid() == 0U))
    {
        return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
    }

    active_slot = g_boot_meta_scan_slot;
    active_meta = g_boot_meta_selected;
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        next_meta,
        &committed_meta,
        &written_slot
    );

    g_boot_upgrade_install_meta_status = write_status;
    g_boot_upgrade_install_written_slot = written_slot;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
    }

    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_boot_upgrade_install_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;
    g_boot_upgrade_install_stage =
        (install_stage_t)selected_meta.install_stage;

    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

static boot_upgrade_install_status_t boot_upgrade_install_meta_to_staged(void)
{
    upgrade_meta_t next_meta;

    next_meta = g_boot_meta_selected;
    next_meta.state = FW_STATE_STAGED_VALID;
    next_meta.install_stage = INSTALL_APP_VALID;

    return boot_upgrade_install_meta_commit(&next_meta);
}

static boot_upgrade_install_status_t boot_upgrade_install_meta_to_stage(
    install_stage_t stage)
{
    upgrade_meta_t next_meta;

    next_meta = g_boot_meta_selected;
    next_meta.state = FW_STATE_INSTALLING;
    next_meta.install_stage = stage;

    return boot_upgrade_install_meta_commit(&next_meta);
}

static boot_upgrade_install_status_t boot_upgrade_install_meta_to_trial(void)
{
    upgrade_meta_t next_meta;

    next_meta = g_boot_meta_selected;
    next_meta.state = FW_STATE_TRIAL_PENDING;
    next_meta.install_stage = INSTALL_APP_VALID;
    next_meta.failure_count = 0U;

    return boot_upgrade_install_meta_commit(&next_meta);
}

static boot_upgrade_install_status_t
boot_upgrade_install_meta_to_rollback_required(void)
{
    upgrade_meta_t next_meta;

    if (g_boot_meta_selected.state == FW_STATE_ROLLBACK_REQUIRED)
    {
        return BOOT_UPGRADE_INSTALL_STATUS_OK;
    }

    next_meta = g_boot_meta_selected;
    next_meta.state = FW_STATE_ROLLBACK_REQUIRED;
    next_meta.install_stage = INSTALL_APP_VALID;

    return boot_upgrade_install_meta_commit(&next_meta);
}

static boot_upgrade_install_status_t boot_upgrade_install_meta_finish_rollback(void)
{
    upgrade_meta_t next_meta;

    next_meta = g_boot_meta_selected;

    if (g_boot_meta_selected.upgrade_source == UPGRADE_SOURCE_TF_OFFLINE)
    {
        /* 先保留失败包身份，供下次 Boot 幂等改名为 .failed。 */
        next_meta.failed_package_crc32 =
            g_boot_meta_selected.pending_crc32;
        next_meta.failed_package_version =
            g_boot_meta_selected.pending_version;
        next_meta.upgrade_source = UPGRADE_SOURCE_TF_OFFLINE;
    }
    else
    {
        next_meta.failed_package_crc32 = 0U;
        next_meta.failed_package_version = 0U;
        next_meta.upgrade_source = UPGRADE_SOURCE_NONE;
    }

    next_meta.state = FW_STATE_IDLE;
    next_meta.install_stage = INSTALL_APP_VALID;
    next_meta.pending_size = 0U;
    next_meta.pending_crc32 = 0U;
    next_meta.pending_version = 0U;
    next_meta.backup_size = 0U;
    next_meta.backup_crc32 = 0U;
    next_meta.backup_version = 0U;
    next_meta.failure_count = 0U;

    return boot_upgrade_install_meta_commit(&next_meta);
}

static boot_upgrade_install_status_t boot_upgrade_install_restage_after_failure(
    boot_upgrade_install_status_t failure_status)
{
    boot_upgrade_install_status_t meta_status;

    meta_status = boot_upgrade_install_meta_to_staged();

    if (meta_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
    }

    s_boot_upgrade_install_started_this_boot = 0U;
    g_boot_upgrade_install_started = 0U;
    boot_upgrade_install_set_status(failure_status);

    return failure_status;
}

static boot_upgrade_install_status_t boot_upgrade_install_rollback(void)
{
    uint32_t backup_size;
    uint32_t backup_crc32;
    uint32_t backup_version;
    board_internal_flash_status_t flash_status;
    boot_upgrade_install_status_t install_status;

    backup_size = g_boot_meta_selected.backup_size;
    backup_crc32 = g_boot_meta_selected.backup_crc32;
    backup_version = g_boot_meta_selected.backup_version;

    /* 先校验备份区，确认旧 App 仍有可用的回滚依据。 */
    if ((backup_size == 0U) || (backup_size > MAX_IMAGE_SIZE) ||
        (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_BACKUP,
            backup_version,
            backup_size,
            backup_crc32,
            1U) == 0U))
    {
        install_status =
            boot_upgrade_install_meta_to_rollback_required();
        if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
        {
            boot_upgrade_install_set_status(
                BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED
            );
            return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED;
        }

        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED;
    }

    flash_status = boot_upgrade_install_region_erase(
        BOARD_INTERNAL_FLASH_REGION_APP,
        APP_PAGE_COUNT
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        (void)boot_upgrade_install_meta_to_rollback_required();
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED;
    }

    flash_status = boot_upgrade_install_image_copy(
        BACKUP_BASE,
        BOARD_INTERNAL_FLASH_REGION_APP,
        backup_size
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        (void)boot_upgrade_install_meta_to_rollback_required();
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED;
    }

    install_status = boot_upgrade_install_manifest_write(
        BOARD_INTERNAL_FLASH_REGION_APP,
        backup_version,
        backup_size,
        backup_crc32
    );

    if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        (void)boot_upgrade_install_meta_to_rollback_required();
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED;
    }

    if (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_APP,
            backup_version,
            backup_size,
            backup_crc32,
            1U) == 0U)
    {
        (void)boot_upgrade_install_meta_to_rollback_required();
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_FAILED;
    }

    install_status = boot_upgrade_install_meta_finish_rollback();

    if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        (void)boot_upgrade_install_meta_to_rollback_required();
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED;
    }

    s_boot_upgrade_install_started_this_boot = 0U;
    g_boot_upgrade_install_started = 0U;
    boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
    fwdgt_counter_reload();

    /* 回滚完成后重新启动，让统一启动分派启动旧 App。 */
    NVIC_SystemReset();

    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

boot_upgrade_install_status_t boot_upgrade_install_rollback_now(void)
{
    return boot_upgrade_install_rollback();
}

static boot_upgrade_install_status_t boot_upgrade_install_backup_prepare(void)
{
    uint32_t active_size;
    uint32_t active_crc32;
    uint32_t active_version;
    board_internal_flash_status_t flash_status;
    boot_upgrade_install_status_t install_status;

    active_size = g_boot_meta_selected.active_size;
    active_crc32 = g_boot_meta_selected.active_crc32;
    active_version = g_boot_meta_selected.active_version;

    if ((active_size == 0U) || (active_size > MAX_IMAGE_SIZE) ||
        (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_APP,
            active_version,
            active_size,
            active_crc32,
            1U) == 0U))
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_ACTIVE_INVALID
        );
        return BOOT_UPGRADE_INSTALL_STATUS_ACTIVE_INVALID;
    }

    if ((g_boot_meta_selected.pending_size == 0U) ||
        (g_boot_meta_selected.pending_size > MAX_IMAGE_SIZE) ||
        (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_STAGING,
            g_boot_meta_selected.pending_version,
            g_boot_meta_selected.pending_size,
            g_boot_meta_selected.pending_crc32,
            1U) == 0U))
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID
        );
        return BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID;
    }

    flash_status = boot_upgrade_install_region_erase(
        BOARD_INTERNAL_FLASH_REGION_BACKUP,
        BACKUP_PAGE_COUNT
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_ERASE_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_BACKUP_ERASE_FAILED;
    }

    flash_status = boot_upgrade_install_image_copy(
        APP_BASE,
        BOARD_INTERNAL_FLASH_REGION_BACKUP,
        active_size
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_COPY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_BACKUP_COPY_FAILED;
    }

    if (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_BACKUP,
            active_version,
            active_size,
            active_crc32,
            0U) == 0U)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_VERIFY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_BACKUP_VERIFY_FAILED;
    }

    install_status = boot_upgrade_install_manifest_write(
        BOARD_INTERNAL_FLASH_REGION_BACKUP,
        active_version,
        active_size,
        active_crc32
    );

    if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_BACKUP_MANIFEST_FAILED;
    }

    if (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_BACKUP,
            active_version,
            active_size,
            active_crc32,
            1U) == 0U)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_BACKUP_VERIFY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_BACKUP_VERIFY_FAILED;
    }

    {
        upgrade_meta_t next_meta;
        boot_upgrade_install_status_t meta_status;

        next_meta = g_boot_meta_selected;
        next_meta.state = FW_STATE_INSTALLING;
        next_meta.install_stage = INSTALL_BACKUP_VALID;
        next_meta.backup_size = active_size;
        next_meta.backup_crc32 = active_crc32;
        next_meta.backup_version = active_version;

        meta_status = boot_upgrade_install_meta_commit(&next_meta);

        if (meta_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
        {
            boot_upgrade_install_set_status(
                BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
            );
            return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
        }
    }

    boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

static boot_upgrade_install_status_t boot_upgrade_install_app_erase(void)
{
    board_internal_flash_status_t flash_status;

    flash_status = boot_upgrade_install_region_erase(
        BOARD_INTERNAL_FLASH_REGION_APP,
        APP_PAGE_COUNT
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_APP_ERASE_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_APP_ERASE_FAILED;
    }

    boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

static boot_upgrade_install_status_t boot_upgrade_install_app_program(void)
{
    uint32_t pending_size;
    uint32_t pending_crc32;
    uint32_t pending_version;
    board_internal_flash_status_t flash_status;
    boot_upgrade_install_status_t install_status;

    pending_size = g_boot_meta_selected.pending_size;
    pending_crc32 = g_boot_meta_selected.pending_crc32;
    pending_version = g_boot_meta_selected.pending_version;

    if ((pending_size == 0U) || (pending_size > MAX_IMAGE_SIZE))
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID
        );
        return BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID;
    }

    flash_status = boot_upgrade_install_image_copy(
        STAGING_BASE,
        BOARD_INTERNAL_FLASH_REGION_APP,
        pending_size
    );

    if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_APP_COPY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_APP_COPY_FAILED;
    }

    if (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_APP,
            pending_version,
            pending_size,
            pending_crc32,
            0U) == 0U)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_APP_VERIFY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_APP_VERIFY_FAILED;
    }

    install_status = boot_upgrade_install_manifest_write(
        BOARD_INTERNAL_FLASH_REGION_APP,
        pending_version,
        pending_size,
        pending_crc32
    );

    if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_APP_MANIFEST_FAILED;
    }

    if (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_APP,
            pending_version,
            pending_size,
            pending_crc32,
            1U) == 0U)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_APP_VERIFY_FAILED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_APP_VERIFY_FAILED;
    }

    boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

boot_upgrade_install_status_t boot_upgrade_install_start(void)
{
    if (boot_upgrade_install_meta_context_valid() == 0U)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_INVALID_ARGUMENT
        );
        return BOOT_UPGRADE_INSTALL_STATUS_INVALID_ARGUMENT;
    }

    if ((g_boot_meta_selected.state != FW_STATE_STAGED_VALID) ||
        ((g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_ONLINE) &&
         (g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_TF_OFFLINE)))
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED
        );
        return BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED;
    }

    /* INSTALL ACK 之前先确认 Staging 的 manifest、CRC 和向量表。 */
    if ((g_boot_meta_selected.pending_size == 0U) ||
        (g_boot_meta_selected.pending_size > MAX_IMAGE_SIZE) ||
        (boot_upgrade_install_image_validate(
            BOARD_INTERNAL_FLASH_REGION_STAGING,
            g_boot_meta_selected.pending_version,
            g_boot_meta_selected.pending_size,
            g_boot_meta_selected.pending_crc32,
            1U) == 0U))
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID
        );
        return BOOT_UPGRADE_INSTALL_STATUS_STAGING_INVALID;
    }

    {
        upgrade_meta_t next_meta;
        boot_upgrade_install_status_t meta_status;

        next_meta = g_boot_meta_selected;
        next_meta.state = FW_STATE_INSTALLING;
        next_meta.install_stage = INSTALL_BACKUP_START;

        meta_status = boot_upgrade_install_meta_commit(&next_meta);

        if (meta_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
        {
            boot_upgrade_install_set_status(
                BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
            );
            return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
        }
    }

    s_boot_upgrade_install_started_this_boot = 1U;
    g_boot_upgrade_install_started = 1U;
    boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);

    return BOOT_UPGRADE_INSTALL_STATUS_OK;
}

boot_upgrade_install_status_t boot_upgrade_install_process(void)
{
    boot_upgrade_install_status_t status;
    boot_upgrade_install_status_t rollback_status;

    g_boot_upgrade_install_stage =
        (install_stage_t)g_boot_meta_selected.install_stage;

    if (g_boot_meta_selected.state == FW_STATE_ROLLBACK_REQUIRED)
    {
        return boot_upgrade_install_rollback_now();
    }

    if (g_boot_meta_selected.state != FW_STATE_INSTALLING)
    {
        boot_upgrade_install_set_status(
            BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION
        );
        return BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION;
    }

    switch ((install_stage_t)g_boot_meta_selected.install_stage)
    {
        case INSTALL_BACKUP_START:
            if (s_boot_upgrade_install_started_this_boot == 0U)
            {
                status = boot_upgrade_install_meta_to_staged();
                if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
                {
                    boot_upgrade_install_set_status(
                        BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
                    );
                    return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
                }

                boot_upgrade_install_set_status(
                    BOOT_UPGRADE_INSTALL_STATUS_OK
                );
                return BOOT_UPGRADE_INSTALL_STATUS_OK;
            }

            status = boot_upgrade_install_backup_prepare();
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                return boot_upgrade_install_restage_after_failure(status);
            }
            return status;

        case INSTALL_BACKUP_VALID:
            if (s_boot_upgrade_install_started_this_boot == 0U)
            {
                return boot_upgrade_install_rollback();
            }

            status = boot_upgrade_install_meta_to_stage(INSTALL_APP_ERASING);
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                return boot_upgrade_install_restage_after_failure(
                    BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
                );
            }
            boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
            return BOOT_UPGRADE_INSTALL_STATUS_OK;

        case INSTALL_APP_ERASING:
            if (s_boot_upgrade_install_started_this_boot == 0U)
            {
                return boot_upgrade_install_rollback();
            }

            status = boot_upgrade_install_app_erase();
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                rollback_status = boot_upgrade_install_rollback();
                return (rollback_status == BOOT_UPGRADE_INSTALL_STATUS_OK) ?
                    status : rollback_status;
            }

            status = boot_upgrade_install_meta_to_stage(
                INSTALL_APP_PROGRAMMING
            );
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                rollback_status = boot_upgrade_install_rollback();
                return (rollback_status == BOOT_UPGRADE_INSTALL_STATUS_OK) ?
                    BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED :
                    rollback_status;
            }
            boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
            return BOOT_UPGRADE_INSTALL_STATUS_OK;

        case INSTALL_APP_PROGRAMMING:
            if (s_boot_upgrade_install_started_this_boot == 0U)
            {
                return boot_upgrade_install_rollback();
            }

            status = boot_upgrade_install_app_program();
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                rollback_status = boot_upgrade_install_rollback();
                return (rollback_status == BOOT_UPGRADE_INSTALL_STATUS_OK) ?
                    status : rollback_status;
            }

            status = boot_upgrade_install_meta_to_stage(INSTALL_APP_VALID);
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                rollback_status = boot_upgrade_install_rollback();
                return (rollback_status == BOOT_UPGRADE_INSTALL_STATUS_OK) ?
                    BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED :
                    rollback_status;
            }
            boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
            return BOOT_UPGRADE_INSTALL_STATUS_OK;

        case INSTALL_APP_VALID:
            if (boot_upgrade_install_image_validate(
                    BOARD_INTERNAL_FLASH_REGION_APP,
                    g_boot_meta_selected.pending_version,
                    g_boot_meta_selected.pending_size,
                    g_boot_meta_selected.pending_crc32,
                    1U) == 0U)
            {
                return boot_upgrade_install_rollback();
            }

            status = boot_upgrade_install_meta_to_trial();
            if (status != BOOT_UPGRADE_INSTALL_STATUS_OK)
            {
                boot_upgrade_install_set_status(
                    BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED
                );
                return BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED;
            }

            boot_upgrade_install_set_status(BOOT_UPGRADE_INSTALL_STATUS_OK);
            fwdgt_counter_reload();

            /* 新 App 只能以 TRIAL_PENDING 身份进入下一次启动。 */
            NVIC_SystemReset();
            return BOOT_UPGRADE_INSTALL_STATUS_OK;

        default:
            boot_upgrade_install_set_status(
                BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED
            );
            return BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED;
    }
}
