/**
 * @file modbus_slave.h
 * @brief HPSB: Modbus RTU Slave - FC01/02/03/04/05/06/15/16.
 */
#ifndef MODBUS_SLAVE_HPSB_H
#define MODBUS_SLAVE_HPSB_H

#ifdef __cplusplus
extern "C" {
#endif

void ModbusSlave_Init(void);
void ModbusSlave_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_HPSB_H */
