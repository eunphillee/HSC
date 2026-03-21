/**
 * @file modbus_slave.h
 * @brief LPSB: Modbus RTU Slave - FC01/02/03/04/05/06/15/16.
 */
#ifndef MODBUS_SLAVE_LPSB_H
#define MODBUS_SLAVE_LPSB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ModbusSlave_Init(void);
void ModbusSlave_Poll(void);
/** Runtime slave address from ID_BIT (2=LPSB1, 4=LPSB2, 8=LPSB3). For debug/LED. */
uint8_t ModbusSlave_GetAddress(void);
/* weak 기본 no-op. 필요 시 별도 UART/SWO로 오버라이드해 슬레이브 로그 출력. */
void LPSB_Debug_Log(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_LPSB_H */
