/*
 * test_ringbuffer.c - RT-Thread mirroring ring buffer test cases
 * Docs/03 M2: full/empty/wraparound, single producer single consumer, capacity >= 2048
 */

#include "unity.h"
#include "ringbuffer.h"
#include <string.h>

static uint8_t pool_small[8];
static uint8_t pool_big[2048];
static struct rt_ringbuffer rb;

void test_ringbuffer_basic_empty_full(void)
{
    rt_ringbuffer_init(&rb, pool_small, sizeof(pool_small));

    /* empty state */
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_data_len(&rb));
    TEST_ASSERT_EQUAL_UINT16(sizeof(pool_small), rt_ringbuffer_space_len(&rb));

    /* fill byte by byte: putchar returns 1 per byte (not free space) */
    uint8_t ch = 0U;
    for (int i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(1U, rt_ringbuffer_putchar(&rb, 0x11));
    }
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_data_len(&rb));
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_space_len(&rb));

    /* one more put must fail (returns 0) */
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_putchar(&rb, 0x22));

    /* drain fully -> empty again */
    for (int i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(1U, rt_ringbuffer_getchar(&rb, &ch));
        TEST_ASSERT_EQUAL_HEX8(0x11, ch);
    }
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_data_len(&rb));
    /* get on empty returns 0 */
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_getchar(&rb, &ch));
}

void test_ringbuffer_put_get_roundtrip(void)
{
    uint8_t in[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    uint8_t out[8] = { 0 };

    rt_ringbuffer_init(&rb, pool_small, sizeof(pool_small));
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_put(&rb, in, 8U));
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_get(&rb, out, 8U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 8U);
}

void test_ringbuffer_wraparound(void)
{
    uint8_t out[8] = { 0 };

    rt_ringbuffer_init(&rb, pool_small, sizeof(pool_small));

    /* write "AB" (2), read 1 -> head advances, tail stays at start boundary */
    TEST_ASSERT_EQUAL_UINT16(2U, rt_ringbuffer_put(&rb, (const uint8_t *)"AB", 2U));
    uint8_t c = 0U;
    TEST_ASSERT_EQUAL_UINT16(1U, rt_ringbuffer_getchar(&rb, &c));
    TEST_ASSERT_EQUAL_HEX8('A', c);

    /* now write 7 bytes to force tail past end -> wraparound (mirror bit) */
    TEST_ASSERT_EQUAL_UINT16(7U, rt_ringbuffer_put(&rb, (const uint8_t *)"CDEFGHI", 7U));

    /* B must be first (never consumed), then CDEFGHI in order */
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_get(&rb, out, 8U));
    TEST_ASSERT_EQUAL_HEX8('B', out[0]);
    TEST_ASSERT_EQUAL_HEX8('C', out[1]);
    TEST_ASSERT_EQUAL_HEX8('D', out[2]);
    TEST_ASSERT_EQUAL_HEX8('E', out[3]);
    TEST_ASSERT_EQUAL_HEX8('F', out[4]);
    TEST_ASSERT_EQUAL_HEX8('G', out[5]);
    TEST_ASSERT_EQUAL_HEX8('H', out[6]);
    TEST_ASSERT_EQUAL_HEX8('I', out[7]);
}

void test_ringbuffer_put_force_overwrite(void)
{
    uint8_t out[8] = { 0 };

    rt_ringbuffer_init(&rb, pool_small, sizeof(pool_small));
    rt_ringbuffer_put(&rb, (const uint8_t *)"01234567", 8U); /* full */

    /* put_force overwrites oldest data, should accept 8 */
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_put_force(&rb, (const uint8_t *)"ABCDEFGH", 8U));
    TEST_ASSERT_EQUAL_UINT16(8U, rt_ringbuffer_get(&rb, out, 8U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)"ABCDEFGH", out, 8U);
}

void test_ringbuffer_peek_consuming(void)
{
    rt_ringbuffer_init(&rb, pool_small, sizeof(pool_small));
    rt_ringbuffer_put(&rb, (const uint8_t *)"XY", 2U);

    uint8_t *p = NULL;
    /* peek returns readable size, *ptr points into the ring buffer */
    TEST_ASSERT_EQUAL_UINT16(2U, rt_ringbuffer_peek(&rb, &p));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_HEX8('X', p[0]);
    TEST_ASSERT_EQUAL_HEX8('Y', p[1]);

    /* documented semantics: rt_ringbuffer_peek is consuming (advances read_index) */
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_data_len(&rb));

    /* empty rb: peek returns 0 and sets *ptr to NULL */
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_peek(&rb, &p));
    TEST_ASSERT_NULL(p);
}

void test_ringbuffer_capacity_2048(void)
{
    static uint8_t buf[2048];
    rt_ringbuffer_init(&rb, pool_big, sizeof(pool_big));
    memset(buf, 0x5A, sizeof(buf));

    /* capacity contract: >= 2048 bytes usable */
    TEST_ASSERT_EQUAL_UINT16(2048U, rt_ringbuffer_space_len(&rb));
    TEST_ASSERT_EQUAL_UINT16(2048U, rt_ringbuffer_put(&rb, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT16(2048U, rt_ringbuffer_data_len(&rb));
    TEST_ASSERT_EQUAL_UINT16(2048U, rt_ringbuffer_get(&rb, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT16(0U, rt_ringbuffer_data_len(&rb));
}
