/**
 * @file modbus_table.h
 * @brief LPSB: Modbus slave address table - get/set Coil, Discrete, Holding, Input Reg.
 */
#ifndef MODBUS_TABLE_LPSB_H
#define MODBUS_TABLE_LPSB_H

#include "io_map.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ModbusTable_GetCoil(uint16_t addr);
void    ModbusTable_SetCoil(uint16_t addr, uint8_t value);
void    ModbusTable_SetCoilBytes(const uint8_t *bytes, uint16_t num_bits);
void    ModbusTable_SetCoilBytesFrom(uint16_t start_addr, const uint8_t *bytes, uint16_t num_bits);

uint8_t ModbusTable_GetDiscrete(uint16_t addr);
void    ModbusTable_RefreshDiscrete(void);

uint16_t ModbusTable_GetHoldingReg(uint16_t addr);
void     ModbusTable_SetHoldingReg(uint16_t addr, uint16_t value);
void     ModbusTable_SetHoldingRegs(uint16_t start, const uint16_t *regs, uint16_t num);

uint16_t ModbusTable_GetInputReg(uint16_t addr);
void     ModbusTable_RefreshInputRegs(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TABLE_LPSB_H */
