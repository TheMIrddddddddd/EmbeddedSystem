#include "app_upgrade_confirm.h"

volatile app_upgrade_confirm_status_t g_app_upgrade_confirm_status =
    APP_UPGRADE_CONFIRM_STATUS_NO_ACTION;
volatile boot_upgrade_meta_select_status_t g_app_upgrade_confirm_select_status =
    BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;
volatile boot_upgrade_meta_write_status_t g_app_upgrade_confirm_meta_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile boot_upgrade_meta_slot_t g_app_upgrade_confirm_written_slot =
    BOOT_UPGRADE_META_SLOT_NONE;
volatile uint8_t g_app_upgrade_confirm_committed;
volatile uint8_t g_app_upgrade_confirm_trial_detected;

static void app_upgrade_confirm_set_status(
    app_upgrade_confirm_status_t status)
{
    g_app_upgrade_confirm_status = status;
}

app_upgrade_confirm_status_t app_upgrade_confirm_execute(void)
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

    g_app_upgrade_confirm_status = APP_UPGRADE_CONFIRM_STATUS_INVALID_ARGUMENT;
    g_app_upgrade_confirm_select_status =
        BOOT_UPGRADE_META_SELECT_INVALID_ARGUMENT;
    g_app_upgrade_confirm_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_app_upgrade_confirm_written_slot = BOOT_UPGRADE_META_SLOT_NONE;
    g_app_upgrade_confirm_committed = 0U;
    g_app_upgrade_confirm_trial_detected = 0U;

    selected_slot = BOOT_UPGRADE_META_SLOT_NONE;
    select_status = boot_upgrade_meta_select(
        &selected_meta,
        &selected_slot
    );
    g_app_upgrade_confirm_select_status = select_status;

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        app_upgrade_confirm_set_status(
            APP_UPGRADE_CONFIRM_STATUS_META_SELECT_FAILED
        );
        return APP_UPGRADE_CONFIRM_STATUS_META_SELECT_FAILED;
    }

    if ((selected_slot != BOOT_UPGRADE_META_SLOT_A) &&
        (selected_slot != BOOT_UPGRADE_META_SLOT_B))
    {
        app_upgrade_confirm_set_status(
            APP_UPGRADE_CONFIRM_STATUS_INVALID_ARGUMENT
        );
        return APP_UPGRADE_CONFIRM_STATUS_INVALID_ARGUMENT;
    }

    if (selected_meta.state != FW_STATE_TRIAL_PENDING)
    {
        app_upgrade_confirm_set_status(APP_UPGRADE_CONFIRM_STATUS_NO_ACTION);
        return APP_UPGRADE_CONFIRM_STATUS_NO_ACTION;
    }

    g_app_upgrade_confirm_trial_detected = 1U;

    next_meta = selected_meta;
    next_meta.state = FW_STATE_CONFIRMED;
    next_meta.install_stage = INSTALL_APP_VALID;
    next_meta.failure_count = 0U;

    written_slot = BOOT_UPGRADE_META_SLOT_NONE;
    write_status = boot_upgrade_meta_update(
        selected_slot,
        &selected_meta,
        &next_meta,
        &committed_meta,
        &written_slot
    );
    g_app_upgrade_confirm_meta_status = write_status;
    g_app_upgrade_confirm_written_slot = written_slot;

    if ((write_status != BOOT_UPGRADE_META_WRITE_OK) ||
        (written_slot == BOOT_UPGRADE_META_SLOT_NONE) ||
        (written_slot == selected_slot))
    {
        app_upgrade_confirm_set_status(
            APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED
        );
        return APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED;
    }

    select_status = boot_upgrade_meta_select(
        &rescanned_meta,
        &rescanned_slot
    );
    g_app_upgrade_confirm_select_status = select_status;

    if ((select_status != BOOT_UPGRADE_META_SELECT_OK) &&
        (select_status != BOOT_UPGRADE_META_SELECT_OK_DEGRADED))
    {
        g_app_upgrade_confirm_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        app_upgrade_confirm_set_status(
            APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED
        );
        return APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED;
    }

    if ((rescanned_slot != written_slot) ||
        (rescanned_meta.state != FW_STATE_CONFIRMED) ||
        (rescanned_meta.pending_size != selected_meta.pending_size) ||
        (rescanned_meta.pending_crc32 != selected_meta.pending_crc32) ||
        (rescanned_meta.pending_version != selected_meta.pending_version))
    {
        g_app_upgrade_confirm_meta_status =
            BOOT_UPGRADE_META_WRITE_COMMIT_VERIFY_FAILED;
        app_upgrade_confirm_set_status(
            APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED
        );
        return APP_UPGRADE_CONFIRM_STATUS_META_UPDATE_FAILED;
    }

    g_app_upgrade_confirm_committed = 1U;
    app_upgrade_confirm_set_status(APP_UPGRADE_CONFIRM_STATUS_OK);
    return APP_UPGRADE_CONFIRM_STATUS_OK;
}
