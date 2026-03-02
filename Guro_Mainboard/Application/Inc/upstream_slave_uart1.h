/**
 * @file upstream_slave_uart1.h
 * @brief Modbus RTU Slave on USART1 (PA9/PA10) with RS485 DE=PB1 for PC↔Mainboard test.
 *        Slave ID 9; FC02/03/05/06/15; 4ms frame end; DE high before TX, low after TX + guard.
 */
#ifndef UPSTREAM_SLAVE_UART1_H
#define UPSTREAM_SLAVE_UART1_H

#include "aggregated_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UpstreamSlaveUart1_Init(void);
void UpstreamSlaveUart1_Poll(const aggregated_status_t *agg);
void UpstreamSlaveUart1_RxEventCallback(uint16_t Size);

/** Debug counters: rx_frame_ok, rx_crc_fail, rx_len_fail, tx_resp. Pass NULL to skip. */
void UpstreamSlaveUart1_GetCounts(uint32_t *rx_ok, uint32_t *rx_crc_fail, uint32_t *rx_len_fail, uint32_t *tx_resp);

/** Weak: called every ~1s from Poll with current counts; override to log (e.g. UART).
 *  When UPSTREAM_DEBUG_LOG=1, use rx_len_fail as invalid_len_count, rx_crc_fail as invalid_crc_count. */
void UpstreamSlaveUart1_LogCounts(uint32_t rx_ok, uint32_t rx_crc_fail, uint32_t rx_len_fail, uint32_t tx_resp);

/** Weak: when UPSTREAM_DEBUG_LOG=1, called per RX frame with HEX(up to 16B), CRC OK/FAIL, FC, addr. Override to output. */
void UpstreamSlaveUart1_LogFrame(const uint8_t *frame, uint16_t len, int crc_ok, uint8_t fc, uint16_t addr);

/** Weak: when UPSTREAM_DEBUG_LOG=1, called when sending Modbus response; override to log e.g. "TX_RSP: 09 03 02 .. .. CRClo CRChi". */
void UpstreamSlaveUart1_LogTxResponse(const uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_SLAVE_UART1_H */
