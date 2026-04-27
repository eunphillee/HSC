/**
 * @file upstream_slave_h2tech.h
 * @brief Upstream Modbus Slave (PC link): H2TECH address handling.
 *        MAIN acts as slave on USART1; uses h2tech_address_map + aggregated image.
 */
#ifndef UPSTREAM_SLAVE_H2TECH_H
#define UPSTREAM_SLAVE_H2TECH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle one Modbus request from PC (H2TECH addresses).
 * @param fc         Modbus function code (01/02/03/04/05/06/15/16 지원).
 * @param start_addr H2TECH logical start address (e.g. 821).
 * @param count      Number of coils/discrete/registers.
 * @param write_data For FC05/06/15/16, payload; else NULL.
 * @param p_agg      Pointer to aggregated status image (aggregated_status_t); cast from application.
 * @param response   Output buffer for response PDU (no slave_id, no CRC).
 * @param resp_max   Max bytes in response.
 * @return >0 response length, 0 if exception sent (check exception code), <0 error.
 *
 * Exception handling:
 * - If any address in [start_addr, start_addr+count) not in translation table -> 0x02.
 * - If FC not supported -> 0x01. If rw violation or bad value -> 0x03.
 * Exception response format: response[0]=FC|0x80, response[1]=exception_code; return 2.
 */
int UpstreamSlave_HandleRequest(uint8_t fc, uint16_t start_addr, uint16_t count,
                                const uint8_t *write_data,
                                const void *p_agg,
                                uint8_t *response, uint16_t resp_max);

/** Call after SystemConfig_Load (boot). Syncs pending IR2103 from effective ID. */
void UpstreamSlave_InitMainboardSlavePending(void);

/** Pending mainboard slave ID for FC04 IR 2103 (before EEPROM save). */
uint16_t UpstreamSlave_GetPendingMainboardSlaveId(void);

/** Pending mainboard baud rate for FC04 IR 2104 (before EEPROM save). */
uint16_t UpstreamSlave_GetPendingMainboardBaudRate(void);

/* FC04 IR 2109: last FC05 coil7 save fail code. */
#define UPSTREAM_COIL7_SAVE_FAIL_NONE              0u
#define UPSTREAM_COIL7_SAVE_FAIL_INVALID_PENDING   1u
#define UPSTREAM_COIL7_SAVE_FAIL_NULL_CFG          2u
#define UPSTREAM_COIL7_SAVE_FAIL_SYSCFG_BASE     0x100u

/**
 * Last FC05 coil7 save fail code.
 * - 0: success / no failure
 * - 1: pending slave id invalid or reserved
 * - 2: SystemConfig_Get() returned NULL
 * - 0x100 | n: SystemConfig_Save() failed, where n is SYSCFG_SAVE_STATUS_*
 */
uint16_t UpstreamSlave_GetLastCoil7SaveFailCode(void);

/** Call periodically from main loop. Executes PC watchdog action by EEPROM timeout policy. */
void UpstreamSlave_PcWatchdogTick(void);

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_SLAVE_H2TECH_H */
