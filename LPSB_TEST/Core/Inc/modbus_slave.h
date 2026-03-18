/**
 ******************************************************************************
 * @file    modbus_slave.h
 * @brief   LPSB Modbus RTU Slave. FC03(Read Holding), FC01(Read Coils), FC05(Write Single Coil).
 ******************************************************************************
 */
#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** UART에서 1바이트 수신 시 호출. (HAL_UART_RxCpltCallback에서 호출) */
void Modbus_PushByte(uint8_t b);

/** 메인 루프에서 주기 호출. 3.5 char idle 경과 시 프레임 처리 후 응답 전송. */
void Modbus_Poll(void);

/** 수신 프레임 버퍼 크기 */
#define MODBUS_RX_BUF_SIZE  64

/** 3.5 char time @ 9600 baud (ms). */
#define MODBUS_RTU_IDLE_MS  4

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
