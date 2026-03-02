/**
 * @file pc_test_aa_stream.c
 * @brief PC 테스트: UART1(PA9/PA10, DE/RE=PB1)로 0xAA 1바이트 500ms 주기 송신, 송신 시 LED2 40ms 펄스.
 *        ENABLE_PC_TEST_AA_STREAM=1 일 때만 동작. set_de_tx() -> Transmit -> set_de_rx() -> guard 2ms.
 */
#include "pc_test_aa_stream.h"
#include "app_config.h"
#include "main.h"
#include "led_status.h"

#if ENABLE_PC_TEST_AA_STREAM

#define INTERVAL_MS    500u
#define TX_GUARD_MS    2u

extern UART_HandleTypeDef huart1;

/* 진단용: 송신 시 PB1=1, 수신 대기 시 PB1=0 고정 (RS485 DE active-high 기준). */
static void set_de_tx(void)
{
	HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);  /* PB1=1 보장 */
}
static void set_de_rx(void)
{
	HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET); /* PB1=0 보장 */
}

static uint32_t last_send_tick;

void PcTestAA_Init(void)
{
	last_send_tick = HAL_GetTick();
	set_de_rx();  /* PB1=0 */
	LED_Status_BootBlinkPcTestAA(3);  /* ENABLE_PC_TEST_AA_STREAM=1 빌드 확인: LED2 3회 빠른 점멸 */
}

void PcTestAA_Tick(const aggregated_status_t *agg)
{
	(void)agg;
	uint32_t now = HAL_GetTick();
	if ((now - last_send_tick) < INTERVAL_MS)
		return;
	last_send_tick = now;

	LED_Status_OnPcTestAASend();  /* LED2 40ms 펄스 */
	set_de_tx();   /* 송신 직전 반드시 호출 → PB1=1 */
	static const uint8_t byte_0xaa = 0xAA;
	HAL_StatusTypeDef ret = HAL_UART_Transmit(&huart1, (uint8_t *)&byte_0xaa, 1, 50);
	set_de_rx();   /* 송신 완료 후 PB1=0 */
	if (ret == HAL_OK)
		LED_Status_OnPcTestAATxOk();    /* LED3 20ms 펄스 */
	else
		LED_Status_OnPcTestAATxError();  /* LED4 200ms 점등 (HAL_BUSY/TIMEOUT/ERROR) */
	if (TX_GUARD_MS > 0)
		HAL_Delay(TX_GUARD_MS);
}

#else

void PcTestAA_Init(void)
{
	(void)0;
}

void PcTestAA_Tick(const aggregated_status_t *agg)
{
	(void)agg;
}

#endif /* ENABLE_PC_TEST_AA_STREAM */
