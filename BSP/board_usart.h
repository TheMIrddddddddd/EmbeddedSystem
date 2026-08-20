#ifndef BOARD_USART_H
#define BOARD_USART_H

#include <stdint.h>

void board_usart0_init(void);

void board_usart0_send_byte(uint8_t data);
uint16_t board_usart0_send_buffer(const uint8_t *data, uint16_t length);

uint8_t board_usart0_try_receive_byte(uint8_t *data);
uint8_t board_usart1_rs485_try_receive_byte(uint8_t *data);

void board_usart0_irq_handler(void);
void board_usart0_rx_dma_irq_handler(void);
void board_usart1_rs485_irq_handler(void);
void board_usart1_rs485_rx_dma_irq_handler(void);

uint32_t board_usart0_rx_overflow_count_get(void);
uint32_t board_usart0_tx_drop_count_get(void);
uint32_t board_usart0_rx_dma_error_count_get(void);

void board_usart1_rs485_init(void);

void board_usart1_rs485_send_buffer(const uint8_t *data, uint16_t length);

#endif /* BOARD_USART_H */

