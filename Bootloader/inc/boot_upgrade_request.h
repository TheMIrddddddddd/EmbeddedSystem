#ifndef BOOT_UPGRADE_REQUEST_H
#define BOOT_UPGRADE_REQUEST_H

#include <stdint.h>

#include "boot_upgrade_meta.h"

typedef enum
{
    BOOT_UPGRADE_REQUEST_STATUS_NO_ACTION = 0,
    BOOT_UPGRADE_REQUEST_STATUS_CONSUMED,
    BOOT_UPGRADE_REQUEST_STATUS_INVALID_CONTEXT,
    BOOT_UPGRADE_REQUEST_STATUS_STATE_NOT_ALLOWED,
    BOOT_UPGRADE_REQUEST_STATUS_META_UPDATE_FAILED
} boot_upgrade_request_status_t;

/* 启动早期消费 App 写入的 ENTER_BOOT 请求。 */
boot_upgrade_request_status_t boot_upgrade_request_consume(void);

extern volatile boot_upgrade_request_status_t g_boot_upgrade_request_status;
extern volatile boot_upgrade_meta_write_status_t
    g_boot_upgrade_request_meta_status;
extern volatile boot_upgrade_meta_slot_t g_boot_upgrade_request_written_slot;
extern volatile uint8_t g_boot_upgrade_request_consumed;

#endif /* BOOT_UPGRADE_REQUEST_H */
