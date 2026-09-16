#include "boot_upgrade_protocol.h"

#include "board_timebase.h"
#include "board_usart.h"
#include "boot_upgrade_begin.h"
#include "boot_upgrade_context.h"
#include "boot_upgrade_data.h"
#include "boot_upgrade_end.h"
#include "boot_upgrade_install.h"
#include "boot_upgrade_staging.h"
#include "protocol_stream.h"

/*
 * Bootloader 只负责在线升级链路的裸机接收。
 * USART1 的 DMA/IDLE 中断把字节放入 BSP ringbuffer，
 * 本模块在主循环中取出字节并执行协议解析和命令分发。
 */
static protocol_stream_t s_boot_upgrade_stream;
static protocol_stream_bad_t s_boot_upgrade_bad;
static uint8_t s_boot_upgrade_tx_buffer[PROTOCOL_MAX_FRAME_SIZE];

volatile uint8_t g_boot_upgrade_protocol_initialized;
volatile uint8_t g_boot_upgrade_protocol_timebase_initialized;
volatile uint32_t g_boot_upgrade_protocol_rx_byte_count;
volatile uint32_t g_boot_upgrade_protocol_frame_count;
volatile uint32_t g_boot_upgrade_protocol_frame_drop_count;
volatile uint16_t g_boot_upgrade_protocol_last_address;
volatile uint16_t g_boot_upgrade_protocol_last_command;
volatile uint16_t g_boot_upgrade_protocol_last_sequence;
volatile uint8_t g_boot_upgrade_protocol_last_frame_type;
volatile boot_upgrade_protocol_poll_status_t g_boot_upgrade_protocol_last_status;
volatile boot_upgrade_begin_status_t g_boot_upgrade_protocol_begin_status;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_protocol_begin_meta_status;
volatile uint8_t g_boot_upgrade_protocol_begin_staging_status;
volatile uint8_t g_boot_upgrade_protocol_begin_accepted;
volatile uint8_t g_boot_upgrade_protocol_last_response_type;
volatile uint8_t g_boot_upgrade_protocol_last_response_code;
volatile uint32_t g_boot_upgrade_protocol_tx_frame_count;
volatile boot_upgrade_install_status_t g_boot_upgrade_protocol_install_status =
    BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION;
volatile uint8_t g_boot_upgrade_protocol_install_accepted;
volatile boot_upgrade_meta_write_status_t g_boot_upgrade_protocol_abort_status =
    BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
volatile uint8_t g_boot_upgrade_protocol_abort_accepted;

static void boot_upgrade_protocol_send_payload(
    const protocol_frame_t *request,
    uint8_t response_type,
    const uint8_t *payload,
    uint16_t payload_length)
{
    protocol_frame_t response;
    size_t encoded_length;

    if ((request == 0) ||
        (payload == 0) ||
        (payload_length == 0U))
    {
        return;
    }

    response.device_address = request->device_address;
    response.frame_type = response_type;
    response.command = request->command;
    response.sequence = request->sequence;
    response.payload_length = payload_length;
    response.payload = payload;
    encoded_length = 0U;

    if (protocol_frame_encode(
            &response,
            s_boot_upgrade_tx_buffer,
            sizeof(s_boot_upgrade_tx_buffer),
            &encoded_length) != PROTOCOL_STATUS_OK)
    {
        return;
    }

    board_usart1_rs485_send_buffer(
        s_boot_upgrade_tx_buffer,
        (uint16_t)encoded_length
    );

    g_boot_upgrade_protocol_last_response_type = response_type;
    g_boot_upgrade_protocol_last_response_code = payload[0];
    g_boot_upgrade_protocol_tx_frame_count++;
}

static void boot_upgrade_protocol_send_status(
    const protocol_frame_t *request,
    uint8_t response_type,
    uint8_t response_code)
{
    uint8_t payload[1];

    payload[0] = response_code;
    boot_upgrade_protocol_send_payload(
        request,
        response_type,
        payload,
        sizeof(payload)
    );
}

static uint8_t boot_upgrade_protocol_begin_error_code(
    boot_upgrade_begin_status_t begin_status)
{
    switch (begin_status)
    {
        case BOOT_UPGRADE_BEGIN_STATUS_INVALID_HEADER:
        case BOOT_UPGRADE_BEGIN_STATUS_INVALID_ARGUMENT:
            return BOOT_UPGRADE_PROTOCOL_ERROR_INVALID_HDR;

        case BOOT_UPGRADE_BEGIN_STATUS_STATE_NOT_ALLOWED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STATE;

        case BOOT_UPGRADE_BEGIN_STATUS_META_WRITE_FAILED:
        case BOOT_UPGRADE_BEGIN_STATUS_STAGING_PREPARE_FAILED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;

        default:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;
    }
}

static uint8_t boot_upgrade_protocol_end_error_code(
    boot_upgrade_end_status_t end_status)
{
    switch (end_status)
    {
        case BOOT_UPGRADE_END_STATUS_STATE_NOT_ALLOWED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STATE;

        case BOOT_UPGRADE_END_STATUS_MANIFEST_PROGRAM_FAILED:
        case BOOT_UPGRADE_END_STATUS_MANIFEST_VERIFY_FAILED:
        case BOOT_UPGRADE_END_STATUS_META_UPDATE_FAILED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;

        default:
            return 0x65U;
    }
}

static uint8_t boot_upgrade_protocol_install_error_code(
    boot_upgrade_install_status_t install_status)
{
    switch (install_status)
    {
        case BOOT_UPGRADE_INSTALL_STATUS_STATE_NOT_ALLOWED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STATE;

        case BOOT_UPGRADE_INSTALL_STATUS_META_UPDATE_FAILED:
        case BOOT_UPGRADE_INSTALL_STATUS_ROLLBACK_META_FAILED:
            return BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;

        default:
            return 0x65U;
    }
}

static uint8_t boot_upgrade_protocol_abort_error_code(
    boot_upgrade_meta_write_status_t abort_status)
{
    if (abort_status == BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT)
    {
        return BOOT_UPGRADE_PROTOCOL_ERROR_STATE;
    }

    return BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;
}

static void boot_upgrade_protocol_send_bad_frame_error(
    const protocol_stream_bad_t *bad)
{
    uint16_t address;
    uint16_t command;
    uint16_t sequence;
    uint8_t error_code;

    if ((bad == 0) || (bad->length < 12U))
    {
        return;
    }

    address = (uint16_t)(((uint16_t)bad->data[3] << 8U) | bad->data[4]);

    /* 0x0000 保留，0xFFFF 广播均不产生错误应答。 */
    if ((address == 0U) ||
        (address == BOOT_UPGRADE_PROTOCOL_BROADCAST_ADDRESS))
    {
        return;
    }

    command = (uint16_t)(((uint16_t)bad->data[6] << 8U) | bad->data[7]);
    sequence = (uint16_t)(((uint16_t)bad->data[8] << 8U) | bad->data[9]);

    if (bad->length < PROTOCOL_FIXED_SIZE)
    {
        error_code = BOOT_UPGRADE_PROTOCOL_ERROR_LENGTH;
    }
    else if ((bad->data[bad->length - 2U] == 0xB6U) &&
             (bad->data[bad->length - 1U] == 0xA5U))
    {
        error_code = BOOT_UPGRADE_PROTOCOL_ERROR_CRC;
    }
    else
    {
        return;
    }

    {
        protocol_frame_t request;

        request.device_address = address;
        request.frame_type = PROTOCOL_TYPE_COMMAND;
        request.command = command;
        request.sequence = sequence;
        request.payload_length = 0U;
        request.payload = 0;

        boot_upgrade_protocol_send_status(
            &request,
            PROTOCOL_TYPE_ERROR,
            error_code
        );
    }
}

static boot_upgrade_protocol_poll_status_t boot_upgrade_protocol_dispatch(
    const protocol_frame_t *frame)
{
    boot_upgrade_begin_status_t begin_status;

    if (frame == 0)
    {
        return BOOT_UPGRADE_PROTOCOL_POLL_FRAME_IGNORED;
    }

    g_boot_upgrade_protocol_last_address = frame->device_address;
    g_boot_upgrade_protocol_last_command = frame->command;
    g_boot_upgrade_protocol_last_sequence = frame->sequence;
    g_boot_upgrade_protocol_last_frame_type = frame->frame_type;

    /* Bootloader 只处理主机下发的命令帧。 */
    if (frame->frame_type != PROTOCOL_TYPE_COMMAND)
    {
        return BOOT_UPGRADE_PROTOCOL_POLL_FRAME_IGNORED;
    }

    /* 广播地址不得启动升级；0 地址已由帧解码器拒绝。 */
    if (frame->device_address == BOOT_UPGRADE_PROTOCOL_BROADCAST_ADDRESS)
    {
        return BOOT_UPGRADE_PROTOCOL_POLL_FRAME_IGNORED;
    }

    if (frame->command == BOOT_UPGRADE_PROTOCOL_CMD_BEGIN)
    {
        /* BEGIN 的数据区必须恰好是固定 32 字节 firmware_header。 */
        if (frame->payload_length != FIRMWARE_HEADER_SIZE)
        {
            g_boot_upgrade_protocol_begin_status =
                BOOT_UPGRADE_BEGIN_STATUS_INVALID_HEADER;
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                BOOT_UPGRADE_PROTOCOL_ERROR_INVALID_HDR
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED;
        }

        begin_status = boot_upgrade_begin_accept(
            frame->payload,
            frame->payload_length
        );

        g_boot_upgrade_protocol_begin_status = begin_status;
        g_boot_upgrade_protocol_begin_meta_status =
            g_boot_upgrade_begin_meta_status;
        g_boot_upgrade_protocol_begin_staging_status =
            (uint8_t)g_boot_upgrade_begin_staging_status;

        if (begin_status != BOOT_UPGRADE_BEGIN_STATUS_OK)
        {
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                boot_upgrade_protocol_begin_error_code(begin_status)
            );

            if ((begin_status == BOOT_UPGRADE_BEGIN_STATUS_STAGING_PREPARE_FAILED) &&
                (g_boot_upgrade_begin_cleanup_status !=
                BOOT_UPGRADE_META_WRITE_OK)
               )
            {
                return BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED_STAY;
            }

            return BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED;
        }

        g_boot_upgrade_protocol_begin_accepted = 1U;
        boot_upgrade_protocol_send_status(
            frame,
            PROTOCOL_TYPE_RESPONSE,
            BOOT_UPGRADE_PROTOCOL_RESPONSE_OK
        );

        return BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_ACCEPTED;
    }

    if (frame->command == BOOT_UPGRADE_PROTOCOL_CMD_DATA)
    {
        upgrade_meta_t selected_meta;
        boot_upgrade_data_status_t data_status;
        uint16_t data_sequence;

        selected_meta = g_boot_meta_selected;
        data_sequence = 0U;

        data_status = boot_upgrade_data_accept(
            &selected_meta,
            frame->payload,
            frame->payload_length,
            &data_sequence
        );

        if ((data_status == BOOT_UPGRADE_DATA_STATUS_OK) ||
            (data_status == BOOT_UPGRADE_DATA_STATUS_DUPLICATE))
        {
            uint8_t ack_payload[3];

            ack_payload[0] = BOOT_UPGRADE_PROTOCOL_RESPONSE_OK;
            ack_payload[1] = (uint8_t)(data_sequence >> 8U);
            ack_payload[2] = (uint8_t)(data_sequence & 0xFFU);

            boot_upgrade_protocol_send_payload(
                frame,
                PROTOCOL_TYPE_RESPONSE,
                ack_payload,
                sizeof(ack_payload)
            );

            return (data_status == BOOT_UPGRADE_DATA_STATUS_DUPLICATE) ?
                BOOT_UPGRADE_PROTOCOL_POLL_DATA_DUPLICATE :
                BOOT_UPGRADE_PROTOCOL_POLL_DATA_ACCEPTED;
        }

        {
            uint8_t nack_payload[3];

            nack_payload[0] = (uint8_t)(data_sequence >> 8U);
            nack_payload[1] = (uint8_t)(data_sequence & 0xFFU);

            switch (data_status)
            {
                case BOOT_UPGRADE_DATA_STATUS_CHUNK_CRC_INVALID:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_DATA_CRC;
                    break;

                case BOOT_UPGRADE_DATA_STATUS_LENGTH_INVALID:
                case BOOT_UPGRADE_DATA_STATUS_INVALID_ARGUMENT:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_DATA_LENGTH;
                    break;

                case BOOT_UPGRADE_DATA_STATUS_OFFSET_INVALID:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_DATA_OFFSET;
                    break;

                case BOOT_UPGRADE_DATA_STATUS_SEQUENCE_NOT_CONTIGUOUS:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_DATA_SEQUENCE;
                    break;

                case BOOT_UPGRADE_DATA_STATUS_STATE_NOT_ALLOWED:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_STATE;
                    break;

                case BOOT_UPGRADE_DATA_STATUS_FLASH_WRITE_FAILED:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;
                    break;

                default:
                    nack_payload[2] = BOOT_UPGRADE_PROTOCOL_ERROR_STORAGE;
                    break;
            }

            boot_upgrade_protocol_send_payload(
                frame,
                PROTOCOL_TYPE_ERROR,
                nack_payload,
                sizeof(nack_payload)
            );
            g_boot_upgrade_protocol_last_response_code = nack_payload[2];
        }

        return BOOT_UPGRADE_PROTOCOL_POLL_DATA_REJECTED;
    }

    if (frame->command == BOOT_UPGRADE_PROTOCOL_CMD_END)
    {
        boot_upgrade_end_status_t end_status;

        if (frame->payload_length != 0U)
        {
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                0x65U
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_END_REJECTED;
        }

        end_status = boot_upgrade_end_accept();

        if (end_status == BOOT_UPGRADE_END_STATUS_OK)
        {
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_RESPONSE,
                BOOT_UPGRADE_PROTOCOL_RESPONSE_OK
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_END_ACCEPTED;
        }

        boot_upgrade_protocol_send_status(
            frame,
            PROTOCOL_TYPE_ERROR,
            boot_upgrade_protocol_end_error_code(end_status)
        );

        if ((end_status != BOOT_UPGRADE_END_STATUS_STATE_NOT_ALLOWED) &&
            (g_boot_upgrade_end_cleanup_status != BOOT_UPGRADE_META_WRITE_OK))
        {
            return BOOT_UPGRADE_PROTOCOL_POLL_END_REJECTED_STAY;
        }

        return BOOT_UPGRADE_PROTOCOL_POLL_END_REJECTED;
    }

    if (frame->command == BOOT_UPGRADE_PROTOCOL_CMD_INSTALL)
    {
        boot_upgrade_install_status_t install_status;

        if (frame->payload_length != 0U)
        {
            g_boot_upgrade_protocol_install_status =
                BOOT_UPGRADE_INSTALL_STATUS_INVALID_ARGUMENT;
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                BOOT_UPGRADE_PROTOCOL_ERROR_LENGTH
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_REJECTED;
        }

        install_status = boot_upgrade_install_start();
        g_boot_upgrade_protocol_install_status = install_status;

        if (install_status != BOOT_UPGRADE_INSTALL_STATUS_OK)
        {
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                boot_upgrade_protocol_install_error_code(install_status)
            );

            return BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_REJECTED;
        }

        g_boot_upgrade_protocol_install_accepted = 1U;
        boot_upgrade_protocol_send_status(
            frame,
            PROTOCOL_TYPE_RESPONSE,
            BOOT_UPGRADE_PROTOCOL_RESPONSE_OK
        );

        return BOOT_UPGRADE_PROTOCOL_POLL_INSTALL_ACCEPTED;
    }

    if (frame->command == BOOT_UPGRADE_PROTOCOL_CMD_ABORT)
    {
        boot_upgrade_meta_write_status_t abort_status;

        if (frame->payload_length != 0U)
        {
            g_boot_upgrade_protocol_abort_status =
                BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                BOOT_UPGRADE_PROTOCOL_ERROR_LENGTH
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_ABORT_REJECTED;
        }

        abort_status = boot_upgrade_abort_receiving();
        g_boot_upgrade_protocol_abort_status = abort_status;

        if (abort_status != BOOT_UPGRADE_META_WRITE_OK)
        {
            boot_upgrade_protocol_send_status(
                frame,
                PROTOCOL_TYPE_ERROR,
                boot_upgrade_protocol_abort_error_code(abort_status)
            );
            return BOOT_UPGRADE_PROTOCOL_POLL_ABORT_REJECTED;
        }

        g_boot_upgrade_protocol_abort_accepted = 1U;
        boot_upgrade_protocol_send_status(
            frame,
            PROTOCOL_TYPE_RESPONSE,
            BOOT_UPGRADE_PROTOCOL_RESPONSE_OK
        );
        return BOOT_UPGRADE_PROTOCOL_POLL_ABORT_ACCEPTED;
    }

    boot_upgrade_protocol_send_status(
        frame,
        PROTOCOL_TYPE_ERROR,
        BOOT_UPGRADE_PROTOCOL_ERROR_COMMAND
    );

    return BOOT_UPGRADE_PROTOCOL_POLL_FRAME_IGNORED;
}

void boot_upgrade_protocol_init(void)
{
    if (g_boot_upgrade_protocol_initialized != 0U)
    {
        return;
    }

    protocol_stream_init(&s_boot_upgrade_stream);

    /*
     * 自定义 8N1 DMA 接收使用 TIMER1 所属的 RS485 时基。
     * FMC 等待改由 TIMER4 承担，不能重配置 TIMER1。
     */
    g_boot_upgrade_protocol_timebase_initialized =
        (board_timebase_init() != 0) ? 1U : 0U;

    /* USART1 = PA2/PA3，RS485 DE = PA1，RX 使用 DMA0/Channel5。 */
    board_usart1_rs485_init();

    g_boot_upgrade_protocol_rx_byte_count = 0U;
    g_boot_upgrade_protocol_frame_count = 0U;
    g_boot_upgrade_protocol_frame_drop_count = 0U;
    g_boot_upgrade_protocol_last_address = 0U;
    g_boot_upgrade_protocol_last_command = 0U;
    g_boot_upgrade_protocol_last_sequence = 0U;
    g_boot_upgrade_protocol_last_frame_type = 0U;
    g_boot_upgrade_protocol_last_status = BOOT_UPGRADE_PROTOCOL_POLL_NONE;
    g_boot_upgrade_protocol_begin_status =
        BOOT_UPGRADE_BEGIN_STATUS_INVALID_ARGUMENT;
    g_boot_upgrade_protocol_begin_meta_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_protocol_begin_staging_status =
        (uint8_t)BOARD_INTERNAL_FLASH_STATUS_OK;
    g_boot_upgrade_protocol_begin_accepted = 0U;
    g_boot_upgrade_protocol_last_response_type = 0U;
    g_boot_upgrade_protocol_last_response_code = 0U;
    g_boot_upgrade_protocol_tx_frame_count = 0U;
    g_boot_upgrade_protocol_install_status =
        BOOT_UPGRADE_INSTALL_STATUS_NO_ACTION;
    g_boot_upgrade_protocol_install_accepted = 0U;
    g_boot_upgrade_protocol_abort_status =
        BOOT_UPGRADE_META_WRITE_INVALID_ARGUMENT;
    g_boot_upgrade_protocol_abort_accepted = 0U;
    g_boot_upgrade_protocol_initialized = 1U;
}

void boot_upgrade_protocol_send_ready_event(void)
{
    static const uint8_t payload[1] =
    {
        BOOT_UPGRADE_PROTOCOL_RESPONSE_OK
    };
    protocol_frame_t event_frame;
    size_t encoded_length;

    if (g_boot_upgrade_protocol_initialized == 0U)
    {
        return;
    }

    event_frame.device_address =
        BOOT_UPGRADE_PROTOCOL_READY_EVENT_ADDRESS;
    event_frame.frame_type = PROTOCOL_TYPE_EVENT;
    event_frame.command = BOOT_UPGRADE_PROTOCOL_CMD_ENTER_BOOT;
    event_frame.sequence = 0U;
    event_frame.payload_length = sizeof(payload);
    event_frame.payload = payload;
    encoded_length = 0U;

    if (protocol_frame_encode(
            &event_frame,
            s_boot_upgrade_tx_buffer,
            sizeof(s_boot_upgrade_tx_buffer),
            &encoded_length) != PROTOCOL_STATUS_OK)
    {
        return;
    }

    board_usart1_rs485_send_buffer(
        s_boot_upgrade_tx_buffer,
        (uint16_t)encoded_length
    );
    g_boot_upgrade_protocol_last_response_type = PROTOCOL_TYPE_EVENT;
    g_boot_upgrade_protocol_last_response_code = payload[0];
    g_boot_upgrade_protocol_tx_frame_count++;
}

boot_upgrade_protocol_poll_status_t boot_upgrade_protocol_poll(void)
{
    uint8_t byte;
    protocol_frame_t frame;
    protocol_stream_event_t stream_event;
    boot_upgrade_protocol_poll_status_t poll_status;

    if (g_boot_upgrade_protocol_initialized == 0U)
    {
        return BOOT_UPGRADE_PROTOCOL_POLL_NONE;
    }

    poll_status = BOOT_UPGRADE_PROTOCOL_POLL_NONE;

    while (board_usart1_rs485_try_receive_byte(&byte) != 0U)
    {
        g_boot_upgrade_protocol_rx_byte_count++;

        stream_event = protocol_stream_feed(
            &s_boot_upgrade_stream,
            byte,
            &frame,
            &s_boot_upgrade_bad
        );

        if (stream_event == PROTOCOL_STREAM_EVENT_FRAME_READY)
        {
            boot_upgrade_protocol_poll_status_t frame_status;

            g_boot_upgrade_protocol_frame_count++;
            frame_status = boot_upgrade_protocol_dispatch(&frame);

            /* 同一批字节中只要有一个 BEGIN 成功，就必须保持驻留。 */
            if (frame_status == BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_ACCEPTED)
            {
                poll_status = frame_status;
            }
            else if ((poll_status != BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_ACCEPTED) &&
                     (poll_status != BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED_STAY))
            {
                poll_status = frame_status;
            }
        }
        else if (stream_event == PROTOCOL_STREAM_EVENT_FRAME_DROPPED)
        {
            g_boot_upgrade_protocol_frame_drop_count++;
            boot_upgrade_protocol_send_bad_frame_error(&s_boot_upgrade_bad);

            if ((poll_status != BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_ACCEPTED) &&
                (poll_status != BOOT_UPGRADE_PROTOCOL_POLL_BEGIN_REJECTED_STAY))
            {
                poll_status = BOOT_UPGRADE_PROTOCOL_POLL_FRAME_DROPPED;
            }
        }
    }

    if (poll_status != BOOT_UPGRADE_PROTOCOL_POLL_NONE)
    {
        g_boot_upgrade_protocol_last_status = poll_status;
    }

    return poll_status;
}
