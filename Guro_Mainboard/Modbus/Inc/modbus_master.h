/**
 * @file modbus_master.h
 * @brief MAIN board: Modbus Master transaction layer (one transaction per Poll).
 */
#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "io_map.h"
#include "modbus_table.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ModbusMaster_Init(void);
void ModbusMaster_Poll(void);

/* Optional: request write (queued or immediate). Use enum addresses. */
int ModbusMaster_WriteCoil(SlaveId_t slave, uint16_t coil_addr, uint8_t value);
int ModbusMaster_WriteHoldingReg(SlaveId_t slave, uint16_t reg_addr, uint16_t value);

/* Communication status for application */
uint8_t ModbusMaster_GetLastSlaveResponded(void);
uint8_t ModbusMaster_IsCommOk(SlaveId_t slave);

/* Optional debug log (UART2 하위 폴링): MODBUS_MASTER_DEBUG_LOG=1 시 구현체에서 UART 출력 */
void ModbusMaster_LogSubPollStart(uint8_t slave_id);
void ModbusMaster_LogSubPollOk(uint8_t slave_id);
void ModbusMaster_LogSubPollFail(uint8_t slave_id, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_H */
