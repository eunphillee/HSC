/**
 ******************************************************************************
 * @file    lpsb_app.h
 * @brief   LPSB 보드 앱: LED, SSR, ID 스위치, heartbeat.
 ******************************************************************************
 */
#ifndef LPSB_APP_H
#define LPSB_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 부팅 시 1회: LED01~04 순차 점등 (검증용). */
void LPSB_LED_Sequence(void);

/** 통신 수신 시: LED04 짧게 점멸 (수신 표시). */
void LPSB_LED_RxBlink(void);

/** heartbeat: 주기적으로 호출. LED01으로 동작 표시. */
void LPSB_Heartbeat(void);

/** SSR 채널 0~2. on: 1=ON, 0=OFF. */
void LPSB_SSR_Set(uint8_t ch, uint8_t on);

/** SSR 상태 읽기. ch 0~2, return 0 or 1. */
uint8_t LPSB_SSR_Get(uint8_t ch);

/** ID 스위치(PB0,PB1,PB3,PB4) 읽어 Slave ID 계산. 1~15, 잘못되면 2. */
uint8_t LPSB_ID_Read(void);

/** 부팅 시 1회 호출. ID 스위치 읽어 Slave ID 저장. */
void LPSB_App_Init(void);

/** 저장된 Slave ID (LPSB_App_Init 호출 후 유효). */
uint8_t LPSB_GetSlaveID(void);

/** heartbeat 카운터 (Modbus 상태용). */
uint16_t LPSB_GetHeartbeatCount(void);

/** 펌웨어 버전 (예: 0x0001). */
#define LPSB_FW_VERSION  0x0002u

#ifdef __cplusplus
}
#endif

#endif /* LPSB_APP_H */
