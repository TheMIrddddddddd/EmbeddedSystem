#include "app_upgrade_enter_boot.h"

volatile app_upgrade_enter_boot_status_t g_app_upgrade_enter_boot_status =
    APP_UPGRADE_ENTER_BOOT_STATUS_INVALID_ARGUMENT;
volatile boot_upgrade_meta_select_status_t
    g_app_upgrade_enter_boot_select_status =
    BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;
volatile boot_upgrade_meta_write_status_t
    g_app_upgrade_enter_boot_meta_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile boot_upgrade_meta_slot_t g_app_upgrade_enter_boot_written_slot =
    BOOT_UPGRADE_META_SLOT_NONE;
volatile uint8_t g_app_upgrade_enter_boot_committed;

app_upgrade_enter_boot_status_t app_upgrade_enter_boot_execute(void)
{
    upgrade_meta_t selected_meta;
    upgrade_meta_t next_meta;
    upgrade_meta_t committed_meta;
    upgrade_meta_t rescanned_meta;
    boot_upgrade_meta_slot_t selected_slot;
    boot_upgrade_meta_slot_t written_slot;
    boot_upgrade_meta_slot_t rescanned_slot;
    boot_upgrade_meta_select_status_t select_status;
    boot_upgrade_meta_write_status_t write_status;

    g_app_upgrade_enter_boot_status =
        APP_UPGRADE_ENTER_BOOT_STATUS_INVALID_ARGUMENT;
    g_app_upgrade_enter_boot_select_status =
        BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;
    g_app_upgrade_enter_boot_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_app_upgrade_enter_boot_written_slot = BOOT_UPGRADE_META_SLOT_NONE;
    g_app_upgrade_enter_boot_committed = 0U;

    selected_slot = BOOT_UPGRADE_META_SLOT_NONE;
    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );
    g_app_upgrade_enter_boot_select_status = select_status;

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_META_SELECT_FAILED;
        return g_app_upgrade_enter_boot_status;
    }

    if ((selected_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (selected_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        return g_app_upgrade_enter_boot_status;
    }

    if (selected_meta.upgrade_source == UPGRADE_SOURCE_TF_OFFLINE)
    {
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_TF_CLEANUP_PENDING;
        return g_app_upgrade_enter_boot_status;
    }

    if ((selected_meta.state != FW_STATE_IDLE) ||
        (selected_meta.upgrade_source != UPGRADE_SOURCE_NONE))
    {
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_STATE_NOT_ALLOWED;
        return g_app_upgrade_enter_boot_status;
    }

    /* 重复请求在复位窗口内保持幂等，不再额外擦写 Meta。 */
    if (selected_meta.request == UPGRADE_META_REQUEST_ENTER_BOOT)
    {
        g_app_upgrade_enter_boot_committed = 1U;
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_OK;
        return g_app_upgrade_enter_boot_status;
    }

    next_meta = selected_meta;
    next_meta.request = UPGRADE_META_REQUEST_ENTER_BOOT;
    written_slot = BOOT_UPGRADE_META_SLOT_NONE;

    write_status = boot_upgrade_meta_update(
        selected_slot,
        &selected_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );
    g_app_upgrade_enter_boot_meta_status = write_status;
    g_app_upgrade_enter_boot_written_slot = written_slot;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == selected_slot))
    {
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_META_UPDATE_FAILED;
        return g_app_upgrade_enter_boot_status;
    }

    select_status = boot_upgrade_meta_select(
        &rescanned_meta,
        &rescanned_slot
    );
    g_app_upgrade_enter_boot_select_status = select_status;

    if (((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
         (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED)) ||
        (rescanned_slot != written_slot) ||
        (rescanned_meta.request != UPGRADE_META_REQUEST_ENTER_BOOT) ||
        (rescanned_meta.state != FW_STATE_IDLE) ||
        (rescanned_meta.upgrade_source != UPGRADE_SOURCE_NONE))
    {
        g_app_upgrade_enter_boot_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        g_app_upgrade_enter_boot_status =
            APP_UPGRADE_ENTER_BOOT_STATUS_META_UPDATE_FAILED;
        return g_app_upgrade_enter_boot_status;
    }

    g_app_upgrade_enter_boot_committed = 1U;
    g_app_upgrade_enter_boot_status =
        APP_UPGRADE_ENTER_BOOT_STATUS_OK;
    return g_app_upgrade_enter_boot_status;
}
