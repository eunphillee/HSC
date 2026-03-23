/**
 * @file modbus_table.c
 * @brief HPSB: Modbus address table implementation. Coil/Discrete from io_map; Holding/Input in RAM.
 */
#include "modbus_table.h"
#include "io_map.h"
#include "hpsb_ct_adc.h"
#include <string.h>

static uint8_t  discrete_image[DISCRETE_COUNT];
static uint16_t holding_regs[HOLDING_REG_COUNT];
static uint16_t input_regs[INPUT_REG_COUNT];

uint8_t ModbusTable_GetCoil(uint16_t addr)
{
    if (addr >= COIL_COUNT) return 0;
    return IO_HPSB_ReadCoil(addr);
}

void ModbusTable_SetCoil(uint16_t addr, uint8_t value)
{
    if (addr >= COIL_COUNT) return;
    IO_HPSB_WriteCoil(addr, value);
}

void ModbusTable_SetCoilBytes(const uint8_t *bytes, uint16_t num_bits)
{
    ModbusTable_SetCoilBytesFrom(0, bytes, num_bits);
}

void ModbusTable_SetCoilBytesFrom(uint16_t start_addr, const uint8_t *bytes, uint16_t num_bits)
{
    for (uint16_t i = 0; i < num_bits && (start_addr + i) < COIL_COUNT; i++)
        ModbusTable_SetCoil(start_addr + i, (bytes[i / 8] >> (i % 8)) & 1u);
}

uint8_t ModbusTable_GetDiscrete(uint16_t addr)
{
    if (addr >= DISCRETE_COUNT) return 0;
    return discrete_image[addr];
}

void ModbusTable_RefreshDiscrete(void)
{
    IO_HPSB_ReadAllDiscrete(discrete_image);
}

uint16_t ModbusTable_GetHoldingReg(uint16_t addr)
{
    if (addr >= HOLDING_REG_COUNT) return 0;
    return holding_regs[addr];
}

void ModbusTable_SetHoldingReg(uint16_t addr, uint16_t value)
{
    if (addr >= HOLDING_REG_COUNT) return;
    holding_regs[addr] = value;
}

void ModbusTable_SetHoldingRegs(uint16_t start, const uint16_t *regs, uint16_t num)
{
    for (uint16_t i = 0; i < num && (start + i) < HOLDING_REG_COUNT; i++)
        holding_regs[start + i] = regs[i];
}

uint16_t ModbusTable_GetInputReg(uint16_t addr)
{
    if (addr >= INPUT_REG_COUNT) return 0;
    return input_regs[addr];
}

void ModbusTable_RefreshInputRegs(void)
{
    ModbusTable_RefreshDiscrete();
    uint8_t byte = 0;
    for (uint16_t i = 0; i < 8 && i < DISCRETE_COUNT; i++)
        byte |= (discrete_image[i] ? (1u << i) : 0);
    input_regs[HPSB_INPUT_REG_DISCRETE_IMAGE] = (uint16_t)byte;
    {
        uint16_t a[3], p[3], c[3];
        HpsbCtAdc_GetSnapshot(a, p, c);
        input_regs[HPSB_INPUT_REG_CT_CH1_AVG] = a[0];
        input_regs[HPSB_INPUT_REG_CT_CH2_AVG] = a[1];
        input_regs[HPSB_INPUT_REG_CT_CH3_AVG] = a[2];
        input_regs[HPSB_INPUT_REG_CT_CH1_PKPK] = p[0];
        input_regs[HPSB_INPUT_REG_CT_CH2_PKPK] = p[1];
        input_regs[HPSB_INPUT_REG_CT_CH3_PKPK] = p[2];
        input_regs[HPSB_INPUT_REG_CT_CH1_CURRENT] = c[0];
        input_regs[HPSB_INPUT_REG_CT_CH2_CURRENT] = c[1];
        input_regs[HPSB_INPUT_REG_CT_CH3_CURRENT] = c[2];
    }
}
