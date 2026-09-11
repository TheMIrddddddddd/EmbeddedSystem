#ifndef BOARD_DMA_MAP_H
#define BOARD_DMA_MAP_H

#include "gd32f4xx.h"
#include "gd32f4xx_dma.h"

/*
 * DMA 资源表 —— 全工程 DMA 通道分配的唯一有效位置（《01》十七-4）。
 *
 * 每个条目五个字段：
 *   _PERIPH      SPL 控制器地址宏（DMA0/DMA1），直接传给 SPL API
 *   _CHANNEL     SPL 通道枚举（DMA_CH0..DMA_CH7），直接传给 SPL API
 *   _CHANNEL_NO  通道号的十进制数字，仅供本表冲突检查使用：
 *                DMA_CHx 是 enum 值，预处理器看不见，不能出现在 #if 里
 *   _SUBPERI     请求复用选择，传给 dma_channel_subperipheral_select()
 *   _IRQn        该通道的 DMA 中断号，传给 nvic_irq_enable()
 *
 * 新增外设步骤：照抄一条五字段，加一个 _SLOT 宏，
 * 并把该宏追加到底部冲突检查的两侧。
 */

/* ---- ADC1：PC0/PC1 双通道采集（M4-1） ---- */
#define BOARD_ADC1_DMA_PERIPH              DMA1
#define BOARD_ADC1_DMA_CHANNEL             DMA_CH2
#define BOARD_ADC1_DMA_CHANNEL_NO          2U
#define BOARD_ADC1_DMA_SUBPERI             DMA_SUBPERI1
#define BOARD_ADC1_DMA_IRQn                DMA1_Channel2_IRQn
#define BOARD_ADC1_DMA_TRANSFER_COUNT      2U
#define BOARD_DMA_SLOT_ADC1                (1UL << (8U + BOARD_ADC1_DMA_CHANNEL_NO))

/* ---- USART0 RX：PA10，调试口 CH340/COM4（M3） ---- */
#define BOARD_USART0_RX_DMA_PERIPH         DMA1
#define BOARD_USART0_RX_DMA_CHANNEL        DMA_CH5
#define BOARD_USART0_RX_DMA_CHANNEL_NO     5U
#define BOARD_USART0_RX_DMA_SUBPERI        DMA_SUBPERI4
#define BOARD_USART0_RX_DMA_IRQn           DMA1_Channel5_IRQn
#define BOARD_DMA_SLOT_USART0_RX           (1UL << (8U + BOARD_USART0_RX_DMA_CHANNEL_NO))

/* ---- USART1 RX：PA3，RS485（M3） ---- */
#define BOARD_USART1_RS485_RX_DMA_PERIPH      DMA0
#define BOARD_USART1_RS485_RX_DMA_CHANNEL     DMA_CH5
#define BOARD_USART1_RS485_RX_DMA_CHANNEL_NO  5U
#define BOARD_USART1_RS485_RX_DMA_SUBPERI     DMA_SUBPERI4
#define BOARD_USART1_RS485_RX_DMA_IRQn        DMA0_Channel5_IRQn
#define BOARD_DMA_SLOT_USART1_RX              (1UL << BOARD_USART1_RS485_RX_DMA_CHANNEL_NO)

/* ---- SDIO：TF 卡（M3） ---- */
#define BOARD_SDIO_DMA_PERIPH              DMA1
#define BOARD_SDIO_DMA_CHANNEL             DMA_CH6
#define BOARD_SDIO_DMA_CHANNEL_NO          6U
#define BOARD_SDIO_DMA_SUBPERI             DMA_SUBPERI4
#define BOARD_SDIO_DMA_IRQn                DMA1_Channel6_IRQn
#define BOARD_DMA_SLOT_SDIO                (1UL << (8U + BOARD_SDIO_DMA_CHANNEL_NO))

/*
 * 编译期冲突检查。
 * 槽位号 = 控制器*8 + 通道号，每个(控制器,通道)组合占一个 bit。
 * 若两个条目占用同一槽位，按位或结果会比相加结果少一个 bit，
 * 两者不等即触发 #error。
 * 注意：这个检查必须在 _CHANNEL_NO 这种预处理器可见的数字上进行，
 * 直接比较 DMA_CHx 枚举时预处理器会把两边都当成 0，永远检查不出冲突。
 */
#if ((BOARD_DMA_SLOT_ADC1 | BOARD_DMA_SLOT_USART0_RX | \
      BOARD_DMA_SLOT_USART1_RX | BOARD_DMA_SLOT_SDIO) != \
     (BOARD_DMA_SLOT_ADC1 + BOARD_DMA_SLOT_USART0_RX + \
      BOARD_DMA_SLOT_USART1_RX + BOARD_DMA_SLOT_SDIO))
#error "board_dma_map: DMA resource conflict - same controller+channel assigned to more than one peripheral"
#endif

#endif /* BOARD_DMA_MAP_H */
