/**
 * @file gateway_write_log.h
 * @brief Gateway write path debug log (PC FC05 898..909 → UART2 to HPSB/LPSB). Enable with GATEWAY_WRITE_DEBUG_LOG=1.
 */
#ifndef GATEWAY_WRITE_LOG_H
#define GATEWAY_WRITE_LOG_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if GATEWAY_WRITE_DEBUG_LOG
void Gateway_LogWriteUpstream(uint16_t addr, uint8_t value);
void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value);
void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value);
void Gateway_LogUart2TxResult(int ok);
#else
static inline void Gateway_LogWriteUpstream(uint16_t addr, uint8_t value) { (void)addr; (void)value; }
static inline void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value) { (void)slave_id; (void)coil; (void)value; }
static inline void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value) { (void)slave_id; (void)coil; (void)value; }
static inline void Gateway_LogUart2TxResult(int ok) { (void)ok; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_WRITE_LOG_H */
