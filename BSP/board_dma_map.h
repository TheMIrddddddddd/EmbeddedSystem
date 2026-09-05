#ifndef BOARD_DMA_MAP_H
#define BOARD_DMA_MAP_H

#include "gd32f4xx.h"
#include "gd32f4xx_dma.h"

/* ADC1 -> DMA1 Channel2 -> DMA_SUBPERI1 */
#define BOARD_ADC1_DMA_PERIPH       DMA1
#define BOARD_ADC1_DMA_CHANNEL      DMA_CH2
#define BOARD_ADC1_DMA_SUBPERI      DMA_SUBPERI1
#define BOARD_ADC1_DMA_IRQn         DMA1_Channel2_IRQn

#define BOARD_ADC1_DMA_TRANSFER_COUNT  2U

#endif /* BOARD_DMA_MAP_H */
