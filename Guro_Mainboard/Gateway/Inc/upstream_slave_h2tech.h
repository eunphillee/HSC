/**
 * @file upstream_slave_h2tech.h
 * @brief Upstream Modbus Slave (PC link): H2TECH address handling skeleton.
 *        MAIN acts as slave on USART2; uses h2tech_address_map + aggregated image.
 */
#ifndef UPSTREAM_SLAVE_H2TECH_H
#define UPSTREAM_SLAVE_H2TECH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle one Modbus request from PC (H2TECH addresses).
 * @param fc         Modbus function code (01..04 read, 05/06/15/16 write).
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

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_SLAVE_H2TECH_H */
