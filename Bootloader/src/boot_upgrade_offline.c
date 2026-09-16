#include "boot_upgrade_offline.h"

#include <stddef.h>
#include <string.h>

#include "board_sdio.h"
#include "board_internal_flash.h"
#include "boot_upgrade_context.h"
#include "boot_upgrade_end.h"
#include "boot_upgrade_install.h"
#include "boot_upgrade_staging.h"
#include "boot_upgrade_state.h"
#include "diskio.h"
#include "ff.h"
#include "gd32f4xx_fwdgt.h"

#define BOOT_UPGRADE_OFFLINE_PACKAGE_PATH      "0:/firmware/app.bin"
#define BOOT_UPGRADE_OFFLINE_TARGET_PATH_MAX   64U
#define BOOT_UPGRADE_OFFLINE_BUFFER_SIZE       256U
#define BOOT_UPGRADE_OFFLINE_HEX_DIGITS        8U

typedef enum
{
    BOOT_UPGRADE_OFFLINE_FILE_VALID = 0,
    BOOT_UPGRADE_OFFLINE_FILE_MISSING,
    BOOT_UPGRADE_OFFLINE_FILE_INVALID
} boot_upgrade_offline_file_status_t;

static FATFS s_boot_upgrade_offline_fatfs;
static FIL s_boot_upgrade_offline_file;
static board_sdio_card_info_t s_boot_upgrade_offline_card;
static uint8_t s_boot_upgrade_offline_buffer[
    BOOT_UPGRADE_OFFLINE_BUFFER_SIZE
];
static uint8_t s_boot_upgrade_offline_startup_attempted;

volatile boot_upgrade_offline_status_t g_boot_upgrade_offline_status =
    BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION;
volatile uint32_t g_boot_upgrade_offline_last_fresult;
volatile uint32_t g_boot_upgrade_offline_file_size;
volatile uint32_t g_boot_upgrade_offline_file_version;
volatile uint32_t g_boot_upgrade_offline_file_crc32;
volatile uint32_t g_boot_upgrade_offline_read_offset;
volatile uint32_t g_boot_upgrade_offline_received_length;
volatile uint32_t g_boot_upgrade_offline_calculated_crc32;
volatile uint32_t g_boot_upgrade_offline_staging_status;
volatile uint32_t g_boot_upgrade_offline_end_status;
volatile uint32_t g_boot_upgrade_offline_install_status;
volatile uint8_t g_boot_upgrade_offline_mounted;

static uint32_t boot_upgrade_offline_crc32_update(
    uint32_t crc,
    const uint8_t *data,
    uint32_t length)
{
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint32_t)data[index];

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
    }

    return crc;
}

static void boot_upgrade_offline_hex8(
    char *destination,
    uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uint32_t index;

    for (index = 0U; index < BOOT_UPGRADE_OFFLINE_HEX_DIGITS; index++)
    {
        uint32_t shift =
            (BOOT_UPGRADE_OFFLINE_HEX_DIGITS - 1U - index) * 4U;
        destination[index] = digits[(value >> shift) & 0x0FU];
    }
}

static uint8_t boot_upgrade_offline_target_path(
    char *path,
    uint32_t version,
    uint32_t crc32,
    const char *extension)
{
    static const char prefix[] = "0:/firmware/app_";
    uint32_t position;
    uint32_t extension_length;

    if ((path == NULL) || (extension == NULL))
    {
        return 0U;
    }

    extension_length = (uint32_t)strlen(extension);
    position = 0U;

    if ((sizeof(prefix) - 1U) +
        (BOOT_UPGRADE_OFFLINE_HEX_DIGITS * 2U) +
        1U + extension_length + 1U >
        BOOT_UPGRADE_OFFLINE_TARGET_PATH_MAX)
    {
        return 0U;
    }

    (void)memcpy(&path[position], prefix, sizeof(prefix) - 1U);
    position += (uint32_t)(sizeof(prefix) - 1U);
    boot_upgrade_offline_hex8(&path[position], version);
    position += BOOT_UPGRADE_OFFLINE_HEX_DIGITS;
    path[position++] = '_';
    boot_upgrade_offline_hex8(&path[position], crc32);
    position += BOOT_UPGRADE_OFFLINE_HEX_DIGITS;
    (void)memcpy(&path[position], extension, extension_length);
    position += extension_length;
    path[position] = '\0';

    return 1U;
}

static boot_upgrade_offline_status_t boot_upgrade_offline_mount(void)
{
    board_sdio_status_t sdio_status;
    FRESULT mount_result;

    if (g_boot_upgrade_offline_mounted != 0U)
    {
        return BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION;
    }

    if ((board_sdio_init() == 0) ||
        (board_sdio_card_present() == 0U))
    {
        diskio_sdio_set_not_ready();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CARD_NOT_READY;
        return g_boot_upgrade_offline_status;
    }

    sdio_status = board_sdio_card_init(&s_boot_upgrade_offline_card);
    if (sdio_status != BOARD_SDIO_STATUS_OK)
    {
        diskio_sdio_set_not_ready();
        g_boot_upgrade_offline_last_fresult =
            (uint32_t)sdio_status;
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_MOUNT_FAILED;
        return g_boot_upgrade_offline_status;
    }

    diskio_sdio_set_ready(s_boot_upgrade_offline_card.rca);
    mount_result = f_mount(
        &s_boot_upgrade_offline_fatfs,
        "0:",
        1U
    );
    g_boot_upgrade_offline_last_fresult = (uint32_t)mount_result;

    if (mount_result != FR_OK)
    {
        diskio_sdio_set_not_ready();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_MOUNT_FAILED;
        return g_boot_upgrade_offline_status;
    }

    g_boot_upgrade_offline_mounted = 1U;
    return BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION;
}

static void boot_upgrade_offline_unmount(void)
{
    FRESULT result;

    if (g_boot_upgrade_offline_mounted != 0U)
    {
        result = f_mount(NULL, "0:", 0U);
        g_boot_upgrade_offline_last_fresult = (uint32_t)result;
    }

    g_boot_upgrade_offline_mounted = 0U;
    diskio_sdio_set_not_ready();
}

static uint8_t boot_upgrade_offline_header_read(
    FIL *file,
    firmware_header_t *header)
{
    uint8_t raw[FIRMWARE_HEADER_SIZE];
    UINT bytes_read;
    FRESULT result;

    if ((file == NULL) || (header == NULL))
    {
        return 0U;
    }

    bytes_read = 0U;
    result = f_read(file, raw, FIRMWARE_HEADER_SIZE, &bytes_read);
    g_boot_upgrade_offline_last_fresult = (uint32_t)result;

    if ((result != FR_OK) || (bytes_read != FIRMWARE_HEADER_SIZE))
    {
        return 0U;
    }

    return (firmware_header_decode(
                raw,
                sizeof(raw),
                header) == FW_FORMAT_STATUS_OK) ? 1U : 0U;
}

static uint8_t boot_upgrade_offline_version_allowed(
    const firmware_header_t *header)
{
    uint8_t force_upgrade;
    uint8_t allow_downgrade;

    if (header == NULL)
    {
        return 0U;
    }

    force_upgrade = (header->flags & FIRMWARE_HEADER_FLAG_FORCE_UPGRADE) != 0U;
    allow_downgrade =
        (header->flags & FIRMWARE_HEADER_FLAG_ALLOW_DOWNGRADE) != 0U;

    if (force_upgrade != 0U)
    {
        return 1U;
    }

    if (header->firmware_version == g_boot_meta_selected.active_version)
    {
        return 0U;
    }

    if ((header->firmware_version < g_boot_meta_selected.active_version) &&
        (allow_downgrade == 0U))
    {
        return 0U;
    }

    if ((g_boot_meta_selected.failed_package_version ==
         header->firmware_version) &&
        (g_boot_meta_selected.failed_package_crc32 == header->image_crc32) &&
        (g_boot_meta_selected.failed_package_crc32 != 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t boot_upgrade_offline_image_copy(
    FIL *file,
    uint32_t image_size,
    uint32_t expected_crc32)
{
    uint32_t remaining;
    uint32_t crc;
    UINT bytes_read;
    UINT read_length;
    uint32_t page_index;
    uint32_t page_offset;
    FRESULT result;
    board_internal_flash_status_t flash_status;

    if ((file == NULL) || (image_size == 0U))
    {
        return 0U;
    }

    remaining = image_size;
    crc = 0xFFFFFFFFUL;
    g_boot_upgrade_offline_read_offset = 0U;
    g_boot_upgrade_offline_received_length = 0U;

    while (remaining > 0U)
    {
        read_length = (remaining > BOOT_UPGRADE_OFFLINE_BUFFER_SIZE) ?
            BOOT_UPGRADE_OFFLINE_BUFFER_SIZE : (UINT)remaining;
        bytes_read = 0U;
        result = f_read(
            file,
            s_boot_upgrade_offline_buffer,
            read_length,
            &bytes_read
        );
        g_boot_upgrade_offline_last_fresult = (uint32_t)result;

        if ((result != FR_OK) || (bytes_read != read_length))
        {
            return 0U;
        }

        page_index = g_boot_upgrade_offline_received_length /
                     INTERNAL_FLASH_PAGE_SIZE;
        page_offset = g_boot_upgrade_offline_received_length %
                      INTERNAL_FLASH_PAGE_SIZE;

        flash_status = board_internal_flash_page_program(
            BOARD_INTERNAL_FLASH_REGION_STAGING,
            page_index,
            page_offset,
            s_boot_upgrade_offline_buffer,
            bytes_read
        );
        g_boot_upgrade_offline_staging_status = (uint32_t)flash_status;

        if (flash_status != BOARD_INTERNAL_FLASH_STATUS_OK)
        {
            return 0U;
        }

        crc = boot_upgrade_offline_crc32_update(
            crc,
            s_boot_upgrade_offline_buffer,
            bytes_read
        );
        g_boot_upgrade_offline_received_length += bytes_read;
        g_boot_upgrade_staging_received_length =
            g_boot_upgrade_offline_received_length;
        g_boot_upgrade_offline_read_offset =
            g_boot_upgrade_offline_received_length;
        remaining -= bytes_read;
        fwdgt_counter_reload();
    }

    g_boot_upgrade_offline_calculated_crc32 = crc ^ 0xFFFFFFFFUL;
    return (g_boot_upgrade_offline_calculated_crc32 == expected_crc32) ?
        1U : 0U;
}

static boot_upgrade_offline_file_status_t boot_upgrade_offline_file_validate(
    const char *path,
    uint32_t expected_version,
    uint32_t expected_size,
    uint32_t expected_crc32)
{
    firmware_header_t header;
    uint32_t remaining;
    uint32_t crc;
    UINT bytes_read;
    UINT read_length;
    FRESULT result;
    boot_upgrade_offline_file_status_t status;

    result = f_open(&s_boot_upgrade_offline_file, path, FA_READ);
    g_boot_upgrade_offline_last_fresult = (uint32_t)result;

    if ((result == FR_NO_FILE) || (result == FR_NO_PATH))
    {
        return BOOT_UPGRADE_OFFLINE_FILE_MISSING;
    }

    if (result != FR_OK)
    {
        return BOOT_UPGRADE_OFFLINE_FILE_INVALID;
    }

    status = BOOT_UPGRADE_OFFLINE_FILE_INVALID;

    if ((boot_upgrade_offline_header_read(
             &s_boot_upgrade_offline_file,
             &header) == 0U) ||
        (header.firmware_version != expected_version) ||
        (header.image_size == 0U) ||
        (header.image_size > MAX_IMAGE_SIZE) ||
        ((expected_size != 0U) && (header.image_size != expected_size)) ||
        (header.image_crc32 != expected_crc32) ||
        (f_size(&s_boot_upgrade_offline_file) !=
         ((FSIZE_t)FIRMWARE_HEADER_SIZE + (FSIZE_t)header.image_size)))
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        return status;
    }

    remaining = header.image_size;
    crc = 0xFFFFFFFFUL;

    while (remaining > 0U)
    {
        read_length = (remaining > BOOT_UPGRADE_OFFLINE_BUFFER_SIZE) ?
            BOOT_UPGRADE_OFFLINE_BUFFER_SIZE : (UINT)remaining;
        bytes_read = 0U;
        result = f_read(
            &s_boot_upgrade_offline_file,
            s_boot_upgrade_offline_buffer,
            read_length,
            &bytes_read
        );
        g_boot_upgrade_offline_last_fresult = (uint32_t)result;

        if ((result != FR_OK) || (bytes_read != read_length))
        {
            (void)f_close(&s_boot_upgrade_offline_file);
            return status;
        }

        crc = boot_upgrade_offline_crc32_update(
            crc,
            s_boot_upgrade_offline_buffer,
            bytes_read
        );
        remaining -= bytes_read;
        fwdgt_counter_reload();
    }

    (void)f_close(&s_boot_upgrade_offline_file);
    return ((crc ^ 0xFFFFFFFFUL) == expected_crc32) ?
        BOOT_UPGRADE_OFFLINE_FILE_VALID : status;
}

static boot_upgrade_meta_write_status_t
boot_upgrade_offline_clear_cleanup_context(void)
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
        return BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
    }

    if ((g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (g_boot_meta_scan_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
    }

    active_slot = g_boot_meta_scan_slot;
    active_meta = g_boot_meta_selected;
    next_meta = active_meta;
    next_meta.state = FW_STATE_IDLE;
    next_meta.install_stage = INSTALL_APP_VALID;
    next_meta.pending_size = 0U;
    next_meta.pending_crc32 = 0U;
    next_meta.pending_version = 0U;
    next_meta.failed_package_crc32 = 0U;
    next_meta.failed_package_version = 0U;
    next_meta.upgrade_source = UPGRADE_SOURCE_NONE;
    next_meta.request = UPGRADE_META_REQUEST_NONE;
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        return write_status;
    }

    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if (((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
         (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED)) ||
        (selected_slot != written_slot) ||
        (selected_meta.upgrade_source != UPGRADE_SOURCE_NONE) ||
        (selected_meta.failed_package_crc32 != 0U) ||
        (selected_meta.failed_package_version != 0U))
    {
        return BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;
    return BOOT_UPGRADE_META_WRITE_OK;
}

boot_upgrade_offline_status_t boot_upgrade_offline_startup_process(void)
{
    firmware_header_t header;
    FSIZE_t expected_file_size;
    FRESULT result;
    boot_upgrade_offline_status_t status;
    boot_upgrade_meta_write_status_t meta_status;
    board_internal_flash_status_t staging_status;
    boot_upgrade_end_status_t end_status;
    boot_upgrade_install_status_t install_status;

    g_boot_upgrade_offline_status =
        BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION;
    g_boot_upgrade_offline_file_size = 0U;
    g_boot_upgrade_offline_file_version = 0U;
    g_boot_upgrade_offline_file_crc32 = 0U;
    g_boot_upgrade_offline_read_offset = 0U;
    g_boot_upgrade_offline_received_length = 0U;
    g_boot_upgrade_offline_calculated_crc32 = 0U;
    g_boot_upgrade_offline_staging_status =
        BOARD_INTERNAL_FLASH_STATUS_OK;
    g_boot_upgrade_offline_end_status = 0U;
    g_boot_upgrade_offline_install_status = 0U;

    if (s_boot_upgrade_offline_startup_attempted != 0U)
    {
        return g_boot_upgrade_offline_status;
    }

    s_boot_upgrade_offline_startup_attempted = 1U;

    if ((g_boot_meta_selected.state != FW_STATE_IDLE) ||
        (g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_NONE))
    {
        return g_boot_upgrade_offline_status;
    }

    status = boot_upgrade_offline_mount();
    if (status != BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION)
    {
        return status;
    }

    result = f_open(
        &s_boot_upgrade_offline_file,
        BOOT_UPGRADE_OFFLINE_PACKAGE_PATH,
        FA_READ
    );
    g_boot_upgrade_offline_last_fresult = (uint32_t)result;

    if ((result == FR_NO_FILE) || (result == FR_NO_PATH))
    {
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_FILE_NOT_FOUND;
        return g_boot_upgrade_offline_status;
    }

    if (result != FR_OK)
    {
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_READ_FAILED;
        return g_boot_upgrade_offline_status;
    }

    g_boot_upgrade_offline_file_size = (uint32_t)f_size(
        &s_boot_upgrade_offline_file
    );

    if (boot_upgrade_offline_header_read(
            &s_boot_upgrade_offline_file,
            &header) == 0U)
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_HEADER_INVALID;
        return g_boot_upgrade_offline_status;
    }

    expected_file_size = (FSIZE_t)FIRMWARE_HEADER_SIZE +
                         (FSIZE_t)header.image_size;
    if ((f_size(&s_boot_upgrade_offline_file) != expected_file_size) ||
        (header.image_size == 0U) ||
        (header.image_size > MAX_IMAGE_SIZE))
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_FILE_SIZE_INVALID;
        return g_boot_upgrade_offline_status;
    }

    g_boot_upgrade_offline_file_version = header.firmware_version;
    g_boot_upgrade_offline_file_crc32 = header.image_crc32;

    if (boot_upgrade_offline_version_allowed(&header) == 0U)
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_VERSION_REJECTED;
        return g_boot_upgrade_offline_status;
    }

    meta_status = boot_upgrade_meta_enter_receiving(
        header.image_size,
        header.image_crc32,
        header.firmware_version,
        UPGRADE_SOURCE_TF_OFFLINE
    );

    if (meta_status != BOOT_UPGRADE_META_WRITE_OK)
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_END_FAILED;
        return g_boot_upgrade_offline_status;
    }

    staging_status = boot_upgrade_staging_prepare();
    g_boot_upgrade_offline_staging_status = (uint32_t)staging_status;
    if (staging_status != BOARD_INTERNAL_FLASH_STATUS_OK)
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        (void)boot_upgrade_abort_receiving();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_STAGING_PREPARE_FAILED;
        return g_boot_upgrade_offline_status;
    }

    if (boot_upgrade_offline_image_copy(
            &s_boot_upgrade_offline_file,
            header.image_size,
            header.image_crc32) == 0U)
    {
        (void)f_close(&s_boot_upgrade_offline_file);
        boot_upgrade_offline_unmount();
        (void)boot_upgrade_abort_receiving();
        g_boot_upgrade_offline_status =
            (g_boot_upgrade_offline_received_length == header.image_size) ?
            BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_CRC_INVALID :
            BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_READ_FAILED;
        return g_boot_upgrade_offline_status;
    }

    if (f_close(&s_boot_upgrade_offline_file) != FR_OK)
    {
        boot_upgrade_offline_unmount();
        (void)boot_upgrade_abort_receiving();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_IMAGE_READ_FAILED;
        return g_boot_upgrade_offline_status;
    }

    boot_upgrade_offline_unmount();
    end_status = boot_upgrade_end_accept();
    g_boot_upgrade_offline_end_status = (uint32_t)end_status;

    if (end_status != BOOT_UPGRADE_END_STATUS_OK)
    {
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_END_FAILED;
        return g_boot_upgrade_offline_status;
    }

    install_status = boot_upgrade_install_start();
    g_boot_upgrade_offline_install_status = (uint32_t)install_status;

    if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
    {
        (void)boot_upgrade_abort_receiving();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_INSTALL_START_FAILED;
        return g_boot_upgrade_offline_status;
    }

    g_boot_upgrade_offline_status =
        BOOT_UPGRADE_OFFLINE_STATUS_INSTALL_STARTED;
    return g_boot_upgrade_offline_status;
}

boot_upgrade_offline_status_t boot_upgrade_offline_cleanup_process(void)
{
    char target_path[BOOT_UPGRADE_OFFLINE_TARGET_PATH_MAX];
    const char *extension;
    uint32_t expected_size;
    uint32_t expected_version;
    uint32_t expected_crc32;
    boot_upgrade_offline_file_status_t target_status;
    boot_upgrade_offline_file_status_t package_status;
    boot_upgrade_offline_status_t status;
    boot_upgrade_meta_write_status_t meta_status;
    FRESULT result;

    g_boot_upgrade_offline_status =
        BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION;

    if ((g_boot_meta_selected.state != FW_STATE_IDLE) ||
        (g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_TF_OFFLINE))
    {
        return g_boot_upgrade_offline_status;
    }

    if ((g_boot_meta_selected.failed_package_version != 0U) &&
        (g_boot_meta_selected.failed_package_crc32 != 0U))
    {
        expected_version = g_boot_meta_selected.failed_package_version;
        expected_crc32 = g_boot_meta_selected.failed_package_crc32;
        expected_size = 0U;
        extension = ".failed";
    }
    else if ((g_boot_meta_selected.active_version != 0U) &&
             (g_boot_meta_selected.active_crc32 != 0U) &&
             (g_boot_meta_selected.active_size != 0U))
    {
        expected_version = g_boot_meta_selected.active_version;
        expected_crc32 = g_boot_meta_selected.active_crc32;
        expected_size = g_boot_meta_selected.active_size;
        extension = ".applied";
    }
    else
    {
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
        return g_boot_upgrade_offline_status;
    }

    status = boot_upgrade_offline_mount();
    if (status != BOOT_UPGRADE_OFFLINE_STATUS_NO_ACTION)
    {
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
        return g_boot_upgrade_offline_status;
    }

    if (boot_upgrade_offline_target_path(
            target_path,
            expected_version,
            expected_crc32,
            extension) == 0U)
    {
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
        return g_boot_upgrade_offline_status;
    }

    target_status = boot_upgrade_offline_file_validate(
        target_path,
        expected_version,
        expected_size,
        expected_crc32
    );

    if (target_status == BOOT_UPGRADE_OFFLINE_FILE_INVALID)
    {
        boot_upgrade_offline_unmount();
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
        return g_boot_upgrade_offline_status;
    }

    if (target_status == BOOT_UPGRADE_OFFLINE_FILE_MISSING)
    {
        package_status = boot_upgrade_offline_file_validate(
            BOOT_UPGRADE_OFFLINE_PACKAGE_PATH,
            expected_version,
            expected_size,
            expected_crc32
        );

        if (package_status != BOOT_UPGRADE_OFFLINE_FILE_VALID)
        {
            boot_upgrade_offline_unmount();
            g_boot_upgrade_offline_status =
                BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
            return g_boot_upgrade_offline_status;
        }

        result = f_rename(
            BOOT_UPGRADE_OFFLINE_PACKAGE_PATH,
            target_path
        );
        g_boot_upgrade_offline_last_fresult = (uint32_t)result;

        if (result != FR_OK)
        {
            boot_upgrade_offline_unmount();
            g_boot_upgrade_offline_status =
                BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
            return g_boot_upgrade_offline_status;
        }

        if (boot_upgrade_offline_file_validate(
                target_path,
                expected_version,
                expected_size,
                expected_crc32) != BOOT_UPGRADE_OFFLINE_FILE_VALID)
        {
            boot_upgrade_offline_unmount();
            g_boot_upgrade_offline_status =
                BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
            return g_boot_upgrade_offline_status;
        }
    }
    else
    {
        package_status = boot_upgrade_offline_file_validate(
            BOOT_UPGRADE_OFFLINE_PACKAGE_PATH,
            expected_version,
            expected_size,
            expected_crc32
        );

        if (package_status == BOOT_UPGRADE_OFFLINE_FILE_INVALID)
        {
            boot_upgrade_offline_unmount();
            g_boot_upgrade_offline_status =
                BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
            return g_boot_upgrade_offline_status;
        }

        if (package_status == BOOT_UPGRADE_OFFLINE_FILE_VALID)
        {
            result = f_unlink(BOOT_UPGRADE_OFFLINE_PACKAGE_PATH);
            g_boot_upgrade_offline_last_fresult = (uint32_t)result;

            if (result != FR_OK)
            {
                boot_upgrade_offline_unmount();
                g_boot_upgrade_offline_status =
                    BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
                return g_boot_upgrade_offline_status;
            }
        }
    }

    meta_status = boot_upgrade_offline_clear_cleanup_context();
    boot_upgrade_offline_unmount();

    if (meta_status != BOOT_UPGRADE_META_WRITE_OK)
    {
        g_boot_upgrade_offline_status =
            BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_BLOCKED;
        return g_boot_upgrade_offline_status;
    }

    g_boot_upgrade_offline_status =
        BOOT_UPGRADE_OFFLINE_STATUS_CLEANUP_DONE;
    return g_boot_upgrade_offline_status;
}
