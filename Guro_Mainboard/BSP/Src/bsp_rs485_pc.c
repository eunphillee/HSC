/**
  * @file bsp_rs485_pc.c
 * @brief PC RS485: USART1 (PA9/PA10), DE/RE = PB1. Default LOW = RX mode.
 */
#include "bsp_rs485_pc.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

#define DE_GPIO_Port  RS485_DE_GPIO_Port
#define DE_Pin        RS485_DE_Pin
#define TX_GUARD_MS   2

void BSP_RS485_PC_Init(void)
{
	BSP_RS485_PC_SetDE_RX();  /* Initial state: RX (LOW) */
}

void BSP_RS485_PC_SetDE_TX(void)
{
	HAL_GPIO_WritePin(DE_GPIO_Port, DE_Pin, GPIO_PIN_SET);
}

void BSP_RS485_PC_SetDE_RX(void)
{
	HAL_GPIO_WritePin(DE_GPIO_Port, DE_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef BSP_RS485_PC_Transmit(uint8_t *buf, uint16_t len)
{
	BSP_RS485_PC_SetDE_TX();
	HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart1, buf, len, 100);
	BSP_RS485_PC_SetDE_RX();
	if (TX_GUARD_MS > 0) {
		HAL_Delay(TX_GUARD_MS);
	}
	return ret;
}

UART_HandleTypeDef *BSP_RS485_PC_GetUartHandle(void)
{
	return &huart1;
}
