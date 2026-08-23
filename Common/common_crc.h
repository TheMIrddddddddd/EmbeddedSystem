#ifndef COMMON_CRC_H
#define COMMON_CRC_H

#include <stdint.h>

uint16_t common_crc16_calc(const uint8_t *data, uint32_t len);
uint32_t common_crc32_calc(const uint8_t *data, uint32_t len);

#endif /* COMMON_CRC_H */