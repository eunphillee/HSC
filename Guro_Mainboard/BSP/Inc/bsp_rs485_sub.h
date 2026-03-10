/**
 * @file bsp_rs485_sub.h
 * @brief Mainboard ↔ Subboard RS485 (USART2 + DE/RE = PB12).
 *        Mainboard = Master. Slave ID: HPSB=01, LPSB1=02, LPSB2=04, LPSB3=08.
 *        Initial DE state = LOW (RX mode).
 */
#ifndef BSP_RS485_SUB_H
#define BSP_RS485_SUB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f2xx_hal.h"

void BSP_RS485_Sub_Init(void);
void BSP_RS485_Sub_SetDE_TX(void);
void BSP_RS485_Sub_SetDE_RX(void);
HAL_StatusTypeDef BSP_RS485_Sub_Transmit(uint8_t *buf, uint16_t len);
UART_HandleTypeDef *BSP_RS485_Sub_GetUartHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RS485_SUB_H */
