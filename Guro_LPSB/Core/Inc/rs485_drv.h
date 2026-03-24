/**
 ******************************************************************************
 * @file    rs485_drv.h
 * @brief   RS485 half-duplex driver (DE control, send). USART1 + PA11(DE).
 ******************************************************************************
 */
#ifndef RS485_DRV_H
#define RS485_DRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 송신 전 DE HIGH, 송신 완료(TC) 후 DE LOW. len 바이트 전송. */
void RS485_Send(const uint8_t *buf, uint16_t len);

/** 수신 모드 유지용: DE LOW (호출 후 수신 가능). */
void RS485_SetRxMode(void);

/** 송신 모드: DE HIGH (RS485_Send 내부에서 사용). */
void RS485_SetTxMode(void);

/** 주기적으로 호출하여 LED4 activity 타임아웃(≈50ms) 처리. */
void RS485_ActivityTick(void);

/** UART RX 수신 시 호출하여 LED4 activity 갱신. */
void RS485_NotifyRxActivity(void);

#ifdef __cplusplus
}
#endif

#endif /* RS485_DRV_H */
