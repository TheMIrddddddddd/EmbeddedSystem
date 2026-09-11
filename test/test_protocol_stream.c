/* PC tests for the streaming frame parser (M4-4a). */

#include "unity.h"
#include "protocol_stream.h"
#include "protocol_frame.h"
#include <string.h>

static protocol_stream_t stream;

static uint16_t make_frame(uint16_t address, uint8_t type, uint16_t command,
                           uint16_t sequence, const uint8_t *payload,
                           uint16_t length, uint8_t *out)
{
    protocol_frame_t frame;
    size_t encoded_length = 0U;

    frame.device_address = address;
    frame.frame_type = type;
    frame.command = command;
    frame.sequence = sequence;
    frame.payload_length = length;
    frame.payload = payload;

    TEST_ASSERT_EQUAL(PROTOCOL_STATUS_OK,
                      protocol_frame_encode(&frame, out,
                                            PROTOCOL_MAX_FRAME_SIZE,
                                            &encoded_length));

    return (uint16_t)encoded_length;
}

static uint16_t feed_all(const uint8_t *data, uint16_t length,
                         protocol_frame_t *frame)
{
    uint16_t i;
    uint16_t ready_count = 0U;

    for (i = 0U; i < length; i++)
    {
        if (protocol_stream_feed(&stream, data[i], frame, NULL) ==
            PROTOCOL_STREAM_EVENT_FRAME_READY)
        {
            ready_count++;
        }
    }

    return ready_count;
}

void test_stream_single_frame_byte_by_byte(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0x11U, 0x22U };
    uint8_t data[32];
    protocol_frame_t frame;
    uint16_t length;
    uint16_t i;

    length = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0201U, 0x0042U,
                        payload, 2U, data);

    for (i = 0U; i < (uint16_t)(length - 1U); i++)
    {
        TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_NONE,
                          protocol_stream_feed(&stream, data[i], &frame, NULL));
    }

    TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_FRAME_READY,
                      protocol_stream_feed(&stream, data[length - 1U], &frame, NULL));
    TEST_ASSERT_EQUAL_UINT16(0x0001U, frame.device_address);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_TYPE_COMMAND, frame.frame_type);
    TEST_ASSERT_EQUAL_UINT16(0x0201U, frame.command);
    TEST_ASSERT_EQUAL_UINT16(0x0042U, frame.sequence);
    TEST_ASSERT_EQUAL_UINT16(2U, frame.payload_length);
    TEST_ASSERT_EQUAL_UINT8(0x11U, frame.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22U, frame.payload[1]);
}

void test_stream_noise_prefix(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0xFFU };
    uint8_t data[32];
    uint8_t noisy[40];
    protocol_frame_t frame;
    uint16_t length;

    length = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0103U, 0x0007U,
                        payload, 1U, data);

    noisy[0] = 0x00U;
    noisy[1] = 0x51U;
    noisy[2] = 0xA5U;
    (void)memcpy(&noisy[3], data, length);

    TEST_ASSERT_EQUAL_UINT16(1U,
        feed_all(noisy, (uint16_t)(length + 3U), &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0007U, frame.sequence);
}

void test_stream_half_frame_then_complete(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0xAAU, 0xBBU, 0xCCU };
    uint8_t data[32];
    protocol_frame_t frame;
    uint16_t length;
    uint16_t half;
    uint16_t i;

    length = make_frame(0x0002U, PROTOCOL_TYPE_COMMAND, 0x0203U, 0x0009U,
                        payload, 3U, data);
    half = (uint16_t)(length / 2U);

    for (i = 0U; i < half; i++)
    {
        TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_NONE,
                          protocol_stream_feed(&stream, data[i], &frame, NULL));
    }

    TEST_ASSERT_EQUAL_UINT16(1U,
        feed_all(&data[half], (uint16_t)(length - half), &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0009U, frame.sequence);
}

void test_stream_two_frames_sticky(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0x01U };
    uint8_t frame_a[32];
    uint8_t frame_b[32];
    uint8_t joined[64];
    protocol_frame_t frame;
    uint16_t length_a;
    uint16_t length_b;

    length_a = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0201U, 0x0001U,
                          payload, 1U, frame_a);
    length_b = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0202U, 0x0002U,
                          payload, 1U, frame_b);

    (void)memcpy(joined, frame_a, length_a);
    (void)memcpy(&joined[length_a], frame_b, length_b);

    TEST_ASSERT_EQUAL_UINT16(2U, feed_all(joined,
        (uint16_t)(length_a + length_b), &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0002U, frame.sequence);
}

void test_stream_crc_error_resync_to_next_frame(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0x5AU };
    uint8_t frame_a[32];
    uint8_t frame_b[32];
    uint8_t joined[64];
    protocol_frame_t frame;
    protocol_stream_event_t event;
    uint16_t length_a;
    uint16_t length_b;
    uint16_t dropped_seen = 0U;
    uint16_t ready_seen = 0U;
    uint16_t i;

    length_a = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0201U, 0x0011U,
                          payload, 1U, frame_a);
    length_b = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0201U, 0x0022U,
                          payload, 1U, frame_b);

    frame_a[length_a - 3U] ^= 0xFFU;   /* 破坏 CRC */

    (void)memcpy(joined, frame_a, length_a);
    (void)memcpy(&joined[length_a], frame_b, length_b);

    for (i = 0U; i < (uint16_t)(length_a + length_b); i++)
    {
        event = protocol_stream_feed(&stream, joined[i], &frame, NULL);

        if (event == PROTOCOL_STREAM_EVENT_FRAME_DROPPED)
        {
            dropped_seen++;
        }
        else if (event == PROTOCOL_STREAM_EVENT_FRAME_READY)
        {
            ready_seen++;
        }
    }

    TEST_ASSERT_EQUAL_UINT16(1U, dropped_seen);
    TEST_ASSERT_EQUAL_UINT16(1U, ready_seen);
    TEST_ASSERT_EQUAL_UINT16(0x0022U, frame.sequence);
}

void test_stream_bad_length_dropped(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0x7FU };
    uint8_t bad_frame[12];
    uint8_t frame_b[32];
    uint8_t joined[64];
    protocol_frame_t frame;
    uint16_t length_b;

    /* 手造长度字段 0xFFFF 的坏帧（覆盖 [10..11]） */
    bad_frame[0] = 0xA5U;
    bad_frame[1] = 0xB6U;
    bad_frame[2] = 0x02U;
    bad_frame[10] = 0xFFU;
    bad_frame[11] = 0xFFU;

    length_b = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0203U, 0x0033U,
                          payload, 1U, frame_b);

    (void)memcpy(&joined[0], bad_frame, 12U);
    (void)memcpy(&joined[12], frame_b, length_b);

    TEST_ASSERT_EQUAL_UINT16(1U,
        feed_all(joined, (uint16_t)(12U + length_b), &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0033U, frame.sequence);
}

void test_stream_dangling_a5_then_resume(void)
{
    protocol_stream_init(&stream);
    static const uint8_t payload[] = { 0x0AU };
    uint8_t data[32];
    protocol_frame_t frame;
    uint16_t length;

    length = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0103U, 0x0044U,
                        payload, 1U, data);

    TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_NONE,
                      protocol_stream_feed(&stream, 0xA5U, &frame, NULL));
    TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_NONE,
                      protocol_stream_feed(&stream, 0x33U, &frame, NULL));
    TEST_ASSERT_EQUAL(PROTOCOL_STREAM_EVENT_NONE,
                      protocol_stream_feed(&stream, 0xA5U, &frame, NULL));

    /* 暂存的 0xA5 与原始帧 data[1]（本就是 0xB6）拼成帧头，
     * 因此从 data[1] 开始喂，跳过 data[0] 的 0xA5 */
    TEST_ASSERT_EQUAL_UINT16(1U, feed_all(&data[1], (uint16_t)(length - 1U), &frame));
    TEST_ASSERT_EQUAL_UINT16(0x0044U, frame.sequence);
}

void test_stream_bad_frame_snapshot_surfaces(void)
{
    static const uint8_t payload[] = { 0x5AU };
    uint8_t frame_a[32];
    protocol_frame_t frame;
    protocol_stream_bad_t bad;
    uint16_t length_a;
    uint16_t i;

    length_a = make_frame(0x0001U, PROTOCOL_TYPE_COMMAND, 0x0201U, 0x0077U,
                          payload, 1U, frame_a);

    frame_a[length_a - 3U] ^= 0xFFU;   /* 破坏 CRC，最后一个字节是帧尾 0xA5 */

    for (i = 0U; i < length_a; i++)
    {
        (void)protocol_stream_feed(&stream, frame_a[i], &frame, &bad);
    }

    /* 快照内容必须等于喂入的坏帧原文（K-01 错误应答的数据来源） */
    TEST_ASSERT_EQUAL_UINT16(length_a, bad.length);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, bad.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB6U, bad.data[1]);
    TEST_ASSERT_EQUAL_UINT16(0x0001U,
        (uint16_t)((bad.data[3] << 8) | bad.data[4]));
    TEST_ASSERT_EQUAL_UINT16(0x0201U,
        (uint16_t)((bad.data[6] << 8) | bad.data[7]));
    TEST_ASSERT_EQUAL_UINT16(0x0077U,
        (uint16_t)((bad.data[8] << 8) | bad.data[9]));
    TEST_ASSERT_EQUAL_UINT8(0xB6U, bad.data[length_a - 2U]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, bad.data[length_a - 1U]);
}
