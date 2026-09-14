#include "boot_upgrade_meta.h"

volatile boot_upgrade_meta_slot_status_t g_boot_upgrade_meta_slot_a_status = BOOT_UPGRADE_META_SLOT_INVALID_ARGUMENT;

volatile boot_upgrade_meta_slot_status_t g_boot_upgrade_meta_slot_b_status = BOOT_UPGRADE_META_SLOT_INVALID_ARGUMENT;

volatile boot_upgrade_meta_slot_t g_boot_upgrade_meta_selected_slot = BOOT_UPGRADE_META_SLOT_NONE;

volatile boot_upgrade_meta_select_status_t g_boot_upgrade_meta_select_status = BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;

volatile boot_upgrade_meta_write_status_t g_boot_upgrade_meta_write_status = BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;

volatile boot_upgrade_meta_slot_t g_boot_upgrade_meta_written_slot = BOOT_UPGRADE_META_SLOT_NONE;


static int boot_upgrade_meta_get_offset(boot_upgrade_meta_slot_t slot, uint32_t *offset)
{
    if (offset == 0)
    {
        return 0;
    }

    switch (slot)
    {
        case BOOT_UPGRADE_META_SLOT_A:
            *offset = GD25Q40E_UPGRADE_META_SLOT_A_OFFSET;
            return 1;

        case BOOT_UPGRADE_META_SLOT_B:
            *offset = GD25Q40E_UPGRADE_META_SLOT_B_OFFSET;
            return 1;

        default:
            return 0;
    }
}

static int boot_upgrade_meta_get_inactive_slot(boot_upgrade_meta_slot_t active_slot, boot_upgrade_meta_slot_t *target_slot)
{
    if (target_slot == 0)
    {
        return 0;
    }

    switch (active_slot)
    {
        case BOOT_UPGRADE_META_SLOT_NONE:
            /*
             * 两个槽都没有有效记录时，
             * 固定从 Slot A 开始。
             */
            *target_slot = BOOT_UPGRADE_META_SLOT_A;
            return 1;

        case BOOT_UPGRADE_META_SLOT_A:
            *target_slot = BOOT_UPGRADE_META_SLOT_B;
            return 1;

        case BOOT_UPGRADE_META_SLOT_B:
            *target_slot = BOOT_UPGRADE_META_SLOT_A;
            return 1;

        default:
            return 0;
    }
}

static int boot_upgrade_meta_bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t length)
{
    uint32_t index;

    if ((left == 0) || (right == 0))
    {
        return 0;
    }

    for (index = 0U; index < length; index++)
    {
        if (left[index] != right[index])
        {
            return 0;
        }
    }

    return 1;
}

static int boot_upgrade_meta_is_erased(const uint8_t *buffer, uint32_t length)
{
    uint32_t index;

    if (buffer == 0)
    {
        return 0;
    }

    for (index = 0U; index < length; index++)
    {
        if (buffer[index] != 0xFFU)
        {
            return 0;
        }
    }

    return 1;
}


boot_upgrade_meta_slot_status_t boot_upgrade_meta_read_slot(boot_upgrade_meta_slot_t slot, upgrade_meta_t *meta)
{
    uint32_t offset;
    uint8_t raw[UPGRADE_META_SIZE];

    if (meta == 0)
    {
        return BOOT_UPGRADE_META_SLOT_INVALID_ARGUMENT;
    }

    if (boot_upgrade_meta_get_offset(slot, &offset) == 0)
    {
        return BOOT_UPGRADE_META_SLOT_INVALID_ARGUMENT;
    }

    if (board_spi_flash_read(offset, raw, UPGRADE_META_SIZE) == 0)
    {
        return BOOT_UPGRADE_META_SLOT_IO_ERROR;
    }

    if (boot_upgrade_meta_is_erased(raw, UPGRADE_META_SIZE) != 0)
    {
        return BOOT_UPGRADE_META_SLOT_EMPTY;
    }

    if (upgrade_meta_decode(raw, UPGRADE_META_SIZE, meta) != FW_FORMAT_STATUS_OK)
    {
        return BOOT_UPGRADE_META_SLOT_INVALID_RECORD;
    }

    return BOOT_UPGRADE_META_SLOT_VALID;
}

boot_upgrade_meta_select_status_t boot_upgrade_meta_select(upgrade_meta_t *selected_meta, boot_upgrade_meta_slot_t *selected_slot)
{
    upgrade_meta_t meta_a;
    upgrade_meta_t meta_b;
    boot_upgrade_meta_slot_status_t status_a;
    boot_upgrade_meta_slot_status_t status_b;
    boot_upgrade_meta_select_status_t select_status;

    if ((selected_meta == 0) || (selected_slot == 0))
    {
        g_boot_upgrade_meta_select_status = BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;

        return BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;
    }

    *selected_slot = BOOT_UPGRADE_META_SLOT_NONE;
    g_boot_upgrade_meta_selected_slot = BOOT_UPGRADE_META_SLOT_NONE;

    status_a = boot_upgrade_meta_read_slot(BOOT_UPGRADE_META_SLOT_A, &meta_a);
    status_b = boot_upgrade_meta_read_slot(BOOT_UPGRADE_META_SLOT_B, &meta_b);
    g_boot_upgrade_meta_slot_a_status = status_a;
    g_boot_upgrade_meta_slot_b_status = status_b;

    if ((status_a == BOOT_UPGRADE_META_SLOT_VALID) && (status_b == BOOT_UPGRADE_META_SLOT_VALID))
    {
        if (meta_b.generation > meta_a.generation)
        {
            *selected_meta = meta_b;
            *selected_slot = BOOT_UPGRADE_META_SLOT_B;
        }
        else
        {
            /*
             * generation 相等时固定选择 A，
             * 保证选择规则确定。
             */
            *selected_meta = meta_a;
            *selected_slot = BOOT_UPGRADE_META_SLOT_A;
        }

        select_status = BOOT_UPGRADE_META_SELECT_OK;
    }
    else if (status_a == BOOT_UPGRADE_META_SLOT_VALID)
    {
        *selected_meta = meta_a;
        *selected_slot = BOOT_UPGRADE_META_SLOT_A;

        select_status = BOOT_UPGRADE_META_SELECT_OK_DEGRADED;
    }
    else if (status_b == BOOT_UPGRADE_META_SLOT_VALID)
    {
        *selected_meta = meta_b;
        *selected_slot = BOOT_UPGRADE_META_SLOT_B;

        select_status = BOOT_UPGRADE_META_SELECT_OK_DEGRADED;
    }
    else
    {
        /*
         * 没有有效槽时，IO_ERROR 不能伪装成空槽。
         */
        if ((status_a == BOOT_UPGRADE_META_SLOT_IO_ERROR) || (status_b == BOOT_UPGRADE_META_SLOT_IO_ERROR))
        {
            select_status = BOOT_UPGRADE_META_SELECT_EXTERNAL_IO_ERROR;
        }
        else
        {
            select_status = BOOT_UPGRADE_META_SELECT_NO_VALID_SLOT;
        }
    }

    g_boot_upgrade_meta_selected_slot = *selected_slot;
    g_boot_upgrade_meta_select_status = select_status;

    return select_status;
}

boot_upgrade_meta_write_status_t boot_upgrade_meta_write_inactive(boot_upgrade_meta_slot_t active_slot, const upgrade_meta_t *meta, boot_upgrade_meta_slot_t *written_slot)
{
    uint32_t offset;
    uint8_t raw[UPGRADE_META_SIZE];
    uint8_t verify[UPGRADE_META_SIZE];
    upgrade_meta_t decoded_meta;
    boot_upgrade_meta_slot_t target_slot;
    boot_upgrade_meta_write_status_t status;

    target_slot = BOOT_UPGRADE_META_SLOT_NONE;
    status = BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_meta_written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    if (written_slot != 0)
    {
        *written_slot = BOOT_UPGRADE_META_SLOT_NONE;
    }

    if ((meta == 0) || (written_slot == 0))
    {
        goto finish;
    }

    if (boot_upgrade_meta_get_inactive_slot(active_slot, &target_slot) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
        goto finish;
    }

    if (boot_upgrade_meta_get_offset(target_slot, &offset) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_INVALID_SLOT;
        goto finish;
    }
    /*
     * encode 会检查 magic、meta_version、state、
     * install_stage、request 和 commit_marker。
     */
    if (upgrade_meta_encode(meta, raw, sizeof(raw)) != FW_FORMAT_STATUS_OK)
    {
        status = BOOT_UPGRADE_META_WRITE_ENCODE_FAILED;
        goto finish;
    }

    /*
     * 第一步：擦除目标 4 KB sector。
     * active_slot 不会被擦除。
     */
    if (board_spi_flash_sector_erase(offset) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_ERASE_FAILED;
        goto finish;
    }

    /*
     * 只检查记录所在的前 72 字节已经擦除。
     */
    if ((board_spi_flash_read(offset, verify, UPGRADE_META_SIZE) == 0) ||
        (boot_upgrade_meta_is_erased(verify, UPGRADE_META_SIZE) == 0))
    {
        status = BOOT_UPGRADE_META_WRITE_ERASE_VERIFY_FAILED;
        goto finish;
    }
    
    /*
     * 第二步：写入字段区 0 ~ 63。
     * 此时 CRC 和 commit_marker 仍未写入。
     */
    if (board_spi_flash_page_program(offset, raw, UPGRADE_META_CRC32_OFFSET) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_FIELDS_PROGRAM_FAILED;
        goto finish;
    }

    if ((board_spi_flash_read(offset, verify, UPGRADE_META_CRC32_OFFSET) == 0) ||
        (boot_upgrade_meta_bytes_equal(raw, verify, UPGRADE_META_CRC32_OFFSET) == 0))
    {
        status = BOOT_UPGRADE_META_WRITE_FIELDS_VERIFY_FAILED;
        goto finish;
    }

    /*
     * 第三步：单独写 CRC32。
     * 写入范围为 64 ~ 67。
     */
    if (board_spi_flash_page_program(offset + UPGRADE_META_CRC32_OFFSET, &raw[UPGRADE_META_CRC32_OFFSET], UPGRADE_META_COMMIT_MARKER_OFFSET - UPGRADE_META_CRC32_OFFSET) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_CRC_PROGRAM_FAILED;
        goto finish;
    }

    if ((board_spi_flash_read(offset, verify, UPGRADE_META_COMMIT_MARKER_OFFSET) == 0) ||
        (boot_upgrade_meta_bytes_equal(raw, verify, UPGRADE_META_COMMIT_MARKER_OFFSET) == 0))
    {
        status = BOOT_UPGRADE_META_WRITE_CRC_VERIFY_FAILED;
        goto finish;
    }

    /*
     * 第四步：最后单独写 commit_marker。
     * 写入范围为 68 ~ 71。
     */
    if (board_spi_flash_page_program(offset + UPGRADE_META_COMMIT_MARKER_OFFSET, &raw[UPGRADE_META_COMMIT_MARKER_OFFSET], UPGRADE_META_SIZE - UPGRADE_META_COMMIT_MARKER_OFFSET) == 0)
    {
        status = BOOT_UPGRADE_META_WRITE_MARKER_PROGRAM_FAILED;
        goto finish;
    }

    /*
     * 第五步：整条记录读回。
     */
    if ((board_spi_flash_read(offset, verify, UPGRADE_META_SIZE) == 0) ||
        (boot_upgrade_meta_bytes_equal(raw, verify, UPGRADE_META_SIZE) == 0) ||
        (upgrade_meta_decode(verify, UPGRADE_META_SIZE, &decoded_meta) != FW_FORMAT_STATUS_OK))
    {
        status = BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        goto finish;
    }

    *written_slot = target_slot;
    g_boot_upgrade_meta_written_slot = target_slot;
    status = BOOT_UPGRADE_META_WRITE_OK;

finish:
    g_boot_upgrade_meta_write_status = status;
    return status;
}
