#ifndef BOOT_UPGRADE_DATA_H
#define BOOT_UPGRADE_DATA_H

#include <stdint.h>

#include "board_internal_flash.h"
#include "upgrade_serialization.h"

typedef enum
{
    BOOT_UPGRADE_DATA_STATUS_OK = 0,
    BOOT_UPGRADE_DATA_STATUS_DUPLICATE,
    BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT,
    BOOT_UPGRADE_DATA_STATUS_STATE_NOT_ALLOWED,
    BOOT_UPGRADE_DATA_STATUS_LENGTH_INVALID,
    BOOT_UPGRADE_DATA_STATUS_CHUNK_CRC_INVALID,
    BOOT_UPGRADE_DATA_STATUS_SEQUENCE_NOT_CONTIGUOUS,
    BOOT_UPGRADE_DATA_STATUS_OFFSET_INVALID,
    BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED
} boot_upgrade_data_status_t;

/*
 * 接收并提交一片 DATA。
 * 成功或重复片时 sequence_out 返回本片序号；
 * 参数/长度不足时返回 0。
 */
boot_upgrade_data_status_t boot_upgrade_data_accept(
    const upgrade_meta_t *selected_meta,
    const uint8_t *payload,
    uint16_t payload_length,
    uint16_t *sequence_out
);

/* BEGIN 完成 Staging 擦除后，清除本轮 DATA 的 RAM 会话缓存。 */
void boot_upgrade_data_session_reset(void);

extern volatile boot_upgrade_data_status_t g_boot_upgrade_data_status;
extern volatile uint16_t g_boot_upgrade_data_sequence;
extern volatile uint32_t g_boot_upgrade_data_offset;
extern volatile uint16_t g_boot_upgrade_data_chunk_length;
extern volatile uint16_t g_boot_upgrade_data_received_crc16;
extern volatile uint16_t g_boot_upgrade_data_calculated_crc16;
extern volatile board_internal_flash_status_t g_boot_upgrade_data_flash_status;
extern volatile uint32_t g_boot_upgrade_data_write_fail_count;
extern volatile uint32_t g_boot_upgrade_data_duplicate_count;
extern volatile uint8_t g_boot_upgrade_data_write_failed;

#endif /* BOOT_UPGRADE_DATA_H */
