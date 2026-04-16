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

/** 메인 루프에서 주기 호출. MODBUS_RTU_IDLE_MS 경과 시 프레임 처리 후 응답 전송. */
void Modbus_Poll(void);

/** 수신 프레임 버퍼 크기 */
#define MODBUS_RX_BUF_SIZE  64

/** 프레임 간 무음(ms). 38400에서 t3.5≈0.9ms; 4ms는 여유 포함. */
#define MODBUS_RTU_IDLE_MS  4

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
