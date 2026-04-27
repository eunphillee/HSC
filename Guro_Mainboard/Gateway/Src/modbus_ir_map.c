/**
 * @file modbus_ir_map.c
 * @brief FC04 Input Register global map implementation (v1.3).
 *
 * Two banks: RefreshAll writes the back bank, then swaps s_front_idx so FC04
 * and FC05 optimistic patch always see a stable front snapshot.
 */
#include "modbus_ir_map.h"
#include "stm32f2xx.h"   /* __DMB() */
#include "system_config.h"
#include "upstream_slave_h2tech.h"
#include "io_map.h"
#include "bsp_gpio.h"
#include "reset_reason.h"
#include "modbus_master.h"
#include "board_rtc.h"
#include "output_state_nvm.h"
#include "main_auto_link.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Double-buffered zone banks */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t main[MB_IR_MAIN_COUNT];
    uint16_t rtc[MB_IR_RTC_COUNT];
    uint16_t env[MB_IR_ENV_COUNT];
    uint16_t diag[MB_IR_DIAG_COUNT];
} ModbusIrBank_t;

static ModbusIrBank_t s_bank[2];
static volatile uint8_t s_front_idx;

/* Read-back firmware marker for field verification.
 * FC04 addr=4039 should return this exact value when the latest
 * "FC04 side-effect free packed read" firmware is actually flashed. */
#define MB_FW_MARKER_FC04_SIDE_EFFECT_FREE   0xA416u

/* Estimated power model (initial coefficient, can be calibrated later).
 * NOTE: This is NOT true active power metering (no live voltage/power-factor).
 * It is an estimated W value from PKPK ADC at nominal 220Vac. */
#define POWER_NOMINAL_VOLTAGE_V          220.0f
#define POWER_ADC_REF_V                  3.3f
#define POWER_ADC_COUNTS                 4096.0f
#define POWER_SENSOR_SENS_V_PER_A        0.111f
#define POWER_A_PER_PKPK_COUNT           (POWER_ADC_REF_V / POWER_ADC_COUNTS / POWER_SENSOR_SENS_V_PER_A)
#define POWER_W_PER_PKPK_COUNT           (POWER_A_PER_PKPK_COUNT * POWER_NOMINAL_VOLTAGE_V)
#define CURRENT_ON_PKPK_THRESHOLD        1u

static uint16_t estimate_power_w_from_pkpk(uint16_t pkpk_adc, uint16_t cur_st)
{
    if (cur_st == 0u) {
        return 0u;
    }
    if (pkpk_adc < CURRENT_ON_PKPK_THRESHOLD) {
        return 0u;
    }

    {
        float w = (float)pkpk_adc * POWER_W_PER_PKPK_COUNT;
        if (w <= 0.0f) {
            return 0u;
        }
        if (w >= 65535.0f) {
            return 65535u;
        }
        return (uint16_t)(w + 0.5f);
    }
}

static void patch_env_config_diag(ModbusIrBank_t *b)
{
    if (!b) return;
    b->env[2] = (uint16_t)SystemConfig_GetEffectiveMainboardSlaveId(); /* 2102 */
    b->env[3] = UpstreamSlave_GetPendingMainboardSlaveId();            /* 2103 */
    b->env[4] = UpstreamSlave_GetPendingMainboardBaudRate();           /* 2104 */
    {
        const system_config_t *sc = SystemConfig_Get();
        b->env[7] = sc ? SystemConfig_GetPcNoCommTimeoutSecFromCfg(sc)
                       : SYSTEM_CONFIG_DEFAULT_PC_NO_COMM_TIMEOUT_SEC; /* 2107: mirror of 4x3003 */
    }
    b->env[8] = SystemConfig_GetLastSaveStatus();                      /* 2108 */
    b->env[9] = UpstreamSlave_GetLastCoil7SaveFailCode();              /* 2109 */
}

/* ------------------------------------------------------------------ */
/* Refresh: MAIN + PACKED + estimated power (0..93)                     */
/* ------------------------------------------------------------------ */

static void refresh_main(ModbusIrBank_t *b, const aggregated_status_t *agg)
{
    uint16_t *const s_ir_main = b->main;
    uint16_t di   = IO_Main_ReadDI_Bitmap();
    uint16_t dout = IO_Main_ReadDO_Bitmap();

    /* MAIN 0..23 */
    s_ir_main[0]  = (agg && agg->error_flags == 0u) ? 1u : 0u;          /* MAIN_STATUS */
    s_ir_main[1]  = agg ? agg->error_flags : 0u;                          /* ERROR_FLAGS */
    for (uint16_t i = 0u; i < 8u; i++) {
        s_ir_main[2u + i] = (di & (1u << i)) ? 1u : 0u;                  /* D_I_01..08 */
    }
    s_ir_main[10] = (uint16_t)BSP_ReadPC_LED_IN();                        /* PC_LED_IN */
    s_ir_main[11] = (dout & (1u << 0)) ? 1u : 0u;                        /* MAIN_RELAY_01 */
    s_ir_main[12] = (dout & (1u << 1)) ? 1u : 0u;                        /* MAIN_RELAY_02 */
    s_ir_main[13] = (dout & (1u << 2)) ? 1u : 0u;                        /* MAIN_RELAY_03 */
    s_ir_main[14] = (dout & (1u << 3)) ? 1u : 0u;                        /* MAIN_RELAY_04 */
    s_ir_main[15] = agg ? (uint16_t)agg->env_temp_cx10 : (uint16_t)0x8000u; /* ENV_TEMP_EXT0 */
    s_ir_main[16] = agg ? agg->env_rh_x10 : 0xFFFFu;                     /* ENV_RH_EXT0 */
    /* [17..19] RESERVED — zero-initialised in static storage */
    s_ir_main[20] = MainAutoLink_GetVirtEnableWord(0u);                    /* MAIN_VBIT_1 */
    s_ir_main[21] = MainAutoLink_GetVirtEnableWord(1u);                    /* MAIN_VBIT_2 */
    s_ir_main[22] = MainAutoLink_GetVirtEnableWord(2u);                    /* MAIN_VBIT_3 */
    s_ir_main[23] = MainAutoLink_GetVirtEnableWord(3u);                    /* MAIN_VBIT_4 */

    /* PACKED Alive 24..33 */
    s_ir_main[24] = agg ? (agg->hpsb_status_reg  ? 1u : 0u) : 0u;       /* HPSB_ALIVE  */
    /* [25..27] RESERVED */
    s_ir_main[28] = agg ? (agg->lpsb1_alarm_reg  ? 1u : 0u) : 0u;       /* LPSB1_ALIVE */
    s_ir_main[29] = agg ? (agg->lpsb2_alarm_reg  ? 1u : 0u) : 0u;       /* LPSB2_ALIVE */
    s_ir_main[30] = agg ? (agg->lpsb3_alarm_reg  ? 1u : 0u) : 0u;       /* LPSB3_ALIVE */
    /* [31..33] RESERVED */

    /* PACKED Coils 34..45 */
    if (agg) {
        s_ir_main[34] = (agg->hpsb_coils & (1u << 0)) ? 1u : 0u;        /* HPSB_CON_1 */
        s_ir_main[35] = (agg->hpsb_coils & (1u << 1)) ? 1u : 0u;        /* HPSB_CON_2 */
        s_ir_main[36] = (agg->hpsb_coils & (1u << 2)) ? 1u : 0u;        /* HPSB_CON_3 */
        s_ir_main[37] = agg->lpsb1_coils[0] ? 1u : 0u;                   /* LPSB1_SSW1 */
        s_ir_main[38] = agg->lpsb1_coils[1] ? 1u : 0u;                   /* LPSB1_SSW2 */
        s_ir_main[39] = agg->lpsb1_coils[2] ? 1u : 0u;                   /* LPSB1_SSW3 */
        s_ir_main[40] = agg->lpsb2_coils[0] ? 1u : 0u;                   /* LPSB2_SSW1 */
        s_ir_main[41] = agg->lpsb2_coils[1] ? 1u : 0u;                   /* LPSB2_SSW2 */
        s_ir_main[42] = agg->lpsb2_coils[2] ? 1u : 0u;                   /* LPSB2_SSW3 */
        s_ir_main[43] = agg->lpsb3_coils[0] ? 1u : 0u;                   /* LPSB3_SSW1 */
        s_ir_main[44] = agg->lpsb3_coils[1] ? 1u : 0u;                   /* LPSB3_SSW2 */
        s_ir_main[45] = agg->lpsb3_coils[2] ? 1u : 0u;                   /* LPSB3_SSW3 */
    }

    /* PACKED AVG 46..57 */
    if (agg) {
        s_ir_main[46] = agg->hpsb_sense_raw[0];   s_ir_main[47] = agg->hpsb_sense_raw[1];   s_ir_main[48] = agg->hpsb_sense_raw[2];
        s_ir_main[49] = agg->lpsb1_sense_raw[0];  s_ir_main[50] = agg->lpsb1_sense_raw[1];  s_ir_main[51] = agg->lpsb1_sense_raw[2];
        s_ir_main[52] = agg->lpsb2_sense_raw[0];  s_ir_main[53] = agg->lpsb2_sense_raw[1];  s_ir_main[54] = agg->lpsb2_sense_raw[2];
        s_ir_main[55] = agg->lpsb3_sense_raw[0];  s_ir_main[56] = agg->lpsb3_sense_raw[1];  s_ir_main[57] = agg->lpsb3_sense_raw[2];
    }

    /* PACKED PKPK 58..69 */
    if (agg) {
        s_ir_main[58] = agg->hpsb_pkpk[0];   s_ir_main[59] = agg->hpsb_pkpk[1];   s_ir_main[60] = agg->hpsb_pkpk[2];
        s_ir_main[61] = agg->lpsb1_pkpk[0];  s_ir_main[62] = agg->lpsb1_pkpk[1];  s_ir_main[63] = agg->lpsb1_pkpk[2];
        s_ir_main[64] = agg->lpsb2_pkpk[0];  s_ir_main[65] = agg->lpsb2_pkpk[1];  s_ir_main[66] = agg->lpsb2_pkpk[2];
        s_ir_main[67] = agg->lpsb3_pkpk[0];  s_ir_main[68] = agg->lpsb3_pkpk[1];  s_ir_main[69] = agg->lpsb3_pkpk[2];
    }

    /* PACKED CUR 70..81 */
    if (agg) {
        s_ir_main[70] = agg->hpsb_current_st[0];   s_ir_main[71] = agg->hpsb_current_st[1];   s_ir_main[72] = agg->hpsb_current_st[2];
        s_ir_main[73] = agg->lpsb1_current_st[0];  s_ir_main[74] = agg->lpsb1_current_st[1];  s_ir_main[75] = agg->lpsb1_current_st[2];
        s_ir_main[76] = agg->lpsb2_current_st[0];  s_ir_main[77] = agg->lpsb2_current_st[1];  s_ir_main[78] = agg->lpsb2_current_st[2];
        s_ir_main[79] = agg->lpsb3_current_st[0];  s_ir_main[80] = agg->lpsb3_current_st[1];  s_ir_main[81] = agg->lpsb3_current_st[2];
    }

    /* Estimated Power W @220V: 82..93 (uint16_t, 1W unit, saturated).
     * Mapping order is identical to PKPK/CUR blocks:
     * HPSB(82..84), LPSB1(85..87), LPSB2(88..90), LPSB3(91..93). */
    if (agg) {
        for (uint16_t i = 0u; i < 3u; i++) {
            s_ir_main[82u + i] = estimate_power_w_from_pkpk(agg->hpsb_pkpk[i], agg->hpsb_current_st[i]);
            s_ir_main[85u + i] = estimate_power_w_from_pkpk(agg->lpsb1_pkpk[i], agg->lpsb1_current_st[i]);
            s_ir_main[88u + i] = estimate_power_w_from_pkpk(agg->lpsb2_pkpk[i], agg->lpsb2_current_st[i]);
            s_ir_main[91u + i] = estimate_power_w_from_pkpk(agg->lpsb3_pkpk[i], agg->lpsb3_current_st[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Refresh: RTC  (890..896)                                             */
/* ------------------------------------------------------------------ */

static void refresh_rtc(ModbusIrBank_t *b)
{
    (void)BoardRtc_ReadWordRegs(b->rtc);
}

/* ------------------------------------------------------------------ */
/* Refresh: ENV/IO  (2100..2122)                                        */
/* ------------------------------------------------------------------ */

static void refresh_env(ModbusIrBank_t *b, const aggregated_status_t *agg)
{
    uint16_t *const s_ir_env = b->env;
    /* 2102..2109:
     * 2102 effective slave id
     * 2103 pending slave id
     * 2104 pending baud
     * 2108 last SystemConfig_Save status
     * 2109 last coil7 save fail code
     */
    s_ir_env[0]  = IO_Main_ReadDI_Bitmap();                                   /* 2100: MAIN_IO_DI_BITMAP */
    s_ir_env[1]  = IO_Main_ReadDO_Bitmap();                                   /* 2101: MAIN_IO_DO_BITMAP */
    patch_env_config_diag(b); /* 2102..2104,2107..2109 패치 포함(2107=PC 워치독 초) */
    /* [5..6] RESERVED (2105..2106) */
    s_ir_env[10] = agg ? (uint16_t)agg->env_temp_cx10 : (uint16_t)0x8000u;   /* 2110: MAIN_ENV_TEMP     */
    s_ir_env[11] = agg ? agg->env_rh_x10 : 0xFFFFu;                          /* 2111: MAIN_ENV_RH       */
    s_ir_env[12] = agg ? agg->error_flags : 0xFFFFu;                          /* 2112: MAIN_ENV_ERR_FLAGS */
    {
        uint32_t csr = ResetReason_GetRccCsr();
        s_ir_env[13] = (uint16_t)(csr & 0xFFFFu);                             /* 2113: RESET_CSR_LOW  */
        s_ir_env[14] = (uint16_t)(csr >> 16);                                 /* 2114: RESET_CSR_HIGH */
    }
    /* [15..21] RESERVED (2115..2121) */
    s_ir_env[22] = (uint16_t)BSP_ReadPC_LED_IN();                             /* 2122: PC_LED_IN_REG  */
}

/* ------------------------------------------------------------------ */
/* Refresh: DIAG  (4000..4039)                                          */
/* ------------------------------------------------------------------ */

static void refresh_diag(ModbusIrBank_t *b, const aggregated_status_t *agg)
{
    uint16_t *const s_ir_diag = b->diag;
    /* Full zero first: covers reserved/unset slots before explicit assignments. */
    (void)memset(s_ir_diag, 0, sizeof(b->diag));

    /* NOTE: existing code returns 0=정상 / 1=이상 for DIAG_MAIN_STATUS (opposite
     * of MAIN_STATUS at addr 0).  Preserved as-is for backward compatibility. */
    s_ir_diag[0]  = (agg && agg->error_flags == 0u) ? 0u : 1u;                /* 4000: DIAG_MAIN_STATUS      */
    s_ir_diag[1]  = (agg && !(agg->error_flags & AGG_ERR_COMM_HPSB)) ? 1u : 0u; /* 4001: DIAG_HPSB_ONLINE   */
    s_ir_diag[2]  = (agg && !(agg->error_flags & AGG_ERR_COMM_LPSB)) ? 1u : 0u; /* 4002: DIAG_LPSB_ONLINE_ANY */
    s_ir_diag[3]  = agg ? agg->hpsb_status_reg : 0u;                           /* 4003: DIAG_HPSB_STATUS_REG */
    s_ir_diag[4]  = agg ? agg->lpsb1_alarm_reg : 0u;                           /* 4004: DIAG_LPSB_ALARM_REG  */
    s_ir_diag[5]  = agg ? agg->lpsb1_sense_raw[0] : 0u;                        /* 4005: DIAG_LPSB1_SENSE1    */
    s_ir_diag[6]  = agg ? agg->lpsb1_sense_raw[1] : 0u;                        /* 4006: DIAG_LPSB1_SENSE2    */
    s_ir_diag[7]  = agg ? agg->lpsb1_sense_raw[2] : 0u;                        /* 4007: DIAG_LPSB1_SENSE3    */
    s_ir_diag[8]  = agg ? agg->error_flags : 0u;                                /* 4008: DIAG_ERROR_FLAGS      */
    s_ir_diag[9]  = (uint16_t)(ModbusMaster_GetUart2OreCount() & 0xFFFFu);     /* 4009: DIAG_UART_ERR_COUNT   */
    s_ir_diag[10] = IO_Main_ReadDI_Bitmap();                                    /* 4010: DIAG_DI_REMAP         */
    s_ir_diag[11] = IO_Main_ReadDO_Bitmap();                                    /* 4011: DIAG_DO_REMAP         */

    /* 4012..4015: last sub-fail (global) */
    {
        uint8_t sid = 0u, fc = 0u;
        ModbusSubFailReason_t r = MODBUS_SUB_FAIL_NONE;
        uint16_t len = 0u;
        ModbusMaster_GetLastSubFail(&sid, &fc, &r, &len);
        s_ir_diag[12] = (uint16_t)sid;   /* 4012: DIAG_LAST_FAIL_SID    */
        s_ir_diag[13] = (uint16_t)fc;    /* 4013: DIAG_LAST_FAIL_FC     */
        s_ir_diag[14] = (uint16_t)r;     /* 4014: DIAG_LAST_FAIL_REASON */
        s_ir_diag[15] = (uint16_t)len;   /* 4015: DIAG_LAST_FAIL_RXLEN  */
    }

    /* 4016..4031: per-slave fail rows (HPSB, LPSB1, LPSB2, LPSB3) */
    {
        const SlaveId_t sids[4] = { SLAVE_ID_HPSB, SLAVE_ID_LPSB1, SLAVE_ID_LPSB2, SLAVE_ID_LPSB3 };
        uint16_t w = 16u;
        for (uint16_t i = 0u; i < 4u; i++) {
            uint8_t fc = 0u;
            ModbusSubFailReason_t r2 = MODBUS_SUB_FAIL_NONE;
            uint16_t len2 = 0u;
            ModbusMaster_GetSubFailForSlave(sids[i], &fc, &r2, &len2);
            s_ir_diag[w++] = (uint16_t)((uint8_t)sids[i]);
            s_ir_diag[w++] = (uint16_t)fc;
            s_ir_diag[w++] = (uint16_t)r2;
            s_ir_diag[w++] = (uint16_t)len2;
        }
    }

    s_ir_diag[32] = OutputStateNvm_Get() ? 1u : 0u;             /* 4032: NVMI_LOADED           */
    s_ir_diag[33] = (uint16_t)OutputStateNvm_IsEepromDirty();   /* 4033: NVMI_DIRTY            */
    s_ir_diag[34] = OutputStateNvm_GetSequence();                /* 4034: NVMI_SEQUENCE         */
    s_ir_diag[35] = OutputStateNvm_GetLastSaveResult();          /* 4035: NVMI_LAST_SAVE_RESULT */
    s_ir_diag[36] = OutputStateNvm_GetLastLoadResult();          /* 4036: NVMI_LAST_LOAD_RESULT */
    s_ir_diag[37] = OutputStateNvm_GetRestoreDoneMask();         /* 4037: NVM_RESTORE_TRY_MASK  */
    s_ir_diag[38] = OutputStateNvm_GetRestoreOkMask();           /* 4038: NVM_RESTORE_OK_MASK   */
    s_ir_diag[39] = MB_FW_MARKER_FC04_SIDE_EFFECT_FREE;          /* 4039: FW_MARKER */
}

/* ------------------------------------------------------------------ */
/* Public: refresh all zones                                            */
/* ------------------------------------------------------------------ */

void ModbusIrMap_RefreshAll(const aggregated_status_t *agg)
{
    uint8_t back = (uint8_t)(1u - s_front_idx);
    ModbusIrBank_t *b = &s_bank[back];

    refresh_main(b, agg);
    refresh_rtc(b);
    refresh_env(b, agg);
    refresh_diag(b, agg);

    __DMB();
    s_front_idx = back;
}

void ModbusIrMap_SyncConfigDiag(void)
{
    patch_env_config_diag(&s_bank[0]);
    patch_env_config_diag(&s_bank[1]);
    __DMB();
}

/* ------------------------------------------------------------------ */
/* Immediate patch after FC05 write                                     */
/* ------------------------------------------------------------------ */

void ModbusIrMap_OnFc05Write(uint16_t addr, bool value)
{
    uint8_t f = s_front_idx;
    __DMB();
    ModbusIrBank_t *b = &s_bank[f];
    uint16_t *const s_ir_main = b->main;
    uint16_t *const s_ir_env = b->env;
    uint16_t *const s_ir_diag = b->diag;

    const uint16_t v = value ? 1u : 0u;

    /* ---- MAIN relay: addr 0..3 ---- *
     * s_ir_main[11..14] = MAIN_RELAY_01..04
     * s_ir_env[1]  = MAIN_IO_DO_BITMAP  (2101)
     * s_ir_diag[11]= DIAG_DO_REMAP      (4011) */
    if (addr <= 3u) {
        s_ir_main[11u + addr] = v;
        /* Re-read GPIO bitmap: IO_Main_WriteDO() already committed the pin */
        uint16_t do_bm = IO_Main_ReadDO_Bitmap();
        s_ir_env[1]    = do_bm;
        s_ir_diag[11]  = do_bm;
        return;
    }

    /* ---- VBIT: addr 20..23 ---- *
     * s_ir_main[20..23] = MAIN_VBIT_1..4
     * MainAutoLink_OnVirtualCoil() was already called by handle_fc05. */
    if (addr >= 20u && addr <= 23u) {
        s_ir_main[addr] = MainAutoLink_GetVirtEnableWord(addr - 20u);
        return;
    }

    /* ---- Sub-coil optimistic update: addr 898..909 ---- *
     * s_ir_main[34..45]:
     *   898..900 → [34..36] HPSB_CON_1..3
     *   901..903 → [37..39] LPSB1_SSW1..3
     *   904..906 → [40..42] LPSB2_SSW1..3
     *   907..909 → [43..45] LPSB3_SSW1..3
     * Optimistic: actual hw state corrected by RefreshAll after next poll. */
    if (addr >= 898u && addr <= 909u) {
        s_ir_main[34u + (addr - 898u)] = v;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Public: FC04 response from map                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t        start;
    uint16_t        count;
    const uint16_t *data;
} IrZone_t;

int ModbusIrMap_Fc04Response(uint16_t start_addr, uint16_t count,
                              uint8_t *response, uint16_t resp_max)
{
    if (count == 0u) {
        response[0] = 0x84u;
        response[1] = 0x03u;  /* EX_ILLEGAL_DATA_VAL */
        return 2;
    }

    uint8_t f = s_front_idx;
    __DMB();
    const ModbusIrBank_t *bk = &s_bank[f];

    const IrZone_t zones[4] = {
        { MB_IR_MAIN_START,  MB_IR_MAIN_COUNT,  bk->main  },
        { MB_IR_RTC_START,   MB_IR_RTC_COUNT,   bk->rtc   },
        { MB_IR_ENV_START,   MB_IR_ENV_COUNT,   bk->env   },
        { MB_IR_DIAG_START,  MB_IR_DIAG_COUNT,  bk->diag  },
    };

    const uint32_t end = (uint32_t)start_addr + (uint32_t)count - 1u;

    for (uint16_t z = 0u; z < 4u; z++) {
        const IrZone_t *zone = &zones[z];
        const uint32_t zone_end = (uint32_t)zone->start + (uint32_t)zone->count - 1u;

        if ((uint32_t)start_addr >= (uint32_t)zone->start && end <= zone_end) {
            const uint16_t byte_count = (uint16_t)(count * 2u);
            if (resp_max < (uint16_t)(2u + byte_count)) return -1;

            response[0] = 0x04u;
            response[1] = (uint8_t)byte_count;

            const uint16_t off = (uint16_t)(start_addr - zone->start);
            for (uint16_t i = 0u; i < count; i++) {
                uint16_t v = zone->data[off + i];
                response[2u + i * 2u]      = (uint8_t)(v >> 8);
                response[2u + i * 2u + 1u] = (uint8_t)(v & 0xFFu);
            }
            return (int)(2u + byte_count);
        }
    }

    /* No zone matched */
    response[0] = 0x84u;
    response[1] = 0x02u;  /* EX_ILLEGAL_DATA_ADDR */
    return 2;
}
