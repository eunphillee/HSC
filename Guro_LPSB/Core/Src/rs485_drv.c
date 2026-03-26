/**
 ******************************************************************************
 * @file    rs485_drv.c
 * @brief   RS485 half-duplex: DE(PA11) 제어, 송신 시 DE HIGH → 전송 → TC 대기 → DE LOW.
 ******************************************************************************
 */
#include "rs485_drv.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

/* LED4: 평상시 ON, RS485 RX/TX 활동 시 80ms 동안 OFF (activity blink). */
#define LED4_BLINK_OFF_MS  80u
static volatile uint32_t s_led4_last_activity = 0u;

void RS485_SetRxMode(void)
{
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
}

void RS485_SetTxMode(void)
{
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
}

void RS485_Send(const uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return;
  (void)HAL_UART_AbortReceive_IT(&huart1);
  RS485_SetTxMode();
  HAL_Delay(1);
  s_led4_last_activity = HAL_GetTick();
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 100u);
  /* TC 플래그로 전송 완료 확인 후 DE LOW */
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) { }
  RS485_SetRxMode();
  /* RX IT 재등록은 Modbus_Poll()이 처리 */
}

void RS485_ActivityTick(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_led4_last_activity) < LED4_BLINK_OFF_MS) {
        HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET);   /* OFF (active-low) */
    } else {
        HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET); /* ON  (active-low) */
    }
}

void RS485_NotifyRxActivity(void)
{
    s_led4_last_activity = HAL_GetTick();
}
