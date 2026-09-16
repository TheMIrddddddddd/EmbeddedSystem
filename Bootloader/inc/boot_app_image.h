#ifndef BOOT_APP_IMAGE_H
#define BOOT_APP_IMAGE_H

#include <stdint.h>

#include "upgrade_serialization.h"

typedef enum
{
    BOOT_APP_IMAGE_STATUS_OK = 0,
    BOOT_APP_IMAGE_STATUS_INVALID_ARGUMENT,
    BOOT_APP_IMAGE_STATUS_MANIFEST_INVALID,
    BOOT_APP_IMAGE_STATUS_MSP_INVALID,
    BOOT_APP_IMAGE_STATUS_RESET_INVALID,
    BOOT_APP_IMAGE_STATUS_IMAGE_CRC_INVALID
} boot_app_image_status_t;

typedef struct
{
    image_manifest_t manifest;
    uint32_t msp;
    uint32_t reset_handler;
} boot_app_image_info_t;

boot_app_image_status_t boot_app_image_validate(boot_app_image_info_t *info);

extern volatile boot_app_image_status_t g_boot_app_image_status;
extern volatile image_manifest_t g_boot_app_manifest;
extern volatile uint32_t g_boot_app_msp;
extern volatile uint32_t g_boot_app_reset_handler;

#endif /* BOOT_APP_IMAGE_H */
