/**
 * @file modbus_table.c
 * @brief MAIN board: polling table definition and slave image storage.
 */
#include "modbus_table.h"
#include <string.h>

/* Per-slave images. Index 0 = HPSB, 1 = LPSB (use slave_id - 1). */
static uint8_t  discrete_img[SLAVE_ID_COUNT][MODBUS_DISCRETE_COUNT];
static uint8_t  coil_img[SLAVE_ID_COUNT][MODBUS_COIL_COUNT];
static uint16_t holding_img[SLAVE_ID_COUNT][MODBUS_HOLDING_COUNT];
static uint16_t input_reg_img[SLAVE_ID_COUNT][MODBUS_INPUT_REG_COUNT];

static const PollEntry_t poll_table[POLL_TABLE_SIZE] = {
    /* HPSB */
    { SLAVE_ID_HPSB, POLL_ENTRY_READ_DISCRETE,  MODBUS_DISCRETE_START, MODBUS_DISCRETE_COUNT },
    { SLAVE_ID_HPSB, POLL_ENTRY_READ_COIL,      MODBUS_COIL_START,     MODBUS_COIL_COUNT },
    { SLAVE_ID_HPSB, POLL_ENTRY_READ_HOLDING,   MODBUS_HOLDING_START, MODBUS_HOLDING_COUNT },
    { SLAVE_ID_HPSB, POLL_ENTRY_READ_INPUT_REG, MODBUS_INPUT_REG_START, MODBUS_INPUT_REG_COUNT },
    /* LPSB */
    { SLAVE_ID_LPSB, POLL_ENTRY_READ_DISCRETE,  MODBUS_DISCRETE_START, MODBUS_DISCRETE_COUNT },
    { SLAVE_ID_LPSB, POLL_ENTRY_READ_COIL,      MODBUS_COIL_START,     MODBUS_COIL_COUNT },
    { SLAVE_ID_LPSB, POLL_ENTRY_READ_HOLDING,   MODBUS_HOLDING_START, MODBUS_HOLDING_COUNT },
    { SLAVE_ID_LPSB, POLL_ENTRY_READ_INPUT_REG, MODBUS_INPUT_REG_START, MODBUS_INPUT_REG_COUNT },
};

static inline uint8_t slave_idx(SlaveId_t slave)
{
    return (uint8_t)(slave - SLAVE_ID_FIRST);
}

int ModbusTable_GetPollEntry(uint8_t index, PollEntry_t *entry)
{
    if (index >= POLL_TABLE_SIZE || entry == NULL) return -1;
    *entry = poll_table[index];
    return 0;
}

uint8_t ModbusTable_GetDiscrete(SlaveId_t slave, uint16_t bit_index)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bit_index >= MODBUS_DISCRETE_COUNT)
        return 0;
    return discrete_img[slave_idx(slave)][bit_index];
}

uint8_t ModbusTable_GetCoil(SlaveId_t slave, uint16_t bit_index)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bit_index >= MODBUS_COIL_COUNT)
        return 0;
    return coil_img[slave_idx(slave)][bit_index];
}

uint16_t ModbusTable_GetHoldingReg(SlaveId_t slave, uint16_t reg_index)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || reg_index >= MODBUS_HOLDING_COUNT)
        return 0;
    return holding_img[slave_idx(slave)][reg_index];
}

uint16_t ModbusTable_GetInputReg(SlaveId_t slave, uint16_t reg_index)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || reg_index >= MODBUS_INPUT_REG_COUNT)
        return 0;
    return input_reg_img[slave_idx(slave)][reg_index];
}

void ModbusTable_SetDiscrete(SlaveId_t slave, uint16_t bit_index, uint8_t value)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bit_index >= MODBUS_DISCRETE_COUNT) return;
    discrete_img[slave_idx(slave)][bit_index] = value ? 1 : 0;
}

void ModbusTable_SetCoil(SlaveId_t slave, uint16_t bit_index, uint8_t value)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bit_index >= MODBUS_COIL_COUNT) return;
    coil_img[slave_idx(slave)][bit_index] = value ? 1 : 0;
}

void ModbusTable_SetHoldingReg(SlaveId_t slave, uint16_t reg_index, uint16_t value)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || reg_index >= MODBUS_HOLDING_COUNT) return;
    holding_img[slave_idx(slave)][reg_index] = value;
}

void ModbusTable_SetInputReg(SlaveId_t slave, uint16_t reg_index, uint16_t value)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || reg_index >= MODBUS_INPUT_REG_COUNT) return;
    input_reg_img[slave_idx(slave)][reg_index] = value;
}

void ModbusTable_SetDiscreteBytes(SlaveId_t slave, const uint8_t *bytes, uint16_t num_bits)
{
    uint8_t si = slave_idx(slave);
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bytes == NULL) return;
    for (uint16_t i = 0; i < num_bits && i < MODBUS_DISCRETE_COUNT; i++)
        discrete_img[si][i] = (bytes[i / 8] >> (i % 8)) & 1u;
}

void ModbusTable_SetCoilBytes(SlaveId_t slave, const uint8_t *bytes, uint16_t num_bits)
{
    uint8_t si = slave_idx(slave);
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || bytes == NULL) return;
    for (uint16_t i = 0; i < num_bits && i < MODBUS_COIL_COUNT; i++)
        coil_img[si][i] = (bytes[i / 8] >> (i % 8)) & 1u;
}

void ModbusTable_SetHoldingRegs(SlaveId_t slave, uint16_t start, const uint16_t *regs, uint16_t num)
{
    uint8_t si = slave_idx(slave);
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || regs == NULL) return;
    for (uint16_t i = 0; i < num && (start + i) < MODBUS_HOLDING_COUNT; i++)
        holding_img[si][start + i] = regs[i];
}

void ModbusTable_SetInputRegs(SlaveId_t slave, uint16_t start, const uint16_t *regs, uint16_t num)
{
    uint8_t si = slave_idx(slave);
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST || regs == NULL) return;
    for (uint16_t i = 0; i < num && (start + i) < MODBUS_INPUT_REG_COUNT; i++)
        input_reg_img[si][start + i] = regs[i];
}

void ModbusTable_ClearAllImages(void)
{
    memset(discrete_img, 0, sizeof(discrete_img));
    memset(coil_img, 0, sizeof(coil_img));
    memset(holding_img, 0, sizeof(holding_img));
    memset(input_reg_img, 0, sizeof(input_reg_img));
}
