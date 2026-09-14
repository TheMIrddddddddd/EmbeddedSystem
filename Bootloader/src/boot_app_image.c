#include "boot_app_image.h"

#include "gd32f4xx.h"
#include "common_crc.h"
#include "common_flash_layout.h"

volatile boot_app_image_status_t g_boot_app_image_status = BOOT_APP_IMAGE_STATUS_INVALID_ARGUMENT;
volatile image_manifest_t g_boot_app_manifest;
volatile uint32_t g_boot_app_msp;
volatile uint32_t g_boot_app_reset_handler;

static boot_app_image_status_t boot_app_image_status_finish(boot_app_image_info_t *info, boot_app_image_status_t status)
{
    if (info != 0)
    {
        g_boot_app_manifest = info->manifest;
        g_boot_app_msp = info->msp;
        g_boot_app_reset_handler = info->reset_handler;
    }

    g_boot_app_image_status = status;

    return status;
}

boot_app_image_status_t boot_app_image_validate(boot_app_image_info_t *info)
{
    uint8_t manifest_raw[IMAGE_MANIFEST_SIZE];
    uint32_t index;
    uint32_t reset_address;
    uint32_t image_end;
    uint32_t calculated_crc;
    fw_format_status_t manifest_status;

    if (info == 0)
    {
        return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_INVALID_ARGUMENT);
    }

    info->msp = 0U;
    info->reset_handler = 0U;

    for (index = 0U; index < IMAGE_MANIFEST_SIZE; index++)
    {
        manifest_raw[index] = *(volatile const uint8_t *) (APP_MANIFEST_ADDR + index);
    }

    manifest_status = image_manifest_decode(manifest_raw, IMAGE_MANIFEST_SIZE, &info->manifest);

    if (manifest_status != FW_FORMAT_STATUS_OK)
    {
        return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_MANIFEST_INVALID);
    }

    info->msp = *(volatile const uint32_t *)APP_BASE;
    info->reset_handler = *(volatile const uint32_t *)(APP_BASE + 4U);

    if ((info->msp < (SRAM_BASE + 8U)) || (info->msp > SRAM_TOP) || ((info->msp & 0x7U) != 0U))
    {
        return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_MSP_INVALID);
    }

    reset_address = info->reset_handler & ~1UL;
    image_end = APP_BASE + info->manifest.image_size;

    if ((reset_address < APP_BASE) || (reset_address >= image_end) || (reset_address >= APP_MANIFEST_ADDR) || ((info->reset_handler & 1U) == 0U))
    {
        return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_RESET_INVALID);
    }

    calculated_crc = common_crc32_calc((const uint8_t *)APP_BASE, info->manifest.image_size);

    if (calculated_crc != info->manifest.image_crc32)
    {
        return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_IMAGE_CRC_INVALID);
    }

    return boot_app_image_status_finish(info, BOOT_APP_IMAGE_STATUS_OK);
}
