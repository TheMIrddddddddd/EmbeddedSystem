/*
 * test_main.c - Unity test entry point (M2 PC unit tests)
 */

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* test cases from sibling files */
void test_crc16_standard_vector(void);
void test_crc16_empty(void);
void test_crc16_modbus_frame_vector(void);
void test_crc32_standard_vector(void);
void test_crc32_empty(void);
void test_crc32_single_char(void);
void test_ringbuffer_basic_empty_full(void);
void test_ringbuffer_put_get_roundtrip(void);
void test_ringbuffer_wraparound(void);
void test_ringbuffer_put_force_overwrite(void);
void test_ringbuffer_peek_consuming(void);
void test_ringbuffer_capacity_2048(void);

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_standard_vector);
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_modbus_frame_vector);
    RUN_TEST(test_crc32_standard_vector);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_single_char);

    RUN_TEST(test_ringbuffer_basic_empty_full);
    RUN_TEST(test_ringbuffer_put_get_roundtrip);
    RUN_TEST(test_ringbuffer_wraparound);
    RUN_TEST(test_ringbuffer_put_force_overwrite);
    RUN_TEST(test_ringbuffer_peek_consuming);
    RUN_TEST(test_ringbuffer_capacity_2048);

    return UNITY_END();
}
