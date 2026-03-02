/**
 * @file led_status.h
 * @brief MAIN board LED1~4 status: PWR always on, DI/DO/RS485 event pulses.
 *        Polling/timer-based; call LED_Status_Tick_1ms() from 1ms (or 10ms) periodic.
 */
#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void LED_Status_Init(void);
void LED_Status_Tick_1ms(void);

void LED_Status_OnDIChanged(void);
void LED_Status_OnDOChanged(void);
void LED_Status_OnRS485Activity(void);

/** UART1 Modbus Slave: pulse LED3 (DO) when response sent (for PC test visibility). */
void LED_Status_OnUart1SlaveTx(void);

/** UART1 raw receive event (≥1 byte): pulse LED2 for 50ms; use to confirm "receive present" / program download. */
void LED_Status_OnUart1RxEvent(void);

/** UART1 CRC OK frame: pulse LED3 50ms (수신→CRCOK→응답송신 3단계 디버그). */
void LED_Status_OnUart1CrcOk(void);

/** UART1 응답 송신 직전: pulse LED2 50ms (3단계 디버그). */
void LED_Status_OnUart1TxRespBefore(void);

/** PC 테스트 0xAA 송신 시작 시 LED2 40ms 펄스 (ENABLE_PC_TEST_AA_STREAM=1). */
void LED_Status_OnPcTestAASend(void);

/** PC 테스트 0xAA: HAL_UART_Transmit HAL_OK 시 LED3 20ms 펄스. */
void LED_Status_OnPcTestAATxOk(void);

/** PC 테스트 0xAA: HAL_UART_Transmit HAL_BUSY/TIMEOUT/ERROR 시 LED4 200ms 점등. */
void LED_Status_OnPcTestAATxError(void);

/** PC 테스트 빌드 모드 확인: LED2를 count번 빠르게 점멸 (부팅 시 3회 호출). */
void LED_Status_BootBlinkPcTestAA(uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* LED_STATUS_H */
