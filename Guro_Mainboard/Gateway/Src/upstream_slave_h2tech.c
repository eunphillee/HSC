/**
 * @file upstream_slave_h2tech.c
 * @brief Upstream Modbus Slave (PC link): Unified Rule v1.3.
 *        Read = FC04 only. Write = FC05 / FC16 only.
 *        FC01/FC02/FC03/FC06/FC15 → EX_ILLEGAL_FUNCTION.
 *        Illegal address -> EX_ILLEGAL_DATA_ADDR(0x02).
 *
 *        FC04 responses are served from the pre-populated IR map
 *        (modbus_ir_map.c, refreshed every 100ms via SystemSync_Update).
 *        To keep upstream reads deterministic, handle_fc04() is side-effect free
 *        and only delegates PDU building to ModbusIrMap_Fc04Response().
 */
#include "upstream_slave_h2tech.h"
#include "modbus_ir_map.h"
#include "h2tech_address_map.h"
#include "gateway_actions.h"
#include "gateway_write_log.h"
#include "aggregated_status.h"
#include "io_map.h"
#include "reset_reason.h"
#include "bsp_gpio.h"
#include "system_config.h"
#include "app_config.h"
#include "modbus_table.h"
#include "modbus_master.h"
#include "board_rtc.h"
#include "main_auto_link.h"
#include "output_state_nvm.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define EX_ILLEGAL_FUNCTION  0x01
#define EX_ILLEGAL_DATA_ADDR 0x02
#define EX_ILLEGAL_DATA_VAL  0x03  /* Write to read-only */
#define EX_SLAVE_DEVICE_FAIL 0x04  /* Address valid but gateway/downstream write failed */
#define PULSE_MS_DEFAULT     300u

/* Upstream current block: 4x2000..4x2027 (read-only). start=2000, 40 registers.
 * Per board (HPSB, LPSB1..3): 9 words = AVG[3], PKPK[3], CURRENT[3]; then 4 reserved = 40 total. */
#define UPSTREAM_CURRENT_START  2000u
#define UPSTREAM_CURRENT_COUNT  40u

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
/* FC04 input register diagnostics (service extension) */
#define UPSTREAM_DIAG_IR_START      4000u
#define UPSTREAM_DIAG_IR_COUNT      40u
/* RTC block: 4x0891..0897 (PDU 890..896) */
#define UPSTREAM_RTC_REG_START      890u
#define UPSTREAM_RTC_REG_COUNT      7u

/* Unified Rule v1.3 FC04 address zones:
 *   0..81   : MAIN (0..23) + PACKED subboard (24..81)
 *   890..896: RTC
 *   2100..2114: ENV/IO/reset-CSR (FC03 blocked, exposed here)
 *   4000..4039: DIAG
 *
 * PACKED 24..81 layout (58 regs):
 *   Alive/status 24..33:
 *     24=HPSB alive, 25-27=rsvd,
 *     28=LPSB2 alive, 29=LPSB4 alive, 30=LPSB8 alive, 31-33=rsvd
 *   Coils 34..45:
 *     34=HPSB_R1, 35=HPSB_R2, 36=HPSB_R3,
 *     37=LPSB2_S1, 38=LPSB2_S2, 39=LPSB2_S3,
 *     40=LPSB4_S1, 41=LPSB4_S2, 42=LPSB4_S3,
 *     43=LPSB8_S1, 44=LPSB8_S2, 45=LPSB8_S3
 *   AVG 46..57:
 *     46=HPSB_A1,  47=HPSB_A2,  48=HPSB_A3,
 *     49=LPSB2_A1, 50=LPSB2_A2, 51=LPSB2_A3,
 *     52=LPSB4_A1, 53=LPSB4_A2, 54=LPSB4_A3,
 *     55=LPSB8_A1, 56=LPSB8_A2, 57=LPSB8_A3
 *   PKPK 58..69: same board/channel order
 *   CUR  70..81: same board/channel order
 */
#define FC04_MAIN_PACKED_END   81u    /* 0..81 = 82 regs total */
#define FC04_ENV_IO_START      2100u
#define FC04_ENV_IO_END        2114u  /* 2100..2114 = 15 regs */
#define FC04_ENV_IO_COUNT      15u
/*
 * FC04 packed reads (24..81) used to request downstream on-demand polls here.
 * In practice this made a simple upstream read mutate system state:
 *   - count=25+ immediately armed UART2 polling,
 *   - Aggregator_Update() could observe ModbusMaster_IsBusy() and publish
 *     env sentinel values (-32768 / 0xFFFF),
 *   - repeated PC polling around the 24/25 boundary produced checksum/value jitter.
 *
 * The FC04 map is already pre-populated every 100ms, so reads should stay
 * side-effect free. Downstream polls still happen from boot / write / restore
 * paths that already own that responsibility.
 */
#define FC04_TRIGGER_ONDEMAND_POLL_ON_PACKED_READ  0

#if ENABLE_MB_FC04_MAIN_DEBUG
static void log_fc04_main_snapshot(uint16_t di_now, uint16_t do_now, const uint16_t *regs)
{
    extern UART_HandleTypeDef huart1;
    if (!regs) return;
    /* Unified debug values for DI_03 <-> REG4 <-> RELAY3 mapping verification */
    int di3 = (di_now & (1u << 2)) ? 1 : 0;          /* DI_03 == bitmap bit2 */
    int reg4 = regs[4] ? 1 : 0;                      /* FC04 MAP_MAIN regs[4] == DI_03 */
    int relay3 = regs[13] ? 1 : 0;                    /* reg13 == MAIN_RELAY_03 */
    int relay3_gpio = (HAL_GPIO_ReadPin(RELAY3_EN_GPIO_Port, RELAY3_EN_Pin) == GPIO_PIN_SET) ? 1 : 0;

    /* User-requested direct printf diagnostics (enable only when needed). */
    (void)printf("RELAY3 GPIO=%d\r\n", relay3_gpio);
    (void)printf("DI3=%d REG4=%d RELAY3=%d\r\n", di3, reg4, relay3);

    char buf[220];
    int n = snprintf(
        buf, sizeof(buf),
        "[MB][FC04][MAIN] di_now=0x%02X do_now=0x%02X regs2_9=[%u,%u,%u,%u,%u,%u,%u,%u] rly=[%u,%u,%u,%u]\r\n",
        (unsigned)(di_now & 0xFFu),
        (unsigned)(do_now & 0xFFu),
        (unsigned)regs[2], (unsigned)regs[3], (unsigned)regs[4], (unsigned)regs[5],
        (unsigned)regs[6], (unsigned)regs[7], (unsigned)regs[8], (unsigned)regs[9],
        (unsigned)regs[11], (unsigned)regs[12], (unsigned)regs[13], (unsigned)regs[14]
    );
    if (n > 0) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
    }
}
#endif

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

static int h2_dec_to_sub_coil(uint16_t h2_dec, uint8_t *out_slave_id, uint16_t *out_coil_index)
{
    if (!out_slave_id || !out_coil_index) return -1;
    if (h2_dec < 899u || h2_dec > 910u) return -1;
    {
        uint16_t offset = (uint16_t)(h2_dec - 899u);
        uint8_t board = (uint8_t)(offset / 3u);
        uint8_t coil = (uint8_t)(offset % 3u);
        static const uint8_t sid[] = {1u, 2u, 4u, 8u};
        *out_slave_id = sid[board];
        *out_coil_index = coil;
    }
    return 0;
}

/* FC01 Read Coils: writable 1x 영역(0899~0910, HPSB/LPSB 미러) — 현재 upstream 에서 FC01 비활성 */
__attribute__((unused)) static int handle_fc01(uint16_t start_addr, uint16_t count, uint8_t *response, uint16_t resp_max)
{
    uint16_t byte_count = (uint16_t)((count + 7u) / 8u);
    if (resp_max < (uint16_t)(2u + byte_count)) return -1;
    response[0] = 0x01;
    response[1] = (uint8_t)byte_count;
    for (uint16_t i = 0; i < byte_count; i++) response[2u + i] = 0u;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t h2_dec = (uint16_t)(H2Map_ModbusAddrToH2Dec(start_addr) + i);
        const H2_MapEntry_t *e = H2Map_FindByDec(H2_AREA_1X, h2_dec);
        if (!e || e->rw != H2_RW_WRITE) {
            response[0] = 0x81;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }

        bool bit = false;
        if (h2_dec >= 899u && h2_dec <= 910u) {
            uint8_t sid = 0u;
            uint16_t coil = 0u;
            if (h2_dec_to_sub_coil(h2_dec, &sid, &coil) == 0)
                bit = ModbusTable_GetCoil((SlaveId_t)sid, coil) ? true : false;
        } else if (e->src == H2_SRC_AGG_BIT) {
            bit = H2Map_ReadAggBit(e->agg_bit_index);
        } else {
            /* H2_SRC_ACTION_PULSE 등: 래치 없음 → 0 */
            bit = false;
        }
        if (bit) response[2u + (i / 8u)] |= (uint8_t)(1u << (i % 8u));
    }
    return (int)(2u + byte_count);
}

/* FC02 Read Discrete Inputs: H2TECH 1x, h2_dec = start_addr + 1 + i */
__attribute__((unused)) static int handle_fc02(uint16_t start_addr, uint16_t count, uint8_t *response, uint16_t resp_max)
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

/* FC03 Read Holding Registers: 4x2000..4x2027 = HPSB/LPSB AVG+PKPK+CURRENT (read-only).
 * Also 4x2100 count=2: MAIN DI bitmap (reg 2100), DO bitmap (reg 2101).
 * Policy: start=2000 count=40 or start=2100 count=2 ... */
__attribute__((unused)) static int handle_fc03(uint16_t start_addr, uint16_t count, const void *p_agg,
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
    /* HPSB */
    regs[0] = agg->hpsb_sense_raw[0];
    regs[1] = agg->hpsb_sense_raw[1];
    regs[2] = agg->hpsb_sense_raw[2];
    regs[3] = agg->hpsb_pkpk[0];
    regs[4] = agg->hpsb_pkpk[1];
    regs[5] = agg->hpsb_pkpk[2];
    regs[6] = agg->hpsb_current_st[0];
    regs[7] = agg->hpsb_current_st[1];
    regs[8] = agg->hpsb_current_st[2];
    /* LPSB1 */
    regs[9] = agg->lpsb1_sense_raw[0];
    regs[10] = agg->lpsb1_sense_raw[1];
    regs[11] = agg->lpsb1_sense_raw[2];
    regs[12] = agg->lpsb1_pkpk[0];
    regs[13] = agg->lpsb1_pkpk[1];
    regs[14] = agg->lpsb1_pkpk[2];
    regs[15] = agg->lpsb1_current_st[0];
    regs[16] = agg->lpsb1_current_st[1];
    regs[17] = agg->lpsb1_current_st[2];
    /* LPSB2 */
    regs[18] = agg->lpsb2_sense_raw[0];
    regs[19] = agg->lpsb2_sense_raw[1];
    regs[20] = agg->lpsb2_sense_raw[2];
    regs[21] = agg->lpsb2_pkpk[0];
    regs[22] = agg->lpsb2_pkpk[1];
    regs[23] = agg->lpsb2_pkpk[2];
    regs[24] = agg->lpsb2_current_st[0];
    regs[25] = agg->lpsb2_current_st[1];
    regs[26] = agg->lpsb2_current_st[2];
    /* LPSB3 */
    regs[27] = agg->lpsb3_sense_raw[0];
    regs[28] = agg->lpsb3_sense_raw[1];
    regs[29] = agg->lpsb3_sense_raw[2];
    regs[30] = agg->lpsb3_pkpk[0];
    regs[31] = agg->lpsb3_pkpk[1];
    regs[32] = agg->lpsb3_pkpk[2];
    regs[33] = agg->lpsb3_current_st[0];
    regs[34] = agg->lpsb3_current_st[1];
    regs[35] = agg->lpsb3_current_st[2];
    regs[36] = 0;
    regs[37] = 0;
    regs[38] = 0;
    regs[39] = 0;
    for (uint16_t i = 0; i < count; i++) {
        response[2 + i * 2]     = (uint8_t)(regs[i] >> 8);
        response[2 + i * 2 + 1] = (uint8_t)(regs[i] & 0xFF);
    }
    return (int)(2 + byte_count);
}

/* FC04 Read Input Registers — Unified Rule v1.3
 *
 * Allowed zones (see modbus_ir_map.h):
 *   0..81     MAIN (0..23) + PACKED subboard (24..81)
 *   890..896  RTC
 *   2100..2122 ENV / IO / reset-CSR / PC_LED_IN
 *   4000..4039 DIAG
 *
 * All zone data is pre-populated by ModbusIrMap_RefreshAll() (100ms periodic).
 * This handler keeps FC04 side-effect free and delegates response building to
 * ModbusIrMap_Fc04Response().
 */
static int handle_fc04(uint16_t start_addr, uint16_t count, const void *p_agg,
                       uint8_t *response, uint16_t resp_max)
{
    (void)p_agg;  /* Map is refreshed periodically; agg used only for poll trigger */

    if (count == 0u) {
        response[0] = 0x84u;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }

    /* Keep FC04 read path side-effect free: return the latest refreshed snapshot.
     * Optional packed-read-triggered polling can be re-enabled for experiments,
     * but is disabled by default because it destabilized 25+ register reads. */
    const uint32_t end = (uint32_t)start_addr + (uint32_t)count - 1u;
#if FC04_TRIGGER_ONDEMAND_POLL_ON_PACKED_READ
    if (start_addr <= FC04_MAIN_PACKED_END && end <= (uint32_t)FC04_MAIN_PACKED_END
        && end >= 24u) {
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_HPSB);
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB1);
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB2);
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB3);
    }
#else
    (void)end;
#endif

    return ModbusIrMap_Fc04Response(start_addr, count, response, resp_max);
}

/* ---- legacy stub kept for reference; no longer called from handle_fc04 ---- */
#if 0  /* MAIN+PACKED on-demand assembly (replaced by modbus_ir_map) */
static void _legacy_fc04_main_packed_unused(uint16_t start_addr, uint16_t count,
                                             const void *p_agg,
                                             uint8_t *response, uint16_t resp_max)
{
    const aggregated_status_t *agg = (const aggregated_status_t *)p_agg;
    (void)resp_max;
    const uint32_t end = (uint32_t)start_addr + (uint32_t)count - 1u;
    (void)end;

        /* Build unified 0..81 buffer (stack: 82 × 2 = 164 bytes) */
        uint16_t unified[82u] = {0};
        uint16_t di_now = IO_Main_ReadDI_Bitmap();
        uint16_t do_now = IO_Main_ReadDO_Bitmap();

        /* ---- MAIN 0..23 ---- */
        unified[0]  = (agg && agg->error_flags == 0u) ? 1u : 0u;
        unified[1]  = agg ? agg->error_flags : 0u;
        for (uint16_t i = 0u; i < 8u; i++) {
            unified[2u + i] = (di_now & (1u << i)) ? 1u : 0u;
        }
        unified[10] = (uint16_t)BSP_ReadPC_LED_IN();
        unified[11] = (do_now & (1u << 0)) ? 1u : 0u; /* MAIN_RELAY_01 */
        unified[12] = (do_now & (1u << 1)) ? 1u : 0u; /* MAIN_RELAY_02 */
        unified[13] = (do_now & (1u << 2)) ? 1u : 0u; /* MAIN_RELAY_03 */
        unified[14] = (do_now & (1u << 3)) ? 1u : 0u; /* MAIN_RELAY_04 */
        unified[15] = agg ? (uint16_t)agg->env_temp_cx10 : (uint16_t)0x8000u;
        unified[16] = agg ? agg->env_rh_x10 : 0xFFFFu;
        /* 17..19: reserved (0) */
        unified[20] = MainAutoLink_GetVirtEnableWord(0u);
        unified[21] = MainAutoLink_GetVirtEnableWord(1u);
        unified[22] = MainAutoLink_GetVirtEnableWord(2u);
        unified[23] = MainAutoLink_GetVirtEnableWord(3u);

        /* ---- PACKED 24..81 ---- */

        /* Alive/status 24..33 */
        unified[24] = agg ? (agg->hpsb_status_reg  ? 1u : 0u) : 0u; /* HPSB alive  */
        /* 25..27: reserved */
        unified[28] = agg ? (agg->lpsb1_alarm_reg  ? 1u : 0u) : 0u; /* LPSB2 alive */
        unified[29] = agg ? (agg->lpsb2_alarm_reg  ? 1u : 0u) : 0u; /* LPSB4 alive */
        unified[30] = agg ? (agg->lpsb3_alarm_reg  ? 1u : 0u) : 0u; /* LPSB8 alive */
        /* 31..33: reserved */

        /* Coils 34..45 */
        if (agg) {
            unified[34] = (agg->hpsb_coils & (1u << 0)) ? 1u : 0u; /* HPSB relay1 */
            unified[35] = (agg->hpsb_coils & (1u << 1)) ? 1u : 0u; /* HPSB relay2 */
            unified[36] = (agg->hpsb_coils & (1u << 2)) ? 1u : 0u; /* HPSB relay3 */
            unified[37] = agg->lpsb1_coils[0] ? 1u : 0u; /* LPSB2 SSR1 */
            unified[38] = agg->lpsb1_coils[1] ? 1u : 0u; /* LPSB2 SSR2 */
            unified[39] = agg->lpsb1_coils[2] ? 1u : 0u; /* LPSB2 SSR3 */
            unified[40] = agg->lpsb2_coils[0] ? 1u : 0u; /* LPSB4 SSR1 */
            unified[41] = agg->lpsb2_coils[1] ? 1u : 0u; /* LPSB4 SSR2 */
            unified[42] = agg->lpsb2_coils[2] ? 1u : 0u; /* LPSB4 SSR3 */
            unified[43] = agg->lpsb3_coils[0] ? 1u : 0u; /* LPSB8 SSR1 */
            unified[44] = agg->lpsb3_coils[1] ? 1u : 0u; /* LPSB8 SSR2 */
            unified[45] = agg->lpsb3_coils[2] ? 1u : 0u; /* LPSB8 SSR3 */
        }

        /* AVG 46..57 */
        if (agg) {
            unified[46] = agg->hpsb_sense_raw[0];  unified[47] = agg->hpsb_sense_raw[1];  unified[48] = agg->hpsb_sense_raw[2];
            unified[49] = agg->lpsb1_sense_raw[0]; unified[50] = agg->lpsb1_sense_raw[1]; unified[51] = agg->lpsb1_sense_raw[2];
            unified[52] = agg->lpsb2_sense_raw[0]; unified[53] = agg->lpsb2_sense_raw[1]; unified[54] = agg->lpsb2_sense_raw[2];
            unified[55] = agg->lpsb3_sense_raw[0]; unified[56] = agg->lpsb3_sense_raw[1]; unified[57] = agg->lpsb3_sense_raw[2];
        }

        /* PKPK 58..69 */
        if (agg) {
            unified[58] = agg->hpsb_pkpk[0];  unified[59] = agg->hpsb_pkpk[1];  unified[60] = agg->hpsb_pkpk[2];
            unified[61] = agg->lpsb1_pkpk[0]; unified[62] = agg->lpsb1_pkpk[1]; unified[63] = agg->lpsb1_pkpk[2];
            unified[64] = agg->lpsb2_pkpk[0]; unified[65] = agg->lpsb2_pkpk[1]; unified[66] = agg->lpsb2_pkpk[2];
            unified[67] = agg->lpsb3_pkpk[0]; unified[68] = agg->lpsb3_pkpk[1]; unified[69] = agg->lpsb3_pkpk[2];
        }

        /* CUR 70..81 */
        if (agg) {
            unified[70] = agg->hpsb_current_st[0];  unified[71] = agg->hpsb_current_st[1];  unified[72] = agg->hpsb_current_st[2];
            unified[73] = agg->lpsb1_current_st[0]; unified[74] = agg->lpsb1_current_st[1]; unified[75] = agg->lpsb1_current_st[2];
            unified[76] = agg->lpsb2_current_st[0]; unified[77] = agg->lpsb2_current_st[1]; unified[78] = agg->lpsb2_current_st[2];
            unified[79] = agg->lpsb3_current_st[0]; unified[80] = agg->lpsb3_current_st[1]; unified[81] = agg->lpsb3_current_st[2];
        }

#if ENABLE_MB_FC04_MAIN_DEBUG
        if (start_addr < 24u) {
            log_fc04_main_snapshot(di_now, do_now, unified);
        }
#endif

        response[0] = 0x04;
        response[1] = (uint8_t)(count * 2u);
        for (uint16_t i = 0u; i < count; i++) {
            uint16_t v = unified[start_addr + i];
            response[2u + i * 2u]     = (uint8_t)(v >> 8);
            response[2u + i * 2u + 1u] = (uint8_t)(v & 0xFFu);
        }
        return (int)(2u + count * 2u);
    }

    /* ---- Unknown address ---- */
    response[0] = 0x84;
    response[1] = EX_ILLEGAL_DATA_ADDR;
    return 2;
}  /* end _legacy_fc04_main_packed_unused */
#endif /* 0 — legacy FC04 on-demand assembly */

/* FC06 Write Single Register: 2101 (DO bitmap); 2120 (PC_ON_EN); 2121 (PC_RESET_EN). 2122 read-only.
 * NOTE: Unified Rule v1.2 금지 FC. UpstreamSlave_HandleRequest에서 EX_ILLEGAL_FUNCTION 반환으로 변경됨. */
__attribute__((unused)) static int handle_fc06(uint16_t start_addr, const uint8_t *write_data,
                       uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 6u || !write_data) return -1;
    uint16_t value = (uint16_t)((write_data[0] << 8) | write_data[1]);

#if FC06_DEBUG_LOG
    Gateway_LogFc06Received(start_addr, value);
#endif

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
#if FC06_DEBUG_LOG
    Gateway_LogFc06MappedLocal(start_addr, value);
#endif
    value &= 0x0Fu;
    IO_Main_WriteDO_Bitmap(value);
    response[0] = 0x06;
    response[1] = (uint8_t)(start_addr >> 8);
    response[2] = (uint8_t)(start_addr & 0xFF);
    response[3] = (uint8_t)(value >> 8);
    response[4] = (uint8_t)(value & 0xFF);
    return 6;
}

/* FC16 Write Multiple Holding Registers: 4x2101(1개), 4x3000~3002(1~3개).
 * NOTE: Unified Rule v1.2 금지 FC. UpstreamSlave_HandleRequest에서 EX_ILLEGAL_FUNCTION 반환으로 변경됨. */
__attribute__((unused)) static int handle_fc16(uint16_t start_addr, uint16_t count, const uint8_t *write_data,
                       uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 5u || !write_data || count == 0u) return -1;

    if (start_addr == UPSTREAM_MAIN_IO_DO_REG && count == 1u) {
        uint16_t value = (uint16_t)((write_data[0] << 8) | write_data[1]);
        value &= 0x0Fu;
        IO_Main_WriteDO_Bitmap(value);
        response[0] = 0x10;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = 0x00;
        response[4] = 0x01;
        return 5;
    }

    if (start_addr == SYSCFG_MODBUS_SLAVE_ID_REG && count <= 3u) {
        const system_config_t *cur = SystemConfig_Get();
        if (!cur) {
            response[0] = 0x90;
            response[1] = EX_SLAVE_DEVICE_FAIL;
            return 2;
        }
        system_config_t cfg = *cur;
        if (count >= 1u) {
            uint16_t v0 = (uint16_t)((write_data[0] << 8) | write_data[1]);
            if (v0 < SYSTEM_CONFIG_SLAVE_ID_MIN || v0 > SYSTEM_CONFIG_SLAVE_ID_MAX) {
                response[0] = 0x90;
                response[1] = EX_ILLEGAL_DATA_VAL;
                return 2;
            }
            cfg.slave_id = (uint8_t)(v0 & 0xFFu);
        }
        if (count >= 2u) {
            uint16_t v1 = (uint16_t)((write_data[2] << 8) | write_data[3]);
            uint32_t baud = baudrate_from_code(v1);
            if (baud == 0u) {
                response[0] = 0x90;
                response[1] = EX_ILLEGAL_DATA_VAL;
                return 2;
            }
            cfg.baudrate = baud;
        }
        if (count >= 3u) {
            uint16_t v2 = (uint16_t)((write_data[4] << 8) | write_data[5]);
            if (v2 == 1u) {
                if (SystemConfig_FactoryReset() != 0) {
                    response[0] = 0x90;
                    response[1] = EX_SLAVE_DEVICE_FAIL;
                    return 2;
                }
            } else {
                response[0] = 0x90;
                response[1] = EX_ILLEGAL_DATA_VAL;
                return 2;
            }
        } else {
            if (SystemConfig_Save(&cfg) != 0) {
                response[0] = 0x90;
                response[1] = EX_SLAVE_DEVICE_FAIL;
                return 2;
            }
        }

        response[0] = 0x10;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFF);
        response[3] = (uint8_t)(count >> 8);
        response[4] = (uint8_t)(count & 0xFF);
        return 5;
    }

    response[0] = 0x90;
    response[1] = EX_ILLEGAL_DATA_ADDR;
    return 2;
}

/* FC05 Write Single Coil */
static int handle_fc05(uint16_t start_addr, const uint8_t *write_data,
                      uint8_t *response, uint16_t resp_max)
{
    if (resp_max < 5u || !write_data) return -1;

    bool value = (write_data[0] != 0);
#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepRecvFromPc();
    Gateway_LogFc05StepRawCoilValue(start_addr, value ? 1u : 0u);
#endif
#if GATEWAY_WRITE_DEBUG_LOG
    Gateway_LogFc05RecvAddr(start_addr, value ? 1u : 0u);
    Gateway_LogFc05Range(898, 910);
#endif
#if FC05_COIL_DIAG_LOG
    Gateway_LogFc05DiagRecv(start_addr, value ? 1u : 0u);
    Gateway_LogFc05DiagRange(898, 910);
#endif

    /* Unified Rule: mainboard coil map (0-based) */
    /* coil0..3 = Mainboard local Relay1..4 (MUST NOT forward downstream)
     * coil4 = PC_ON  → pulse PC_ON_EN
     * coil5 = PC_OFF → set PC_ON_EN = 0
     * coil6 = RESET  → pulse PC_RESET_EN
     */
    if (start_addr <= 3u) {
        MainAutoLink_OnManualRelay((uint8_t)start_addr);
        IO_Main_WriteDO((MainDoChannel_t)start_addr, value ? 1u : 0u);
        /* PC가 직접 릴레이 상태를 변경한 경우에만 EEPROM 저장 (AutoLink 경로는 저장 안 함) */
        OutputStateNvm_NotifyMainRelay((uint8_t)start_addr, value ? 1u : 0u);
        /* 즉시 맵 반영: s_ir_main[11+addr], s_ir_env[1], s_ir_diag[11] */
        ModbusIrMap_OnFc05Write(start_addr, value);
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }
    if (start_addr >= 20u && start_addr <= 23u) {
        MainAutoLink_OnVirtualCoil((uint8_t)(start_addr - 20u), value ? 1u : 0u);
        /* MainAutoLink 갱신 완료 후 즉시 맵 반영: s_ir_main[20..23] */
        ModbusIrMap_OnFc05Write(start_addr, value);
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }
    if (start_addr == 4u) {
        if (value) Gateway_Action_StartPulsePC_ON_EN();
        else BSP_WritePC_ON_EN(0);
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }
    if (start_addr == 5u) {
        /* PC_OFF: writing ON should turn PC_ON_EN off */
        if (value) BSP_WritePC_ON_EN(0);
        else BSP_WritePC_ON_EN(0);
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }
    if (start_addr == 6u) {
        if (value) Gateway_Action_StartPulsePC_RESET_EN();
        else BSP_WritePC_RESET_EN(0);
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }

    uint16_t h2_dec = H2Map_ModbusAddrToH2Dec(start_addr);
    const H2_MapEntry_t *e = H2Map_FindByDec(H2_AREA_1X, h2_dec);
    if (!e) {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepNoMapping(start_addr);
        Gateway_LogFc05StepBeforeSendExceptionToPc(EX_ILLEGAL_DATA_ADDR);
#endif
#if GATEWAY_WRITE_DEBUG_LOG
        Gateway_LogFc05NoMapping(start_addr);
#endif
#if FC05_COIL_DIAG_LOG
        Gateway_LogFc05DiagNoMapping(start_addr);
#endif
        response[0] = 0x85;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }
    /* Defensive: write to read-only range (0821~0836, 0853~0860, 0869~0880) -> 0x03 */
    if (e->rw != H2_RW_WRITE) {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepBeforeSendExceptionToPc(EX_ILLEGAL_DATA_VAL);
#endif
        response[0] = 0x85;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }
    if (e->action == H2_ACT_WRITE_SUB_COIL) {
#if FC05_GW_STEP_LOG
        if (e->h2_dec >= 899u && e->h2_dec <= 910u) {
            uint16_t offset = e->h2_dec - 899u;
            static const uint8_t sid_step[] = { 1, 2, 4, 8 };
            uint8_t slave_id = sid_step[offset / 3u];
            uint16_t sub_coil = offset % 3u;
            Gateway_LogFc05StepMappingResult(slave_id, 0x05, sub_coil);
        }
#endif
        Gateway_LogWriteUpstream(start_addr, value ? 1u : 0u);
#if GATEWAY_WRITE_DEBUG_LOG
        if (e->h2_dec >= 899u && e->h2_dec <= 910u) {
            uint16_t offset = e->h2_dec - 899u;
            static const uint8_t sid[] = { 1, 2, 4, 8 };
            uint8_t slave_id = sid[offset / 3u];
            uint16_t sub_coil = offset % 3u;
            Gateway_LogFc05Mapped(start_addr, slave_id, sub_coil);
        }
#endif
#if FC05_COIL_DIAG_LOG
        if (e->h2_dec >= 899u && e->h2_dec <= 910u) {
            uint16_t offset = e->h2_dec - 899u;
            static const uint8_t sid_diag[] = { 1, 2, 4, 8 };
            uint8_t slave_id = sid_diag[offset / 3u];
            uint16_t sub_coil = offset % 3u;
            Gateway_LogFc05DiagMapped(start_addr, slave_id, sub_coil);
        }
#endif
    }
    if (e->action == H2_ACT_WRITE_SUB_COIL && e->h2_dec >= 899u && e->h2_dec <= 910u) {
#if GATEWAY_WRITE_DEBUG_LOG
        uint16_t off = (uint16_t)(e->h2_dec - 899u);
        static const uint8_t sid_pc[] = { 1u, 2u, 4u, 8u };
        uint8_t sid = sid_pc[off / 3u];
        uint16_t sub_coil = (uint16_t)(off % 3u);
        (void)printf("[PC_REQ] sid=%u coil=%u val=%u\r\n",
                     (unsigned)sid, (unsigned)sub_coil, value ? 1u : 0u);
#endif
        Gateway_WriteSubCoil_SetNextReason("PC_USER");
    }
    if (!H2Map_ApplyWrite(e, value, PULSE_MS_DEFAULT)) {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepLocalException04();
        Gateway_LogFc05StepBeforeSendExceptionToPc(EX_SLAVE_DEVICE_FAIL);
#endif
#if GATEWAY_WRITE_DEBUG_LOG
        Gateway_LogFc05ApplyWriteFail(start_addr);
#endif
#if FC05_COIL_DIAG_LOG
        Gateway_LogFc05DiagApplyFail(start_addr);
#endif
        response[0] = 0x85;
        response[1] = EX_SLAVE_DEVICE_FAIL;  /* 주소는 유효, 하위버스/게이트웨이 쓰기 실패 → 0x04 구분 */
        return 2;
    }
#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepBeforeSendNormalToPc();
#endif
    /* FC05 sub-coil(주소 898~909) 성공 후: 즉시 poll + NVM 저장 + 맵 즉시 반영 */
    if (e->action == H2_ACT_WRITE_SUB_COIL && start_addr >= 898u && start_addr <= 909u) {
        uint16_t offset = (uint16_t)(start_addr - 898u);
        static const uint8_t sid_map[] = { 1u, 2u, 4u, 8u };
        uint8_t sid = sid_map[offset / 3u];
        uint16_t sub_coil = (uint16_t)(offset % 3u);
        ModbusMaster_RequestOnDemandPoll((uint16_t)sid);
        (void)OutputStateNvm_SetSubCoilTarget(sid, sub_coil, value ? 1u : 0u);
        /* 낙관적 즉시 반영: s_ir_main[34..45] (실제 결과는 다음 poll→Aggregator→RefreshAll로 보정) */
        ModbusIrMap_OnFc05Write(start_addr, value);
    }
    response[0] = 0x05;
    response[1] = (uint8_t)(start_addr >> 8);
    response[2] = (uint8_t)(start_addr & 0xFF);
    response[3] = value ? 0xFFu : 0u;
    response[4] = 0u;
    return 5;
}

/* FC15 Write Multiple Coils */
__attribute__((unused)) static int handle_fc15(uint16_t start_addr, uint16_t count, const uint8_t *write_data,
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
        if (e->action == H2_ACT_WRITE_SUB_COIL && e->h2_dec >= 899u && e->h2_dec <= 910u) {
#if GATEWAY_WRITE_DEBUG_LOG
            uint16_t off = (uint16_t)(e->h2_dec - 899u);
            static const uint8_t sid_pc[] = { 1u, 2u, 4u, 8u };
            uint8_t sid = sid_pc[off / 3u];
            uint16_t sub_coil = (uint16_t)(off % 3u);
            (void)printf("[PC_REQ] sid=%u coil=%u val=%u\r\n",
                         (unsigned)sid, (unsigned)sub_coil, value ? 1u : 0u);
#endif
            Gateway_WriteSubCoil_SetNextReason("PC_USER");
        }
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
    /* Unified Rule v1.2: Read = FC04 ONLY. FC01/FC02/FC03 금지. */
    case 0x01:
    case 0x02:
    case 0x03:
        response[0] = (uint8_t)(fc | 0x80);
        response[1] = EX_ILLEGAL_FUNCTION;
        return 2;
    case 0x04:
        return handle_fc04(start_addr, count, p_agg, response, resp_max);
    case 0x05:
        return handle_fc05(start_addr, write_data, response, resp_max);
    /* Unified Rule v1.2: Write = FC05 primary, FC16 reserved for RTC block. */
    case 0x06:
    case 0x0F:
        response[0] = (uint8_t)(fc | 0x80u);
        response[1] = EX_ILLEGAL_FUNCTION;
        return 2;
    case 0x10:
        if (start_addr != UPSTREAM_RTC_REG_START || count != UPSTREAM_RTC_REG_COUNT || !write_data) {
            response[0] = 0x90u;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }
        {
            uint16_t rtc_regs[UPSTREAM_RTC_REG_COUNT];
            for (uint16_t i = 0; i < UPSTREAM_RTC_REG_COUNT; i++) {
                rtc_regs[i] = (uint16_t)(((uint16_t)write_data[i * 2u] << 8)
                    | (uint16_t)write_data[i * 2u + 1u]);
            }
            if (BoardRtc_WriteWordRegs(rtc_regs) != 0) {
                response[0] = 0x90u;
                response[1] = EX_ILLEGAL_DATA_VAL;
                return 2;
            }
            response[0] = 0x10u;
            response[1] = (uint8_t)(start_addr >> 8);
            response[2] = (uint8_t)(start_addr & 0xFFu);
            response[3] = (uint8_t)(count >> 8);
            response[4] = (uint8_t)(count & 0xFFu);
            return 5;
        }
    default:
        response[0] = (uint8_t)(fc | 0x80);
        response[1] = EX_ILLEGAL_FUNCTION;
        return 2;
    }
}
