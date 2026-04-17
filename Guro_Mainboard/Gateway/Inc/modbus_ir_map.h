/**
 * @file modbus_ir_map.h
 * @brief FC04 Input Register global map: 4 static zone arrays (MAIN/RTC/ENV/DIAG).
 *
 * Zones (v1.3 address map):
 *   s_ir_main[82]   : addr   0 ..   81  (MAIN 0..23 + PACKED 24..81)
 *   s_ir_rtc[7]     : addr 890 ..  896  (RTC)
 *   s_ir_env[23]    : addr 2100 .. 2122
 *                     - 2100..2101 : MAIN IO bitmaps
 *                     - 2102       : effective slave id
 *                     - 2103       : pending slave id
 *                     - 2104       : pending baudrate
 *                     - 2108       : last SystemConfig_Save() status
 *                     - 2109       : last FC05 coil7 save fail code
 *                     - 2113..2114 : reset CSR
 *                     - 2122       : PC_LED_IN
 *   s_ir_diag[40]   : addr 4000 .. 4039 (DIAG/NVM/FW marker at 4039)
 *
 * Usage:
 *   1) Call ModbusIrMap_RefreshAll(agg) periodically (100ms, from SystemSync_Update).
 *   2) In handle_fc04: call ModbusIrMap_Fc04Response(start, count, resp, max).
 */
#ifndef MODBUS_IR_MAP_H
#define MODBUS_IR_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include "aggregated_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Zone start addresses (v1.3) ---- */
#define MB_IR_MAIN_START    0u
#define MB_IR_RTC_START   890u
#define MB_IR_ENV_START  2100u
#define MB_IR_DIAG_START 4000u

/* ---- Zone sizes ---- */
#define MB_IR_MAIN_COUNT   82u  /* 0..81              */
#define MB_IR_RTC_COUNT     7u  /* 890..896            */
#define MB_IR_ENV_COUNT    23u  /* 2100..2122          */
#define MB_IR_DIAG_COUNT   40u  /* 4000..4039          */

/* Total static SRAM: (82+7+23+40)*2 = 304 bytes */

/**
 * Refresh all IR zones from the given aggregated state + live GPIO reads.
 * Call from the 100ms periodic task (SystemSync_Update).
 *
 * @param agg  Pointer to current aggregated_status_t; NULL is safe (zeroes maps).
 */
void ModbusIrMap_RefreshAll(const aggregated_status_t *agg);

/**
 * Build a FC04 (0x04) response PDU from the pre-filled map.
 *
 * Validates that [start_addr, start_addr+count-1] lies within exactly one
 * valid zone.  On success writes:
 *   response[0] = 0x04
 *   response[1] = count*2   (byte count)
 *   response[2..] = big-endian register values
 *
 * @return  >0  PDU length (2 + count*2).
 *           2  Exception PDU already written (response[0]=0x84, [1]=exception code).
 *          -1  Internal buffer too small.
 */
int ModbusIrMap_Fc04Response(uint16_t start_addr, uint16_t count,
                              uint8_t *response, uint16_t resp_max);

/**
 * Immediately patch the IR map after a successful FC05 write.
 * Call AFTER the GPIO / AutoLink / downstream-queue function has completed.
 *
 * Addresses handled (v1.3):
 *   0..3    MAIN relay   → s_ir_main[11..14], s_ir_env[1], s_ir_diag[11]
 *   20..23  VBIT         → s_ir_main[20..23]  (reads MainAutoLink after write)
 *   898..909 sub-coil    → s_ir_main[34..45]  (optimistic; RefreshAll corrects)
 *
 * @param addr   FC05 0-based Modbus address
 * @param value  true = coil ON
 */
void ModbusIrMap_OnFc05Write(uint16_t addr, bool value);

/**
 * Immediately synchronize ENV config/diagnostic registers that are otherwise
 * refreshed by the 100ms periodic map update:
 *   2102 effective slave id
 *   2103 pending slave id
 *   2104 pending baudrate
 *   2108 last SystemConfig_Save() status
 *   2109 last FC05 coil7 save fail code
 */
void ModbusIrMap_SyncConfigDiag(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_IR_MAP_H */
