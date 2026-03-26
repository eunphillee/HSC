/**
 ******************************************************************************
 * @file    modbus_slave.h
 * @brief   LPSB Modbus RTU Slave. FC04(Read Input Regs), FC05(Write Single Coil).
 ******************************************************************************
 */
#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 초기화: RX 모드 설정 + HAL_UART_Receive_IT 시작. main()에서 1회 호출. */
void Modbus_Init(void);

/** 메인 루프에서 주기 호출. FRAME_SILENCE_MS 경과 시 프레임 처리 후 응답 전송. */
void Modbus_Poll(void);

/** 수신 프레임 버퍼 크기 */
#define MODBUS_RX_BUF_SIZE  64

/** 3.5 char time @ 9600 baud (ms). */
#define MODBUS_RTU_IDLE_MS  4

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
