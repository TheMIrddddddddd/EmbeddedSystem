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
uint16_t board_usart0_tx_free_get(void);

void board_usart1_rs485_init(void);

void board_usart1_rs485_send_buffer(const uint8_t *data, uint16_t length);

/* 运行期修改 RS485 波特率（CLI 或协议应答完成后），立即生效 */
void board_usart1_rs485_baudrate_set(uint32_t baudrate);

/* Modbus RTU 接收帧事件返回值 */
#define BOARD_USART1_RS485_RX_FRAME_NONE          0U
#define BOARD_USART1_RS485_RX_FRAME_READY         1U
#define BOARD_USART1_RS485_RX_FRAME_OVERFLOW      2U
#define BOARD_USART1_RS485_RX_FRAME_BUFFER_SMALL  3U
#define BOARD_USART1_RS485_RX_FRAME_GAP_ERROR     4U

/*
 * Modbus RTU 模式切换时调用。
 * enabled = 1：8E1 逐字节中断接收，等待初始静默；
 * enabled = 0：8N1 DMA+IDLE 接收，清空旧接收状态。
 * 仅由通信所有者在发送完成（TC）后调用。
 */
void board_usart1_rs485_rtu_receive_enable(uint8_t enabled);

/*
 * 从已经完成静默定界的队列中取出一帧。
 *
 * 返回：
 *   NONE         没有完整帧；
 *   READY        成功取出完整帧；
 *   OVERFLOW     当前帧或接收队列发生溢出，该帧应丢弃；
 *   BUFFER_SMALL 目标缓冲区太小，事件暂时保留。
 *   GAP_ERROR    帧内间隔非法，当前帧已消费，length=0。
 */
uint8_t board_usart1_rs485_rtu_frame_receive(uint8_t *buffer,
                                             uint16_t buffer_size,
                                             uint16_t *length);

uint32_t board_usart1_rs485_rtu_frame_end_count_get(void);
uint32_t board_usart1_rs485_rtu_frame_overflow_count_get(void);
uint32_t board_usart1_rs485_rtu_alarm_error_count_get(void);
uint32_t board_usart1_rs485_rtu_t3_5_us_get(void);
uint32_t board_usart1_rs485_rtu_t1_5_us_get(void);
uint32_t board_usart1_rs485_rtu_gap_error_count_get(void);

#endif /* BOARD_USART_H */

