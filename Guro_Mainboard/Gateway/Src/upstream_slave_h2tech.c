/**
 * @file upstream_slave_h2tech.c
 * @brief Upstream Modbus Slave (PC link): Unified Rule v1.2.
 *        Read = FC04 only. Write = FC05 only.
 *        FC01/FC02/FC03/FC06/FC15/FC16 → EX_ILLEGAL_FUNCTION.
 *        Illegal address -> EX_ILLEGAL_DATA_ADDR(0x02).
 */
#include "upstream_slave_h2tech.h"
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
#define UPSTREAM_DIAG_IR_COUNT      32u
/* RTC block: 4x0891..0897 (PDU 890..896) */
#define UPSTREAM_RTC_REG_START      890u
#define UPSTREAM_RTC_REG_COUNT      7u

#if ENABLE_MB_FC04_MAIN_DEBUG
static void log_fc04_main_snapshot(uint16_t di_now, uint16_t do_now, const uint16_t *regs)
{
    extern UART_HandleTypeDef huart1;
    if (!regs) return;
    /* Unified debug values for DI_03 <-> REG4 <-> RELAY3 mapping verification */
    int di3 = (di_now & (1u << 2)) ? 1 : 0;          /* DI_03 == bitmap bit2 */
    int reg4 = regs[4] ? 1 : 0;                      /* FC04 MAP_MAIN regs[4] == DI_03 */
    int relay3 = (regs[11] & (1u << 2)) ? 1 : 0;     /* reg11 bit2 == RELAY3 actual */
    int relay3_gpio = (HAL_GPIO_ReadPin(RELAY3_EN_GPIO_Port, RELAY3_EN_Pin) == GPIO_PIN_SET) ? 1 : 0;

    /* User-requested direct printf diagnostics (enable only when needed). */
    (void)printf("RELAY3 GPIO=%d\r\n", relay3_gpio);
    (void)printf("DI3=%d REG4=%d RELAY3=%d\r\n", di3, reg4, relay3);

    char buf[220];
    int n = snprintf(
        buf, sizeof(buf),
        "[MB][FC04][MAIN] di_now=0x%02X do_now=0x%02X regs2_9=[%u,%u,%u,%u,%u,%u,%u,%u] reg11=0x%02X\r\n",
        (unsigned)(di_now & 0xFFu),
        (unsigned)(do_now & 0xFFu),
        (unsigned)regs[2], (unsigned)regs[3], (unsigned)regs[4], (unsigned)regs[5],
        (unsigned)regs[6], (unsigned)regs[7], (unsigned)regs[8], (unsigned)regs[9],
        (unsigned)(regs[11] & 0xFFu)
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

/* FC01 Read Coils: writable 1x 영역(0892~0910)을 coil 상태로 제공 */
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
            /* pulse coil(0892~0898)은 래치하지 않으므로 read는 0 유지 */
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

/* FC04 Read Input Registers: 진단/상태 snapshot 제공 (service extension) */
static int handle_fc04(uint16_t start_addr, uint16_t count, const void *p_agg,
                       uint8_t *response, uint16_t resp_max)
{
    const aggregated_status_t *agg = (const aggregated_status_t *)p_agg;

    if (start_addr >= UPSTREAM_RTC_REG_START
        && start_addr < (uint16_t)(UPSTREAM_RTC_REG_START + UPSTREAM_RTC_REG_COUNT)) {
        if (count == 0u
            || (uint32_t)start_addr + (uint32_t)count
                > (uint32_t)UPSTREAM_RTC_REG_START + (uint32_t)UPSTREAM_RTC_REG_COUNT) {
            response[0] = 0x84;
            response[1] = EX_ILLEGAL_DATA_ADDR;
            return 2;
        }
        if (resp_max < (uint16_t)(2u + count * 2u)) return -1;
        uint16_t rtc_regs[UPSTREAM_RTC_REG_COUNT] = {0};
        (void)BoardRtc_ReadWordRegs(rtc_regs);
        response[0] = 0x04;
        response[1] = (uint8_t)(count * 2u);
        {
            uint16_t off = (uint16_t)(start_addr - UPSTREAM_RTC_REG_START);
            for (uint16_t i = 0; i < count; i++) {
                uint16_t v = rtc_regs[off + i];
                response[2u + i * 2u] = (uint8_t)(v >> 8);
                response[2u + i * 2u + 1u] = (uint8_t)(v & 0xFFu);
            }
        }
        return (int)(2u + count * 2u);
    }

    /* Unified Rule v1.1 FC04 map (0-based)
     * - Mainboard: 0..23 (14 regs + reserve + virtual bits)
     * - HPSB copy: 100..115 (16 regs: 0..15)
     * - LPSB(2) copy: 200..213 (14 regs: 0..13)
     * - LPSB(4) copy: 300..313 (14 regs: 0..13)
     * - LPSB(8) copy: 400..413 (14 regs: 0..13)
     */
    enum { MAP_MAIN_SIZE = 24u, MAP_HPSB_SIZE = 16u, MAP_LPSB_SIZE = 14u, MAP_MAX_SIZE = 24u };
    uint16_t base = 0xFFFFu;
    enum { MAP_MAIN, MAP_HPSB, MAP_LPSB2, MAP_LPSB4, MAP_LPSB8 } which = MAP_MAIN;

    if (start_addr == UPSTREAM_DIAG_IR_START) {
        /* Legacy diagnostic extension remains supported at 4000.. */
        if (count == 0u || count > UPSTREAM_DIAG_IR_COUNT) {
            response[0] = 0x84;
            response[1] = EX_ILLEGAL_DATA_VAL;
            return 2;
        }
        if (resp_max < (uint16_t)(2u + count * 2u)) return -1;

        uint16_t regs[UPSTREAM_DIAG_IR_COUNT] = {0};
        regs[0] = (agg && agg->error_flags == 0u) ? 0u : 1u; /* main status code */
        regs[1] = (agg && !(agg->error_flags & AGG_ERR_COMM_HPSB)) ? 1u : 0u; /* hpsb online */
        regs[2] = (agg && !(agg->error_flags & AGG_ERR_COMM_LPSB)) ? 1u : 0u; /* lpsb online(any) */
        regs[3] = agg ? agg->hpsb_status_reg : 0u;
        regs[4] = agg ? agg->lpsb1_alarm_reg : 0u;
        regs[5] = agg ? agg->lpsb1_sense_raw[0] : 0u;
        regs[6] = agg ? agg->lpsb1_sense_raw[1] : 0u;
        regs[7] = agg ? agg->lpsb1_sense_raw[2] : 0u;
        regs[8] = agg ? agg->error_flags : 0u;
        /* UART2 ORE(overrun) counter: non-zero이면 하위 응답 바이트를 놓치고 있을 가능성이 큼 */
        regs[9] = (uint16_t)(ModbusMaster_GetUart2OreCount() & 0xFFFFu);
        regs[10] = IO_Main_ReadDI_Bitmap();
        regs[11] = IO_Main_ReadDO_Bitmap();
        /* Last sub-bus failure reason (ModbusMaster) */
        {
            uint8_t sid = 0u, fc = 0u;
            ModbusSubFailReason_t r = MODBUS_SUB_FAIL_NONE;
            uint16_t len = 0u;
            ModbusMaster_GetLastSubFail(&sid, &fc, &r, &len);
            regs[12] = (uint16_t)sid;
            regs[13] = (uint16_t)fc;
            regs[14] = (uint16_t)r;
            regs[15] = (uint16_t)len;
        }

        /* Per-slave sub-bus failure table (HPSB=1, LPSB=2/4/8) */
        {
            const SlaveId_t sids[4] = { SLAVE_ID_HPSB, SLAVE_ID_LPSB1, SLAVE_ID_LPSB2, SLAVE_ID_LPSB3 };
            uint16_t w = 16u;
            for (uint16_t i = 0u; i < 4u; i++) {
                uint8_t fc = 0u;
                ModbusSubFailReason_t r = MODBUS_SUB_FAIL_NONE;
                uint16_t len = 0u;
                ModbusMaster_GetSubFailForSlave(sids[i], &fc, &r, &len);
                regs[w++] = (uint16_t)((uint8_t)sids[i]); /* slave id */
                regs[w++] = (uint16_t)fc;                /* fc */
                regs[w++] = (uint16_t)r;                 /* reason */
                regs[w++] = (uint16_t)len;               /* rx_len */
            }
        }

        response[0] = 0x04;
        response[1] = (uint8_t)(count * 2u);
        for (uint16_t i = 0; i < count; i++) {
            response[2u + i * 2u] = (uint8_t)(regs[i] >> 8);
            response[2u + i * 2u + 1u] = (uint8_t)(regs[i] & 0xFFu);
        }
        return (int)(2u + count * 2u);
    }

    if (start_addr < MAP_MAIN_SIZE) {
        base = 0u;
        which = MAP_MAIN;
        /* Mainboard 자체 데이터: downstream poll 불필요 */
    } else if (start_addr >= 100u && start_addr < 116u) {
        base = 100u;
        which = MAP_HPSB;
        /* PC가 HPSB 데이터를 요청 → 다음 poll 주기에 HPSB를 1회 읽도록 요청 */
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_HPSB);
    } else if (start_addr >= 200u && start_addr < 214u) {
        base = 200u;
        which = MAP_LPSB2;
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB1);
    } else if (start_addr >= 300u && start_addr < 314u) {
        base = 300u;
        which = MAP_LPSB4;
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB2);
    } else if (start_addr >= 400u && start_addr < 414u) {
        base = 400u;
        which = MAP_LPSB8;
        ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_LPSB3);
    } else {
        response[0] = 0x84;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }

    uint16_t map_size = MAP_MAIN_SIZE;
    if (which == MAP_HPSB) map_size = MAP_HPSB_SIZE;
    else if (which == MAP_LPSB2 || which == MAP_LPSB4 || which == MAP_LPSB8) map_size = MAP_LPSB_SIZE;

    if (count == 0u || count > map_size) {
        response[0] = 0x84;
        response[1] = EX_ILLEGAL_DATA_VAL;
        return 2;
    }

    uint16_t offset = (uint16_t)(start_addr - base);
    if ((uint16_t)(offset + count) > map_size) {
        response[0] = 0x84;
        response[1] = EX_ILLEGAL_DATA_ADDR;
        return 2;
    }

    if (resp_max < (uint16_t)(2u + count * 2u)) return -1;

    uint16_t regs[MAP_MAX_SIZE] = {0};
    uint16_t di_now = 0u;
    uint16_t do_now = 0u;

    switch (which) {
    case MAP_MAIN:
        regs[0] = (agg && agg->error_flags == 0u) ? 1u : 0u;
        regs[1] = agg ? agg->error_flags : 0u;
        /* DI/DO는 집계 스냅샷 대신 실시간 GPIO bitmap을 사용해 UI 표시 지연/불일치 방지 */
        {
            di_now = IO_Main_ReadDI_Bitmap();
            for (uint16_t i = 0; i < 8u; i++) {
                regs[2u + i] = (di_now & (1u << i)) ? 1u : 0u;
            }
        }
        regs[10] = (uint16_t)BSP_ReadPC_LED_IN();
        /* Mainboard local relay states (bitmap bits0..3 = Relay1..4), 실시간 GPIO read */
        do_now = IO_Main_ReadDO_Bitmap();
        regs[11] = (uint16_t)(do_now & 0x0Fu);
        /* Env (SHTC3): temp_c_x10 (signed), rh_x10 (unsigned). If sensor error -> -32768 / 0xFFFF. */
        regs[12] = agg ? (uint16_t)agg->env_temp_cx10 : (uint16_t)0x8000u;
        regs[13] = agg ? agg->env_rh_x10 : 0xFFFFu;
        for (uint16_t r = 14u; r < 20u; r++) regs[r] = 0u;
        regs[20] = MainAutoLink_GetVirtEnableWord(0u);
        regs[21] = MainAutoLink_GetVirtEnableWord(1u);
        regs[22] = MainAutoLink_GetVirtEnableWord(2u);
        regs[23] = MainAutoLink_GetVirtEnableWord(3u);
        break;
    case MAP_HPSB:
        regs[0] = agg ? (agg->hpsb_status_reg ? 1u : 0u) : 0u;
        regs[1] = 0u;
        regs[2] = agg ? ((agg->hpsb_coils & (1u << 0)) ? 1u : 0u) : 0u;
        regs[3] = agg ? ((agg->hpsb_coils & (1u << 1)) ? 1u : 0u) : 0u;
        regs[4] = agg ? ((agg->hpsb_coils & (1u << 2)) ? 1u : 0u) : 0u;
        regs[5] = agg ? ((agg->hpsb_coils & (1u << 3)) ? 1u : 0u) : 0u;
        regs[6]  = agg ? agg->hpsb_sense_raw[0] : 0u; /* ADC1 AVG */
        regs[7]  = agg ? agg->hpsb_sense_raw[1] : 0u; /* ADC2 AVG */
        regs[8]  = agg ? agg->hpsb_sense_raw[2] : 0u; /* ADC3 AVG */
        regs[9]  = agg ? agg->hpsb_pkpk[0] : 0u;      /* ADC1 PKPK */
        regs[10] = agg ? agg->hpsb_pkpk[1] : 0u;      /* ADC2 PKPK */
        regs[11] = agg ? agg->hpsb_pkpk[2] : 0u;      /* ADC3 PKPK (may be 0 if not provided) */
        regs[12] = agg ? agg->hpsb_current_st[0] : 0u;
        regs[13] = agg ? agg->hpsb_current_st[1] : 0u;
        regs[14] = agg ? agg->hpsb_current_st[2] : 0u;
        regs[15] = 0u; /* reserve */
        break;
    case MAP_LPSB2:
        /* LPSB slave2 (modbus id=2) = aggregated_status.lpsb1_* */
        regs[0] = agg ? (agg->lpsb1_alarm_reg ? 1u : 0u) : 0u;
        regs[1] = 0u;
        regs[2] = agg ? (agg->lpsb1_coils[0] ? 1u : 0u) : 0u;
        regs[3] = agg ? (agg->lpsb1_coils[1] ? 1u : 0u) : 0u;
        regs[4] = agg ? (agg->lpsb1_coils[2] ? 1u : 0u) : 0u;
        regs[5] = agg ? agg->lpsb1_sense_raw[0] : 0u;
        regs[6] = agg ? agg->lpsb1_sense_raw[1] : 0u;
        regs[7] = agg ? agg->lpsb1_sense_raw[2] : 0u;
        regs[8] = agg ? agg->lpsb1_pkpk[0] : 0u;
        regs[9] = agg ? agg->lpsb1_pkpk[1] : 0u;
        regs[10] = agg ? agg->lpsb1_pkpk[2] : 0u;
        regs[11] = agg ? agg->lpsb1_current_st[0] : 0u;
        regs[12] = agg ? agg->lpsb1_current_st[1] : 0u;
        regs[13] = agg ? agg->lpsb1_current_st[2] : 0u;
        break;
    case MAP_LPSB4:
        /* LPSB slave4 (modbus id=4) = aggregated_status.lpsb2_* */
        regs[0] = agg ? (agg->lpsb2_alarm_reg ? 1u : 0u) : 0u;
        regs[1] = 0u;
        regs[2] = agg ? (agg->lpsb2_coils[0] ? 1u : 0u) : 0u;
        regs[3] = agg ? (agg->lpsb2_coils[1] ? 1u : 0u) : 0u;
        regs[4] = agg ? (agg->lpsb2_coils[2] ? 1u : 0u) : 0u;
        regs[5] = agg ? agg->lpsb2_sense_raw[0] : 0u;
        regs[6] = agg ? agg->lpsb2_sense_raw[1] : 0u;
        regs[7] = agg ? agg->lpsb2_sense_raw[2] : 0u;
        regs[8] = agg ? agg->lpsb2_pkpk[0] : 0u;
        regs[9] = agg ? agg->lpsb2_pkpk[1] : 0u;
        regs[10] = agg ? agg->lpsb2_pkpk[2] : 0u;
        regs[11] = agg ? agg->lpsb2_current_st[0] : 0u;
        regs[12] = agg ? agg->lpsb2_current_st[1] : 0u;
        regs[13] = agg ? agg->lpsb2_current_st[2] : 0u;
        break;
    case MAP_LPSB8:
        /* LPSB slave8 (modbus id=8) = aggregated_status.lpsb3_* */
        regs[0] = agg ? (agg->lpsb3_alarm_reg ? 1u : 0u) : 0u;
        regs[1] = 0u;
        regs[2] = agg ? (agg->lpsb3_coils[0] ? 1u : 0u) : 0u;
        regs[3] = agg ? (agg->lpsb3_coils[1] ? 1u : 0u) : 0u;
        regs[4] = agg ? (agg->lpsb3_coils[2] ? 1u : 0u) : 0u;
        regs[5] = agg ? agg->lpsb3_sense_raw[0] : 0u;
        regs[6] = agg ? agg->lpsb3_sense_raw[1] : 0u;
        regs[7] = agg ? agg->lpsb3_sense_raw[2] : 0u;
        regs[8] = agg ? agg->lpsb3_pkpk[0] : 0u;
        regs[9] = agg ? agg->lpsb3_pkpk[1] : 0u;
        regs[10] = agg ? agg->lpsb3_pkpk[2] : 0u;
        regs[11] = agg ? agg->lpsb3_current_st[0] : 0u;
        regs[12] = agg ? agg->lpsb3_current_st[1] : 0u;
        regs[13] = agg ? agg->lpsb3_current_st[2] : 0u;
        break;
    }

#if ENABLE_MB_FC04_MAIN_DEBUG
    if (which == MAP_MAIN) {
        log_fc04_main_snapshot(di_now, do_now, regs);
    }
#endif

    response[0] = 0x04;
    response[1] = (uint8_t)(count * 2u);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = regs[offset + i];
        response[2u + i * 2u] = (uint8_t)(v >> 8);
        response[2u + i * 2u + 1u] = (uint8_t)(v & 0xFFu);
    }
    return (int)(2u + count * 2u);
}

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
    Gateway_LogFc05Range(892, 910);
#endif
#if FC05_COIL_DIAG_LOG
    Gateway_LogFc05DiagRecv(start_addr, value ? 1u : 0u);
    Gateway_LogFc05DiagRange(892, 910);
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
        response[0] = 0x05;
        response[1] = (uint8_t)(start_addr >> 8);
        response[2] = (uint8_t)(start_addr & 0xFFu);
        response[3] = value ? 0xFFu : 0u;
        response[4] = 0u;
        return 5;
    }
    if (start_addr >= 20u && start_addr <= 23u) {
        MainAutoLink_OnVirtualCoil((uint8_t)(start_addr - 20u), value ? 1u : 0u);
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
    /* FC05 성공 후 해당 slave 상태를 즉시 갱신하도록 on-demand poll 요청 */
    if (e->action == H2_ACT_WRITE_SUB_COIL && e->h2_dec >= 899u && e->h2_dec <= 910u) {
        uint16_t offset = (uint16_t)(e->h2_dec - 899u);
        static const uint8_t sid_poll[] = { 1u, 2u, 4u, 8u };
        uint8_t poll_sid = sid_poll[offset / 3u];
        ModbusMaster_RequestOnDemandPoll((uint16_t)poll_sid);
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
