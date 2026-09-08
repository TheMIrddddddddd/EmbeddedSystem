#include "protocol_stream.h"
#include <string.h>

#define STREAM_HEADER_SIZE          12U
#define STREAM_TRAILER_SIZE         4U

static uint16_t stream_read_u16_be(const uint8_t *buffer)
{
    return (uint16_t)(((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1]);
}

static void stream_reset(protocol_stream_t *stream)
{
    stream->fill = 0U;
    stream->expected = 0U;
    stream->in_frame = 0U;
    stream->pending_a5 = 0U;
}

void protocol_stream_init(protocol_stream_t *stream)
{
    if (stream != NULL)
    {
        stream_reset(stream);
    }
}

/*
 * 在 buffer[1..fill) 里搜下一个 A5B6（坏帧肚子里可能嵌着下一个真帧头）。
 * 搜到：尾巴前移继续收；搜不到：整体清空回同步态。
 * 每次调用严格缩短 fill，天然终止。
 */
static void stream_resync(protocol_stream_t *stream)
{
    uint16_t index;

    for (index = 1U; (index + 1U) < stream->fill; index++)
    {
        if ((stream->buffer[index] == 0XA5U) && (stream->buffer[index + 1U] == 0xB6U))
        {
            (void)memmove(&stream->buffer[0], &stream->buffer[index], (size_t)(stream->fill - index));
            stream->fill = (uint16_t)(stream->fill - index);
            stream->in_frame = 1U;
            stream->pending_a5 = 0U;
            stream->expected = 0U;

            if (stream->fill >= STREAM_HEADER_SIZE)
            {
                uint16_t payload_len = stream_read_u16_be(&stream->buffer[10]);

                if (payload_len > PROTOCOL_MAX_PAYLOAD_SIZE)
                {
                    stream_reset(stream);
                }
                else
                {
                    stream->expected = (uint16_t)(STREAM_HEADER_SIZE + payload_len + STREAM_TRAILER_SIZE);
                }
            }

            /* 找到帧头、搬移完成后必须立即返回：
             * 残余可能不足 12 字节（裸帧头），继续扫描会误伤并落到清空分支 */
            return;
        }
    }
    stream_reset(stream);
}

protocol_stream_event_t protocol_stream_feed(protocol_stream_t *stream, uint8_t byte, protocol_frame_t *frame, protocol_stream_bad_t *bad)
{
    uint8_t dropped = 0U;

    if ((stream == NULL) || (frame == NULL))
    {
        return PROTOCOL_STREAM_EVENT_NONE;
    }

    if (stream->in_frame == 0U)
    {
        if (stream->pending_a5 != 0)
        {
            if (byte == 0xB6U)
            {
                stream->buffer[0] = 0xA5U;
                stream->buffer[1] = 0xB6U;
                stream->fill = 2U;
                stream->in_frame = 1U;
                stream->pending_a5 = 0U;
            }
            else if (byte == 0xA5U)
            {
                /* 连续 0xA5，保持暂存 */
            }
            else
            {
                stream->pending_a5 = 0U;
            }
        }

        else if (byte == 0xA5U)
        {
            stream->pending_a5 = 1U;
        }
        if (stream->in_frame == 0U)
        {
            return PROTOCOL_STREAM_EVENT_NONE;
        }
    }
    else
    {
        stream->buffer[stream->fill] = byte;
        stream->fill++;
    }

    for(;;)
    {
        if ((stream->expected == 0U) && (stream->fill >= STREAM_HEADER_SIZE))
        {
            uint16_t payload_len = stream_read_u16_be(&stream->buffer[10]);

            if (payload_len > PROTOCOL_MAX_PAYLOAD_SIZE)
            {
                dropped = 1U;

                /* 先留坏帧快照（K-01/K-02 错误应答用），再重同步；
                 * resync 会 memmove 缓冲，快照必须自带存储 */
                if (bad != NULL)
                {
                    (void)memcpy(bad->data, stream->buffer, stream->fill);
                    bad->length = stream->fill;
                }

                stream_resync(stream);

                if (stream->in_frame == 0U)
                {
                    return PROTOCOL_STREAM_EVENT_FRAME_DROPPED;
                }
                continue;
            }
            stream->expected = (uint16_t)(STREAM_HEADER_SIZE + payload_len + STREAM_TRAILER_SIZE);
        }

        if ((stream->expected != 0U) && (stream->fill >= stream->expected))
        {
            protocol_frame_t decoded;
            size_t decoded_length = 0U;

            if (protocol_frame_decode(stream->buffer, stream->fill, &decoded, &decoded_length) == PROTOCOL_STATUS_OK)
            {
                *frame = decoded;
                stream_reset(stream);

                return PROTOCOL_STREAM_EVENT_FRAME_READY;
            }

            dropped = 1U;

            if (bad != NULL)
            {
                (void)memcpy(bad->data, stream->buffer, stream->fill);
                bad->length = stream->fill;
            }

            stream_resync(stream);

            if (stream->in_frame == 0U)
            {
                return PROTOCOL_STREAM_EVENT_FRAME_DROPPED;
            }
            continue;
        }
        break;
    }
    return (dropped != 0U) ? PROTOCOL_STREAM_EVENT_FRAME_DROPPED : PROTOCOL_STREAM_EVENT_NONE;
}
