/**
 * @file bsp_rs485_pc.h
 * @brief PC ↔ Mainboard RS485 (USART1 + DE/RE = PB1).
 *        Mainboard = Slave ID 09. Initial DE state = LOW (RX mode).
 */
#ifndef BSP_RS485_PC_H
#define BSP_RS485_PC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f2xx_hal.h"

void BSP_RS485_PC_Init(void);
void BSP_RS485_PC_SetDE_TX(void);  /* Before transmit */
void BSP_RS485_PC_SetDE_RX(void);  /* After transmit */
HAL_StatusTypeDef BSP_RS485_PC_Transmit(uint8_t *buf, uint16_t len);
UART_HandleTypeDef *BSP_RS485_PC_GetUartHandle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RS485_PC_H */
