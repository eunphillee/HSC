/**
 * @file modbus_table.h
 * @brief HPSB: Modbus slave address table - get/set Coil, Discrete, Holding, Input Reg.
 */
#ifndef MODBUS_TABLE_HPSB_H
#define MODBUS_TABLE_HPSB_H

#include "io_map.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Coil (0x) - read/write via IO */
uint8_t ModbusTable_GetCoil(uint16_t addr);
void    ModbusTable_SetCoil(uint16_t addr, uint8_t value);
void    ModbusTable_SetCoilBytes(const uint8_t *bytes, uint16_t num_bits);
void    ModbusTable_SetCoilBytesFrom(uint16_t start_addr, const uint8_t *bytes, uint16_t num_bits);

/* Discrete (1x) - read-only from IO */
uint8_t ModbusTable_GetDiscrete(uint16_t addr);
void    ModbusTable_RefreshDiscrete(void);  /* copy GPIO to image */

/* Holding (4x) - read/write */
uint16_t ModbusTable_GetHoldingReg(uint16_t addr);
void     ModbusTable_SetHoldingReg(uint16_t addr, uint16_t value);
void     ModbusTable_SetHoldingRegs(uint16_t start, const uint16_t *regs, uint16_t num);

/* Input Reg (3x) - read-only */
uint16_t ModbusTable_GetInputReg(uint16_t addr);
void     ModbusTable_RefreshInputRegs(void);  /* build from discrete image + ADC etc. */

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TABLE_HPSB_H */
