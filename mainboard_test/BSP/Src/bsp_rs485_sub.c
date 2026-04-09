/**
 * @file bsp_rs485_sub.c
 * @brief Subboard RS485: USART2 (PA2/PA3), DE/RE = PB12. Default LOW = RX mode.
 *        mainboard_test + MAINBOARD_TEST_PROFILE: PB12 DE 제어 없음(TTL 가정).
 */
#include "bsp_rs485_sub.h"
#include "mainboard_test_profile.h"
#include "main.h"

extern UART_HandleTypeDef huart2;
#include <stdint.h>

#if MAINBOARD_TEST_PROFILE

#define TX_GUARD_MS  2

void BSP_RS485_Sub_Init(void) { }

void BSP_RS485_Sub_SetDE_TX(void) { }

void BSP_RS485_Sub_SetDE_RX(void) { }

HAL_StatusTypeDef BSP_RS485_Sub_Transmit(uint8_t *buf, uint16_t len)
{
	HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart2, buf, len, 100);
	if (TX_GUARD_MS > 0) {
		HAL_Delay(TX_GUARD_MS);
	}
	return ret;
}

UART_HandleTypeDef *BSP_RS485_Sub_GetUartHandle(void)
{
	return &huart2;
}

#else

#define TX_GUARD_MS  2

void BSP_RS485_Sub_Init(void)
{
	BSP_RS485_Sub_SetDE_RX();  /* Initial state: RX (LOW) */
}

void BSP_RS485_Sub_SetDE_TX(void)
{
	HAL_GPIO_WritePin(RS485_DE_SUB_GPIO_Port, RS485_DE_SUB_Pin, GPIO_PIN_SET);
}

void BSP_RS485_Sub_SetDE_RX(void)
{
	HAL_GPIO_WritePin(RS485_DE_SUB_GPIO_Port, RS485_DE_SUB_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef BSP_RS485_Sub_Transmit(uint8_t *buf, uint16_t len)
{
	BSP_RS485_Sub_SetDE_TX();
	HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart2, buf, len, 100);
	BSP_RS485_Sub_SetDE_RX();
	if (TX_GUARD_MS > 0) {
		HAL_Delay(TX_GUARD_MS);
	}
	return ret;
}

UART_HandleTypeDef *BSP_RS485_Sub_GetUartHandle(void)
{
	return &huart2;
}

#endif /* MAINBOARD_TEST_PROFILE */
