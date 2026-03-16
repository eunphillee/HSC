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

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_LPSB_H */
