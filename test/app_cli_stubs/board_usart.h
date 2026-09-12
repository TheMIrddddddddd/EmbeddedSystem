#ifndef TEST_APP_CLI_BOARD_USART_H
#define TEST_APP_CLI_BOARD_USART_H

#include <stdint.h>

uint16_t board_usart0_tx_free_get(void);
uint16_t board_usart0_send_buffer(const uint8_t *data, uint16_t length);
void board_usart1_rs485_baudrate_set(uint32_t baudrate);

#endif /* TEST_APP_CLI_BOARD_USART_H */
