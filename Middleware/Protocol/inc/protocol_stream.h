#ifndef PROTOCOL_STREAM_H
#define PROTOCOL_STREAM_H

#include <stdint.h>
#include <stddef.h>

#include "protocol_frame.h"



/*
 * 流式帧解析器。
 *
 * 逐字节喂入，负责：搜帧头 A5B6、半帧、粘包、噪声前缀、
 * CRC/长度/帧尾错误的丢弃与重新同步。
 * 字段级校验与 CRC 计算复用 protocol_frame_decode()，本模块不重复实现。
 */
typedef enum
{
    PROTOCOL_STREAM_EVENT_NONE = 0,
    PROTOCOL_STREAM_EVENT_FRAME_READY,
    PROTOCOL_STREAM_EVENT_FRAME_DROPPED  /* 坏帧已丢弃并重新同步；bad 输出坏帧原始内容供 K-01 错误应答 */
} protocol_stream_event_t;

/*
 * 坏帧快照（《01》十四 K-01/K-02）：解码失败或长度非法时，
 * 把丢弃前的原始字节拷给调用方，协议层据此回应答帧。
 * 调用方自备此结构（建议静态分配，1040B），feed 返回后 data 内容稳定。
 */
typedef struct
{
    uint8_t  data[PROTOCOL_MAX_FRAME_SIZE];
    uint16_t length;
} protocol_stream_bad_t;

typedef struct
{
    uint8_t buffer[PROTOCOL_MAX_FRAME_SIZE];
    uint16_t fill;
    uint16_t expected;
    uint8_t in_frame;
    uint8_t pending_a5;
} protocol_stream_t;

void protocol_stream_init(protocol_stream_t *stream);
protocol_stream_event_t protocol_stream_feed(protocol_stream_t *stream, uint8_t byte, protocol_frame_t *frame, protocol_stream_bad_t *bad);

#endif /* PROTOCOL_STREAM_H */
