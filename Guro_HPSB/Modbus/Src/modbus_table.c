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
    /* Unified Rule v1.1 HPSB FC04 map (reg0~15):
     * reg0  = alive/status (1=정상)
     * reg1  = error code
     * reg2~5= relay1~4 state
     * reg6~8= ADC1~3 AVG
     * reg9~11= ADC1~3 PKPK
     * reg12~14= Current1~3 state (0/1)
     * reg15 reserve
     */
    uint16_t avg[3] = {0u, 0u, 0u};
    uint16_t pkpk[3] = {0u, 0u, 0u};
    uint16_t cur_on[3] = {0u, 0u, 0u};
    HpsbCtAdc_GetSnapshot(avg, pkpk, cur_on);

    input_regs[HPSB_INPUT_REG_ALIVE_STATUS] = 1u;
    input_regs[HPSB_INPUT_REG_ERROR_CODE] = 0u;

    /* relay state = coil 0..3 (coil 3 may be reserved/not wired on some boards) */
    input_regs[HPSB_INPUT_REG_RELAY1_STATE] = (uint16_t)IO_HPSB_ReadCoil(0);
    input_regs[HPSB_INPUT_REG_RELAY2_STATE] = (uint16_t)IO_HPSB_ReadCoil(1);
    input_regs[HPSB_INPUT_REG_RELAY3_STATE] = (uint16_t)IO_HPSB_ReadCoil(2);
    input_regs[HPSB_INPUT_REG_RELAY4_STATE] = (uint16_t)IO_HPSB_ReadCoil(3);

    /* ADC AVG/PKPK */
    input_regs[HPSB_INPUT_REG_ADC1_AVG] = avg[0];
    input_regs[HPSB_INPUT_REG_ADC2_AVG] = avg[1];
    input_regs[HPSB_INPUT_REG_ADC3_AVG] = avg[2];
    input_regs[HPSB_INPUT_REG_ADC1_PKPK] = pkpk[0];
    input_regs[HPSB_INPUT_REG_ADC2_PKPK] = pkpk[1];
    input_regs[HPSB_INPUT_REG_ADC3_PKPK] = pkpk[2];

    /* Current states */
    input_regs[HPSB_INPUT_REG_CUR1_STATE] = cur_on[0];
    input_regs[HPSB_INPUT_REG_CUR2_STATE] = cur_on[1];
    input_regs[HPSB_INPUT_REG_CUR3_STATE] = cur_on[2];

    /* reserve */
    input_regs[HPSB_INPUT_REG_RESERVED_15] = 0u;
}
