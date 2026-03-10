/**
 * @file upstream_slave_h2tech.c
 * @brief Upstream Modbus Slave (PC link): H2TECH table-driven read/write.
 *        FC02: h2_dec = start_addr + 1, H2Map_FindByDec + H2Map_ReadAggBit, LSB-first.
 *        FC05/15: H2Map_ApplyWrite(entry, value, 300ms). Illegal address -> 0x02.
 */
#include "upstream_slave_h2tech.h"
#include "h2tech_address_map.h"
#include "gateway_actions.h"
#include "aggregated_status.h"
#include "io_map.h"
#include "reset_reason.h"
#include "bsp_gpio.h"
#include "system_config.h"
#include "app_config.h"

#define EX_ILLEGAL_FUNCTION  0x01
#define EX_ILLEGAL_DATA_ADDR 0x02
#define EX_ILLEGAL_DATA_VAL  0x03  /* Write to read-only */
#define PULSE_MS_DEFAULT     300u

/* Upstream current block: 4x2000..4x200D (read-only). Modbus start_addr 2000, 14 registers. */
#define UPSTREAM_CURRENT_START  2000u
#define UPSTREAM_CURRENT_COUNT  14u

/* MAIN I/O: 4x2100 = DI bitmap (8 bits), 4x2101 = DO bitmap (bits 0..3). */
#define UPSTREAM_MAIN_IO_DI_REG  2100u
#define UPSTREAM_MAIN_IO_DO_REG  2101u
#define UPSTREAM_MAIN_IO_REG_COUNT 2u

/* MAIN ENV (SHTC3):
 * 4x2110=temp_c_x10 (signed), 4x2111=rh_x10 (unsigned), 4x2112=error_flags (AGG_ERR_SHTC3 bit 포함). */
#define UPSTREAM_MAIN_ENV_START   2110u
#define UPSTREAM_MAIN_ENV_COUNT   3u

/* Reset reason (RCC->CSR snapshot): 4x2113 = low16, 4x2114 = high16 */
#define UPSTREAM_RESET_CSR_START  2113u
#define UPSTREAM_RESET_CSR_COUNT  2u

/* PC control GPIO: 4x2120=PC_ON_EN (W), 4x2121=PC_RESET_EN (W), 4x2122=PC_LED_IN (R) */
#define UPSTREAM_PC_ON_EN_REG    2120u
#define UPSTREAM_PC_RESET_EN_REG 2121u
#define UPSTREAM_PC_LED_IN_REG   2122u

/* System config (EEPROM): 4x3000=slave_id, 4x3001=baudrate code, 4x3002=factory reset command */
#define UPSTREAM_SYSCFG_REG_COUNT  3u

static uint32_t baudrate_from_code(uint16_t code)
{
	switch (code) {
	case 0: return SYSTEM_CONFIG_BAUDRATE_9600;
	case 1: return SYSTEM_CONFIG_BAUDRATE_19200;
	case 2: return SYSTEM_CONFIG_BAUDRATE_38400;
	case 3: return SYSTEM_CONFIG_BAUDRATE_57600;
	case 4: return SYSTEM_CONFIG_BAUDRATE_115200;
	default: return 0;
	}
}

static uint16_t baudrate_to_code(uint32_t baud)
{
	switch (baud) {
	case SYSTEM_CONFIG_BAUDRATE_9600:   return 0;
	case SYSTEM_CONFIG_BAUDRATE_19200: return 1;
	case SYSTEM_CONFIG_BAUDRATE_38400: return 2;
	case SYSTEM_CONFIG_BAUDRATE_57600: return 3;
	case SYSTEM_CONFIG_BAUDRATE_115200: return 4;
	default: return 0xFFu;
	}
}

/* FC02 Read Discrete Inputs: H2TECH 1x, h2_dec = start_addr + 1 + i */
static int handle_fc02(uint16_t start_addr, uint16_t count, uint8_t *response, uint16_t resp_max)
{
    const uint16_t byte_count = (uint16_t)((count + 7u) / 8u);
    if (resp_max < 2u + byte_count) return -1;

    response[0] = 0x02;
    response[1] = (uint8_t)byte_count;

    for (uint16_t i = 0; i < byte_count; i++)
        response[2u + i] = 0;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t h2_dec = H2Map_ModbusAddrToH2Dec(start_addr) + i;
        const H2_MapEntry_t *e = H2Map_FindByDec(H2_AREA_1X, h2_dec);
        if (!e) {
            response[0] = 0x82;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }
        bool bit = false;
        if (e->src == H2_SRC_AGG_BIT)
            bit = H2Map_ReadAggBit(e->agg_bit_index);
        else if (e->src == H2_SRC_CONST0)
            bit = false;
        if (bit)
            response[2u + (i / 8u)] |= (uint8_t)(1u << (i % 8u));
    }
    /* Clear downstream write-fail alarm when PC reads 1x0880 (ALM12). */
    if (start_addr <= 880u && (start_addr + count) > 880u)
        Gateway_Action_ClearDownstreamWriteFailAlarm();
    return (int)(2 + byte_count);
}

/* FC03 Read Holding Registers: 4x2000..4x200D = per-port current raw (read-only).
 * Also 4x2100 count=2: MAIN DI bitmap (reg 2100), DO bitmap (reg 2101).
 * Policy: only start=2000 count=14 or start=2100 count=2 accepted; else 0x02 (bad address) or 0x03 (bad value). */
static int handle_fc03(uint16_t start_addr, uint16_t count, const void *p_agg,
                       uint8_t *response, uint16_t resp_max)
{
    /* Reset flags: RCC->CSR snapshot (2 regs) */
    if (start_addr == UPSTREAM_RESET_CSR_START) {
        if (count != UPSTREAM_RESET_CSR_COUNT) {
            response[0] = 0x83;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (resp_max < 2u + 4u) return -1;
        uint32_t csr = ResetReason_GetRccCsr();
        response[0] = 0x03;
        response[1] = 4u;
        response[2] = (uint8_t)((csr >> 24) & 0xFF);
        response[3] = (uint8_t)((csr >> 16) & 0xFF);
        response[4] = (uint8_t)((csr >> 8) & 0xFF);
        response[5] = (uint8_t)(csr & 0xFF);
        return 6;
    }

    /* MAIN ENV block: 4x2110 count=2 or 3 */
    if (start_addr == UPSTREAM_MAIN_ENV_START) {
        if (count != 2u && count != UPSTREAM_MAIN_ENV_COUNT) {
            response[0] = 0x83;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (resp_max < (uint16_t)(2u + count * 2u)) return -1;
        const aggregated_status_t *agg = (const aggregated_status_t *)p_agg;
        int16_t t_x10 = agg ? agg->env_temp_cx10 : (int16_t)-32768;
        uint16_t rh_x10 = agg ? agg->env_rh_x10 : (uint16_t)0xFFFF;
        uint16_t flags = agg ? agg->error_flags : 0xFFFF;
        response[0] = 0x03;
        response[1] = (uint8_t)(count * 2u);
        response[2] = (uint8_t)(((uint16_t)t_x10) >> 8);
        response[3] = (uint8_t)(((uint16_t)t_x10) & 0xFF);
        response[4] = (uint8_t)(rh_x10 >> 8);
        response[5] = (uint8_t)(rh_x10 & 0xFF);
        if (count == 3u) {
            response[6] = (uint8_t)(flags >> 8);
            response[7] = (uint8_t)(flags & 0xFF);
            return 8;
        }
        return 6; /* count==2 */
    }

    /* MAIN I/O block: 4x2100 (and optionally 4x2101). Accept count=1 (DI only) or count=2 (DI+DO). */
    if (start_addr == UPSTREAM_MAIN_IO_DI_REG) {
        if (count != 1u && count != UPSTREAM_MAIN_IO_REG_COUNT) {
            response[0] = 0x83;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        uint16_t di = IO_Main_ReadDI_Bitmap();
        if (count == 1u) {
            /* FC03 addr=2100 cnt=1: SlaveID + FC + ByteCount(2) + Data(2B) + CRC */
            if (resp_max < 2u + 2u) return -1;
            response[0] = 0x03;
            response[1] = 2u;
            response[2] = (uint8_t)(di >> 8);
            response[3] = (uint8_t)(di & 0xFF);
            return 4;
        }
        /* count == 2 */
        if (resp_max < 2u + 4u) return -1;
        uint16_t do_val = IO_Main_ReadDO_Bitmap();
        response[0] = 0x03;
        response[1] = 4u;
        response[2] = (uint8_t)(di >> 8);
        response[3] = (uint8_t)(di & 0xFF);
        response[4] = (uint8_t)(do_val >> 8);
        response[5] = (uint8_t)(do_val & 0xFF);
        return 6;
    }
    if (start_addr == UPSTREAM_MAIN_IO_DO_REG) {
        response[0] = 0x83;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }

    /* PC_LED_IN (4x2122): read-only, single register. 0=OFF, 1=ON */
    if (start_addr == UPSTREAM_PC_LED_IN_REG) {
        if (count != 1u) {
            response[0] = 0x83;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (resp_max < 2u + 2u) return -1;
        uint16_t val = (uint16_t)BSP_ReadPC_LED_IN();
        response[0] = 0x03;
        response[1] = 2u;
        response[2] = (uint8_t)(val >> 8);
        response[3] = (uint8_t)(val & 0xFF);
        return 4;
    }

    /* System config (4x3000~3002): FC03 read — slave_id, baudrate code, factory_reset(read 0) */
    if (start_addr == SYSCFG_MODBUS_SLAVE_ID_REG) {
        if (count == 0u || count > UPSTREAM_SYSCFG_REG_COUNT) {
            response[0] = 0x83;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (resp_max < (uint16_t)(2u + count * 2u)) return -1;
        const system_config_t *cfg = SystemConfig_Get();
        response[0] = 0x03;
        response[1] = (uint8_t)(count * 2u);
        uint16_t r0 = cfg ? (uint16_t)cfg->slave_id : SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
        uint16_t r1 = cfg ? baudrate_to_code(cfg->baudrate) : 0u;
        if (r1 > 4u) r1 = 0u;
        response[2] = (uint8_t)(r0 >> 8);
        response[3] = (uint8_t)(r0 & 0xFF);
        if (count >= 2u) {
            response[4] = (uint8_t)(r1 >> 8);
            response[5] = (uint8_t)(r1 & 0xFF);
        }
        if (count >= 3u) {
            response[6] = 0;
            response[7] = 0;  /* 4x3002 read: always 0 */
        }
#if SYSCFG_MODBUS_DEBUG_LOG
        UpstreamSlave_LogSyscfgRead(r0, r1);
#endif
        return (int)(2 + count * 2u);
    }

    if (start_addr != UPSTREAM_CURRENT_START) {
        response[0] = 0x83;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }
    if (count != UPSTREAM_CURRENT_COUNT) {
        response[0] = 0x83;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }
    const uint16_t byte_count = count * 2u;
    if (resp_max < 2u + byte_count) return -1;

    const aggregated_status_t *agg = (const aggregated_status_t *)p_agg;
    response[0] = 0x03;
    response[1] = (uint8_t)byte_count;

    uint16_t regs[UPSTREAM_CURRENT_COUNT];
    regs[0] = agg->hpsb_sense_raw[0];
    regs[1] = agg->hpsb_sense_raw[1];
    regs[2] = agg->hpsb_sense_raw[2];
    regs[3] = agg->lpsb1_sense_raw[0];
    regs[4] = agg->lpsb1_sense_raw[1];
    regs[5] = agg->lpsb1_sense_raw[2];
    regs[6] = agg->lpsb2_sense_raw[0];
    regs[7] = agg->lpsb2_sense_raw[1];
    regs[8] = agg->lpsb2_sense_raw[2];
    regs[9] = agg->lpsb3_sense_raw[0];
    regs[10] = agg->lpsb3_sense_raw[1];
    regs[11] = agg->lpsb3_sense_raw[2];
    regs[12] = 0;  /* MAIN DOOR1 current (none) */
    regs[13] = 0;  /* MAIN DOOR2 current (none) */
    for (uint16_t i = 0; i < count; i++) {
        response[2 + i * 2]     = (uint8_t)(regs[i] >> 8);
        response[2 + i * 2 + 1] = (uint8_t)(regs[i] & 0xFF);
    }
    return (int)(2 + byte_count);
}

/* FC06 Write Single Register: 2101 (DO bitmap); 2120 (PC_ON_EN); 2121 (PC_RESET_EN). 2122 read-only. */
static int handle_fc06(uint16_t start_addr, const uint8_t *write_data,
                       uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 6u || !write_data) return -1;
    uint16_t value = (uint16_t)((write_data[0] << 8) | write_data[1]);

    if (start_addr == UPSTREAM_MAIN_IO_DI_REG) {
        response[0] = 0x86;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }
    if (start_addr == UPSTREAM_PC_LED_IN_REG) {
        response[0] = 0x86;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }

    if (start_addr == UPSTREAM_PC_ON_EN_REG) {
        if (value != 0u)
            Gateway_Action_StartPulsePC_ON_EN();
        else
            BSP_WritePC_ON_EN(0);
        response[0] = 0x06;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(value >> 8);
        response[4] = (uint8_t)(value & 0xFF);
        return 6;
    }
    if (start_addr == UPSTREAM_PC_RESET_EN_REG) {
        if (value != 0u)
            Gateway_Action_StartPulsePC_RESET_EN();
        else
            BSP_WritePC_RESET_EN(0);
        response[0] = 0x06;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(value >> 8);
        response[4] = (uint8_t)(value & 0xFF);
        return 6;
    }

    /* 4x3000: slave_id (1~247) -> EEPROM save */
    if (start_addr == SYSCFG_MODBUS_SLAVE_ID_REG) {
        const system_config_t *cur = SystemConfig_Get();
        if (!cur || value < SYSTEM_CONFIG_SLAVE_ID_MIN || value > SYSTEM_CONFIG_SLAVE_ID_MAX) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_SLAVE_ID_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        system_config_t cfg = *cur;
        cfg.slave_id = (uint8_t)(value & 0xFF);
        if (SystemConfig_Save(&cfg) != 0) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_SLAVE_ID_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
#if SYSCFG_MODBUS_DEBUG_LOG
        UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_SLAVE_ID_REG, value, 1);
#endif
        response[0] = 0x06;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(value >> 8);
        response[4] = (uint8_t)(value & 0xFF);
        return 6;
    }

    /* 4x3001: baudrate code (0~4) -> EEPROM save only.
     * 보드레이트는 즉시 적용되지 않음. 다음 재부팅 시 main.c에서 Load 후 huart1.Init.BaudRate 적용. */
    if (start_addr == SYSCFG_MODBUS_BAUDRATE_CODE_REG) {
        const system_config_t *cur = SystemConfig_Get();
        if (!cur || value > 4u) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_BAUDRATE_CODE_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        uint32_t baud = baudrate_from_code((uint16_t)value);
        system_config_t cfg = *cur;
        cfg.baudrate = baud;
        if (SystemConfig_Save(&cfg) != 0) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_BAUDRATE_CODE_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
#if SYSCFG_MODBUS_DEBUG_LOG
        UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_BAUDRATE_CODE_REG, value, 1);
#endif
        response[0] = 0x06;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(value >> 8);
        response[4] = (uint8_t)(value & 0xFF);
        return 6;
    }

    /* 4x3002: factory reset command. value=1 -> SystemConfig_FactoryReset() */
    if (start_addr == SYSCFG_MODBUS_FACTORY_RESET_REG) {
        if (value != 1u) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_FACTORY_RESET_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (SystemConfig_FactoryReset() != 0) {
#if SYSCFG_MODBUS_DEBUG_LOG
            UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_FACTORY_RESET_REG, value, 0);
#endif
            response[0] = 0x86;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
#if SYSCFG_MODBUS_DEBUG_LOG
        UpstreamSlave_LogSyscfgWrite(SYSCFG_MODBUS_FACTORY_RESET_REG, value, 1);
#endif
        response[0] = 0x06;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(value >> 8);
        response[4] = (uint8_t)(value & 0xFF);
        return 6;
    }

    if (start_addr != UPSTREAM_MAIN_IO_DO_REG) {
        response[0] = 0x86;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }
    value &= 0x0Fu;
    IO_Main_WriteDO_Bitmap(value);
    response[0] = 0x06;
    response[1] = (uint8_t)(start_addr >> 8);
    response[2] = (uint8_t)(start_addr & 0xFF);
    response[3] = (uint8_t)(value >> 8);
    response[4] = (uint8_t)(value & 0xFF);
    return 6;
}

/* FC05 Write Single Coil */
static int handle_fc05(uint16_t start_addr, const uint8_t *write_data,
                      uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 5u || !write_data) return -1;

    uint16_t h2_dec = H2Map_ModbusAddrToH2Dec(start_addr);
    const H2_MapEntry_t *e = H2Map_FindByDec(H2_AREA_1X, h2_dec);
    if (!e) {
        response[0] = 0x85;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }
    /* Defensive: write to read-only range (0821~0836, 0853~0860, 0869~0880) -> 0x03 */
    if (e->rw != H2_RW_WRITE) {
        response[0] = 0x85;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }
    bool value = (write_data[0] != 0);
    if (!H2Map_ApplyWrite(e, value, PULSE_MS_DEFAULT)) {
        response[0] = 0x85;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }
    response[0] = 0x05;
    response[1] = (uint8_t)(start_addr >> 8);
    response[2] = (uint8_t)(start_addr & 0xFF);
    response[3] = value ? 0xFFu : 0u;
    response[4] = 0u;
    return 5;
}

/* FC15 Write Multiple Coils */
static int handle_fc15(uint16_t start_addr, uint16_t count, const uint8_t *write_data,
                       uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 5u || !write_data) return -1;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t h2_dec = H2Map_ModbusAddrToH2Dec(start_addr) + i;
        const H2_MapEntry_t *e = H2Map_FindByDec(H2_AREA_1X, h2_dec);
        if (!e) {
            response[0] = 0x8F;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }
        /* Defensive: write to read-only range -> 0x03; 0899/0900 not in table -> 0x02 above */
        if (e->rw != H2_RW_WRITE) {
            response[0] = 0x8F;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        bool value = (write_data[i / 8u] >> (i % 8u)) & 1u;
        if (!H2Map_ApplyWrite(e, value, PULSE_MS_DEFAULT)) {
            response[0] = 0x8F;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }
    }
    response[0] = 0x0F;
    response[1] = (uint8_t)(start_addr >> 8);
    response[2] = (uint8_t)(start_addr & 0xFF);
    response[3] = (uint8_t)(count >> 8);
    response[4] = (uint8_t)(count & 0xFF);
    return 5;
}

int UpstreamSlave_HandleRequest(uint8_t fc, uint16_t start_addr, uint16_t count,
                                const uint8_t *write_data,
                                const void *p_agg,
                                uint8_t *response, uint16_t resp_max)
{
    (void)p_agg;
    if (!response || resp_max < 2u) return -1;

    switch (fc) {
    case 0x02:
        return handle_fc02(start_addr, count, response, resp_max);
    case 0x03:
        return handle_fc03(start_addr, count, p_agg, response, resp_max);
    case 0x05:
        return handle_fc05(start_addr, write_data, response, resp_max);
    case 0x06:
        return handle_fc06(start_addr, write_data, response, resp_max);
    case 0x0F:
        return handle_fc15(start_addr, count, write_data, response, resp_max);
    case 0x01:
    case 0x04:
    case 0x10:
    default:
        response[0] = (uint8_t)(fc | 0x80);
        response[1] = EX_ILLEGAL_FUNCTION;
        return 2;
    }
}
