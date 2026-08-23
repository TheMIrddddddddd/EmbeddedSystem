/*
 * test_crc.c - CRC-16/Modbus and CRC-32/ISO-HDLC test cases
 * Vectors from Docs/01 section 12-2 (authoritative) + well-known public vectors.
 */

#include "unity.h"
#include "common_crc.h"
#include <string.h>

#define VECTOR_SIZE 9U /* "123456789" */

static const uint8_t standard_vector[VECTOR_SIZE] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

/* Classic Modbus RTU read-holding-registers frame: 01 03 00 00 00 0A -> CRC 0xC5CD */
static const uint8_t modbus_frame[6] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };

void test_crc16_standard_vector(void)
{
    /* CRC-16/Modbus("123456789") = 0x4B37  (Docs/01 12-2) */
    TEST_ASSERT_EQUAL_HEX16(0x4B37, common_crc16_calc(standard_vector, VECTOR_SIZE));
}

void test_crc16_empty(void)
{
    /* no data: init 0xFFFF, reflected, xorout 0 -> 0xFFFF */
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, common_crc16_calc(NULL, 0U));
}

void test_crc16_modbus_frame_vector(void)
{
    /* Classic Modbus RTU read-holding-registers frame: 01 03 00 00 00 0A.
     * Standard register value = 0xCDC5 (transmitted low byte first as CD C5);
     * project wire order is defined at protocol layer (Docs/01 12-2). */
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, common_crc16_calc(modbus_frame, sizeof(modbus_frame)));
}

void test_crc32_standard_vector(void)
{
    /* CRC-32/ISO-HDLC("123456789") = 0xCBF43926  (Docs/01 12-2) */
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, common_crc32_calc(standard_vector, VECTOR_SIZE));
}

void test_crc32_empty(void)
{
    /* no data: init 0xFFFFFFFF xorout 0xFFFFFFFF -> 0x00000000 */
    TEST_ASSERT_EQUAL_HEX32(0x00000000UL, common_crc32_calc(NULL, 0U));
}

void test_crc32_single_char(void)
{
    /* well-known zlib value: crc32("a") = 0xE8B7BE43 */
    TEST_ASSERT_EQUAL_HEX32(0xE8B7BE43UL, common_crc32_calc((const uint8_t *)"a", 1U));
}
