#include <string.h>
#include "common_crc.h"

#define CRC16_POLY_REFLECTED    0XA001U
#define CRC32_POLY_REFLECTED    0XEDB88320UL

uint16_t common_crc16_calc(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    if ((data == NULL) || (len == 0U))
    {
        return 0XFFFFU;
    }
    
    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (bit = 0; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (crc >> 1) ^ CRC16_POLY_REFLECTED;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t common_crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0XFFFFFFFFUL;
    uint32_t i;
    uint32_t bit;

    if ((data == NULL) || (len == 0U))
    {
        return 0x00000000UL;
    }

    for (i = 0U; i < len; i++)
    {
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x00000001UL) != 0U)
            {
                crc = (crc >> 1) ^ CRC32_POLY_REFLECTED;
            }
            else
            {
                crc >>= 1;
            }
            
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}
