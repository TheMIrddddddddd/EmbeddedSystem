#include "boot_upgrade_request.h"

#include "boot_upgrade_context.h"

volatile boot_upgrade_request_status_t g_boot_upgrade_request_status =
    BOOT_UPGRADE_REQUEST_STATUS_NO_ACTION;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_request_meta_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile boot_upgrade_meta_slot_t g_boot_upgrade_request_written_slot =
    BOOT_UPGRADE_META_SLOT_NONE;
volatile uint8_t g_boot_upgrade_request_consumed;

static uint8_t boot_upgrade_request_context_valid(void)
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

boot_upgrade_request_status_t boot_upgrade_request_consume(void)
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

    g_boot_upgrade_request_status =
        BOOT_UPGRADE_REQUEST_STATUS_NO_ACTION;
    g_boot_upgrade_request_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_request_written_slot = BOOT_UPGRADE_META_SLOT_NONE;
    g_boot_upgrade_request_consumed = 0U;

    if (boot_upgrade_request_context_valid() == 0U)
    {
        g_boot_upgrade_request_status =
            BOOT_UPGRADE_REQUEST_STATUS_INVALID_CONTEXT;
        return g_boot_upgrade_request_status;
    }

    if (g_boot_meta_selected.request != UPGRADE_META_REQUEST_ENTER_BOOT)
    {
        return g_boot_upgrade_request_status;
    }

    if ((g_boot_meta_selected.state != FW_STATE_IDLE) ||
        (g_boot_meta_selected.upgrade_source != UPGRADE_SOURCE_NONE))
    {
        g_boot_upgrade_request_status =
            BOOT_UPGRADE_REQUEST_STATUS_STATE_NOT_ALLOWED;
        return g_boot_upgrade_request_status;
    }

    active_slot = g_boot_meta_scan_slot;
    active_meta = g_boot_meta_selected;
    next_meta = active_meta;
    next_meta.request = UPGRADE_META_REQUEST_NONE;
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        active_slot,
        &active_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );
    g_boot_upgrade_request_meta_status = write_status;
    g_boot_upgrade_request_written_slot = written_slot;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == active_slot))
    {
        g_boot_upgrade_request_status =
            BOOT_UPGRADE_REQUEST_STATUS_META_UPDATE_FAILED;
        return g_boot_upgrade_request_status;
    }

    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );

    if (((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
         (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED)) ||
        (selected_slot != written_slot) ||
        (selected_meta.request != UPGRADE_META_REQUEST_NONE))
    {
        g_boot_upgrade_request_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        g_boot_upgrade_request_status =
            BOOT_UPGRADE_REQUEST_STATUS_META_UPDATE_FAILED;
        return g_boot_upgrade_request_status;
    }

    g_boot_meta_selected = selected_meta;
    g_boot_meta_scan_slot = selected_slot;
    g_boot_meta_scan_status = select_status;
    g_boot_upgrade_request_consumed = 1U;
    g_boot_upgrade_request_status =
        BOOT_UPGRADE_REQUEST_STATUS_CONSUMED;

    return g_boot_upgrade_request_status;
}
