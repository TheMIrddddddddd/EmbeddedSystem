#ifndef BOARD_DAC_H
#define BOARD_DAC_H

#include <stdint.h>

int board_dac_init(void);
int board_dac_output_set(uint16_t value);
uint16_t board_dac_output_get(void);

#endif /* BOARD_DAC_H */
