#include "boot_upgrade_staging.h"

#include "common_flash_layout.h"
#include "gd32f4xx_fwdgt.h"
#include "boot_upgrade_data.h"

volatile uint32_t g_boot_upgrade_staging_prepare_page_index;
volatile uint32_t g_boot_upgrade_staging_prepare_completed_pages;
volatile board_internal_flash_status_t g_boot_upgrade_staging_prepare_status;
volatile uint32_t g_boot_upgrade_staging_received_length;
volatile uint16_t g_boot_upgrade_staging_expected_sequence;

board_internal_flash_status_t boot_upgrade_staging_prepare(void)
{
    uint32_t page_index;
    board_internal_flash_status_t status;

    g_boot_upgrade_staging_prepare_page_index = 0U;
    g_boot_upgrade_staging_prepare_completed_pages = 0U;
    g_boot_upgrade_staging_prepare_status = BOARD_INTERNAL_FLASH_STATUS_OK;

    /* 新一轮 BEGIN 从空 Staging 和初始序号开始。 */
    g_boot_upgrade_staging_received_length = 0U;
    g_boot_upgrade_staging_expected_sequence = 0U;
    boot_upgrade_data_session_reset();

    /* STAGING_MANIFEST_ADDR 所在页在这里且只擦除一次。 */
    for (page_index = 0U; page_index < STAGING_PAGE_COUNT; page_index++)
    {
        g_boot_upgrade_staging_prepare_page_index = page_index;

        status = board_internal_flash_page_erase(
            BOARD_INTERNAL_FLASH_REGION_STAGING,
            page_index
        );

        if (status != BOARD_INTERNAL_FLASH_STATUS_OK)
        {
            g_boot_upgrade_staging_prepare_status = status;
            return status;
        }

        g_boot_upgrade_staging_prepare_completed_pages++;
        fwdgt_counter_reload();
    }

    g_boot_upgrade_staging_prepare_page_index = STAGING_PAGE_COUNT;
    g_boot_upgrade_staging_prepare_status = BOARD_INTERNAL_FLASH_STATUS_OK;

    return BOARD_INTERNAL_FLASH_STATUS_OK;
}
