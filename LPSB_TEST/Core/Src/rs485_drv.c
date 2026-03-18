/**
 ******************************************************************************
 * @file    rs485_drv.c
 * @brief   RS485 half-duplex: DE(PA11) 제어, 송신 시 DE HIGH → 전송 → TC 대기 → DE LOW.
 ******************************************************************************
 */
#include "rs485_drv.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

void RS485_SetRxMode(void)
{
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
}

/* LED4: RS485 Activity 표시용 (TX/RX). main.c에서 GPIO 초기화됨. */
static uint32_t s_led4_last_activity = 0;

void RS485_SetTxMode(void)
{
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
}

void RS485_Send(const uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return;
  RS485_SetTxMode();
  /* RS485 TX 활동 표시: LED4 ON, 타임스탬프 기록 */
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET); /* LED4 active-low 라고 가정 시 조정 가능 */
  s_led4_last_activity = HAL_GetTick();
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 100u);
  /* TC 플래그로 전송 완료 확인 후 DE LOW */
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) { }
  RS485_SetRxMode();
}

void RS485_ActivityTick(void)
{
  /* 마지막 TX/RX 활동 후 50ms 경과 시 LED4 OFF */
  uint32_t now = HAL_GetTick();
  if ((now - s_led4_last_activity) > 50u)
  {
    HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET);
  }
}

void RS485_NotifyRxActivity(void)
{
  /* RX 인터럽트에서 호출: LED4 짧게 ON */
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET);
  s_led4_last_activity = HAL_GetTick();
}
