/**
 * @file modbus_table.h
 * @brief MAIN board: Modbus polling table and slave image buffers.
 *        All addressing via enums/constants from io_map.h.
 */
#ifndef MODBUS_TABLE_MAIN_H
#define MODBUS_TABLE_MAIN_H

#include "io_map.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single poll entry: one request type per slave area */
typedef enum {
    POLL_ENTRY_READ_DISCRETE,
    POLL_ENTRY_READ_COIL,
    POLL_ENTRY_READ_HOLDING,
    POLL_ENTRY_READ_INPUT_REG,
    POLL_ENTRY_COUNT
} PollEntryType_t;

typedef struct {
    SlaveId_t        slave_id;
    PollEntryType_t   entry_type;
    uint16_t         start_addr;
    uint16_t         count;
} PollEntry_t;

/* Number of poll entries in the table (one per slave per read type) */
#define POLL_TABLE_SIZE   (SLAVE_ID_COUNT * 4)   /* HPSB: 4 read types, LPSB: 4 read types */

/* Get poll entry by index (0 .. POLL_TABLE_SIZE-1). Returns 0 on success. */
int ModbusTable_GetPollEntry(uint8_t index, PollEntry_t *entry);

/* Slave image buffers: updated by Modbus Master when response received */
uint8_t  ModbusTable_GetDiscrete(SlaveId_t slave, uint16_t bit_index);
uint8_t  ModbusTable_GetCoil(SlaveId_t slave, uint16_t bit_index);
uint16_t ModbusTable_GetHoldingReg(SlaveId_t slave, uint16_t reg_index);
uint16_t ModbusTable_GetInputReg(SlaveId_t slave, uint16_t reg_index);

void ModbusTable_SetDiscrete(SlaveId_t slave, uint16_t bit_index, uint8_t value);
void ModbusTable_SetCoil(SlaveId_t slave, uint16_t bit_index, uint8_t value);
void ModbusTable_SetHoldingReg(SlaveId_t slave, uint16_t reg_index, uint16_t value);
void ModbusTable_SetInputReg(SlaveId_t slave, uint16_t reg_index, uint16_t value);

/* Bulk set from received bytes (LSB-packed) for discrete/coil */
void ModbusTable_SetDiscreteBytes(SlaveId_t slave, const uint8_t *bytes, uint16_t num_bits);
void ModbusTable_SetCoilBytes(SlaveId_t slave, const uint8_t *bytes, uint16_t num_bits);
void ModbusTable_SetHoldingRegs(SlaveId_t slave, uint16_t start, const uint16_t *regs, uint16_t num);
void ModbusTable_SetInputRegs(SlaveId_t slave, uint16_t start, const uint16_t *regs, uint16_t num);

void ModbusTable_ClearAllImages(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TABLE_MAIN_H */
