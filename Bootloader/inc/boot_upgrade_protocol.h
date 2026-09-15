#ifndef BOOT_UPGRADE_PROTOCOL_H
#define BOOT_UPGRADE_PROTOCOL_H

#include <stdint.h>

#include "boot_upgrade_begin.h"
#include "boot_upgrade_install.h"
#include "boot_upgrade_meta.h"
#include "boot_upgrade_state.h"
#include "protocol_frame.h"

#define BOOT_UPGRADE_PROTOCOL_CMD_ENTER_BOOT      0x0500U
#define BOOT_UPGRADE_PROTOCOL_CMD_BEGIN          0x0501U
#define BOOT_UPGRADE_PROTOCOL_CMD_DATA           0x0502U
#define BOOT_UPGRADE_PROTOCOL_CMD_END            0x0503U
#define BOOT_UPGRADE_PROTOCOL_CMD_INSTALL        0x0504U
#define BOOT_UPGRADE_PROTOCOL_CMD_ABORT          0x0505U
#define BOOT_UPGRADE_PROTOCOL_BROADCAST_ADDRESS  0xFFFFU
#define BOOT_UPGRADE_PROTOCOL_RESPONSE_OK        0xFFU
#define BOOT_UPGRADE_PROTOCOL_ERROR_CRC          0x01U
#define BOOT_UPGRADE_PROTOCOL_ERROR_LENGTH       0x02U
#define BOOT_UPGRADE_PROTOCOL_ERROR_COMMAND      0x03U
#define BOOT_UPGRADE_PROTOCOL_ERROR_DATA_CRC     0x61U
#define BOOT_UPGRADE_PROTOCOL_ERROR_DATA_LENGTH  0x62U
#define BOOT_UPGRADE_PROTOCOL_ERROR_DATA_OFFSET  0x63U
#define BOOT_UPGRADE_PROTOCOL_ERROR_DATA_SEQUENCE 0x64U
#define BOOT_UPGRADE_PROTOCOL_ERROR_INVALID_HDR  0x60U
#define BOOT_UPGRADE_PROTOCOL_ERROR_STATE        0x66U
#define BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE      0x67U

/* Boot 无 App 配置上下文，Ready 事件使用保留地址并携带 OK 字节。 */
#define BOOT_UPGRADE_PROTOCOL_READY_EVENT_ADDRESS \
    BOOT_UPGRADE_PROTOCOL_BROADCAST_ADDRESS

typedef enum
{
    BOOT_UPGRADE_PROTOCOL_POLL_NONE = 0,
    BOOT_UPGRADE_PROTOCOL_POLL_FRAME_DROPPED,
    BOOT_UPGRADE_PROTOCOL_POLL_FRAME_IGNORED,
    BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED,
    BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED_STAY,
    BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_ACCEPTED,
    BOOT_UPGRADE_PROTOCOL_POLL_DATA_ACCEPTED,
    BOOT_UPGRADE_PROTOCOL_POLL_DATA_DUPLICATE,
    BOOT_UPGRADE_PROTOCOL_POLL_DATA_REJECTED,
    BOOT_UPGRADE_PROTOCOL_POLL_END_ACCEPTED,
    BOOT_UPGRADE_PROTOCOL_POLL_END_REJECTED,
    BOOT_UPGRADE_PROTOCOL_POLL_END_REJECTED_STAY,
    BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_ACCEPTED,
    BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_REJECTED,
    BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_REJECTED_STAY,
    BOOT_UPGRADE_PROTOCOL_POLL_ABORT_ACCEPTED,
    BOOT_UPGRADE_PROTOCOL_POLL_ABORT_REJECTED
} boot_upgrade_protocol_poll_status_t;

void boot_upgrade_protocol_init(void);
boot_upgrade_protocol_poll_status_t boot_upgrade_protocol_poll(void);
void boot_upgrade_protocol_send_ready_event(void);

extern volatile uint8_t g_boot_upgrade_protocol_initialized;
extern volatile uint8_t g_boot_upgrade_protocol_timebase_initialized;
extern volatile uint32_t g_boot_upgrade_protocol_rx_byte_count;
extern volatile uint32_t g_boot_upgrade_protocol_frame_count;
extern volatile uint32_t g_boot_upgrade_protocol_frame_drop_count;
extern volatile uint16_t g_boot_upgrade_protocol_last_address;
extern volatile uint16_t g_boot_upgrade_protocol_last_command;
extern volatile uint16_t g_boot_upgrade_protocol_last_sequence;
extern volatile uint8_t g_boot_upgrade_protocol_last_frame_type;
extern volatile boot_upgrade_protocol_poll_status_t g_boot_upgrade_protocol_last_status;
extern volatile boot_upgrade_begin_status_t g_boot_upgrade_protocol_begin_status;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_protocol_begin_meta_status;
extern volatile uint8_t g_boot_upgrade_protocol_begin_staging_status;
extern volatile uint8_t g_boot_upgrade_protocol_begin_accepted;
extern volatile uint8_t g_boot_upgrade_protocol_last_response_type;
extern volatile uint8_t g_boot_upgrade_protocol_last_response_code;
extern volatile uint32_t g_boot_upgrade_protocol_tx_frame_count;
extern volatile boot_upgrade_install_status_t g_boot_upgrade_protocol_install_status;
extern volatile uint8_t g_boot_upgrade_protocol_install_accepted;
extern volatile boot_upgrade_meta_write_status_t g_boot_upgrade_protocol_abort_status;
extern volatile uint8_t g_boot_upgrade_protocol_abort_accepted;

#endif /* BOOT_UPGRADE_PROTOCOL_H */
