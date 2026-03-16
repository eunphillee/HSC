/**
 * @file gateway_write_log.h
 * @brief Gateway write path debug log (PC FC05 → UART2 to HPSB/LPSB). FC06 path log (FC06_DEBUG_LOG=1).
 */
#ifndef GATEWAY_WRITE_LOG_H
#define GATEWAY_WRITE_LOG_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if FC05_GW_STEP_LOG
void Gateway_LogFc05StepRecvFromPc(void);
void Gateway_LogFc05StepRawCoilValue(uint16_t coil, uint8_t val);
void Gateway_LogFc05StepMappingResult(uint8_t slave_id, uint8_t fc, uint16_t sub_addr);
void Gateway_LogFc05StepNoMapping(uint16_t coil);
void Gateway_LogFc05StepBeforeUart2Tx(void);
void Gateway_LogFc05StepAfterUart2TxComplete(void);
void Gateway_LogFc05StepBeforeUart2RxWait(void);
void Gateway_LogFc05StepUart2RxTimeout(void);
void Gateway_LogFc05StepUart2RxException(uint8_t exc_byte);
void Gateway_LogFc05StepUart2RxOk(void);
void Gateway_LogFc05StepBeforeSendExceptionToPc(uint8_t exc_byte);
void Gateway_LogFc05StepBeforeSendNormalToPc(void);
void Gateway_LogFc05StepCleanupDone(void);
void Gateway_LogFc05StepLocalException04(void);
void Gateway_LogFc05StepSubboardException(const uint8_t *rx_buf, uint16_t len);
#else
static inline void Gateway_LogFc05StepRecvFromPc(void) {}
static inline void Gateway_LogFc05StepRawCoilValue(uint16_t c, uint8_t v) { (void)c; (void)v; }
static inline void Gateway_LogFc05StepMappingResult(uint8_t s, uint8_t f, uint16_t a) { (void)s; (void)f; (void)a; }
static inline void Gateway_LogFc05StepNoMapping(uint16_t c) { (void)c; }
static inline void Gateway_LogFc05StepBeforeUart2Tx(void) {}
static inline void Gateway_LogFc05StepAfterUart2TxComplete(void) {}
static inline void Gateway_LogFc05StepBeforeUart2RxWait(void) {}
static inline void Gateway_LogFc05StepUart2RxTimeout(void) {}
static inline void Gateway_LogFc05StepUart2RxException(uint8_t x) { (void)x; }
static inline void Gateway_LogFc05StepUart2RxOk(void) {}
static inline void Gateway_LogFc05StepBeforeSendExceptionToPc(uint8_t x) { (void)x; }
static inline void Gateway_LogFc05StepBeforeSendNormalToPc(void) {}
static inline void Gateway_LogFc05StepCleanupDone(void) {}
static inline void Gateway_LogFc05StepLocalException04(void) {}
static inline void Gateway_LogFc05StepSubboardException(const uint8_t *b, uint16_t n) { (void)b; (void)n; }
#endif

#if FC05_COIL_DIAG_LOG
void Gateway_LogFc05DiagRecv(uint16_t coil, uint8_t val);
void Gateway_LogFc05DiagRange(uint16_t start, uint16_t end);
void Gateway_LogFc05DiagNoMapping(uint16_t coil);
void Gateway_LogFc05DiagMapped(uint16_t coil, uint8_t slave_id, uint16_t sub_coil);
void Gateway_LogFc05DiagApplyFail(uint16_t coil);
#else
static inline void Gateway_LogFc05DiagRecv(uint16_t coil, uint8_t val) { (void)coil; (void)val; }
static inline void Gateway_LogFc05DiagRange(uint16_t start, uint16_t end) { (void)start; (void)end; }
static inline void Gateway_LogFc05DiagNoMapping(uint16_t coil) { (void)coil; }
static inline void Gateway_LogFc05DiagMapped(uint16_t coil, uint8_t slave_id, uint16_t sub_coil) { (void)coil; (void)slave_id; (void)sub_coil; }
static inline void Gateway_LogFc05DiagApplyFail(uint16_t coil) { (void)coil; }
#endif

#if FC06_DEBUG_LOG
void Gateway_LogFc06Received(uint16_t addr, uint16_t value);
void Gateway_LogFc06MappedLocal(uint16_t addr, uint16_t value);
void Gateway_LogFc06SendingResponseToPc(const uint8_t *frame, uint16_t len);
void Gateway_LogFc06ResponseHex(const uint8_t *frame, uint16_t len);
#else
static inline void Gateway_LogFc06Received(uint16_t addr, uint16_t value) { (void)addr; (void)value; }
static inline void Gateway_LogFc06MappedLocal(uint16_t addr, uint16_t value) { (void)addr; (void)value; }
static inline void Gateway_LogFc06SendingResponseToPc(const uint8_t *frame, uint16_t len) { (void)frame; (void)len; }
static inline void Gateway_LogFc06ResponseHex(const uint8_t *frame, uint16_t len) { (void)frame; (void)len; }
#endif

#if GATEWAY_WRITE_DEBUG_LOG
void Gateway_LogWriteUpstream(uint16_t addr, uint8_t value);
void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value);
/* FC05 path: recv addr, range, mapped slave/coil, or no mapping */
void Gateway_LogFc05RecvAddr(uint16_t coil_addr, uint8_t value);
void Gateway_LogFc05Range(uint16_t start, uint16_t end);
void Gateway_LogFc05Mapped(uint16_t coil_addr, uint8_t slave_id, uint16_t sub_coil);
void Gateway_LogFc05NoMapping(uint16_t coil_addr);
void Gateway_LogFc05ApplyWriteFail(uint16_t coil_addr);
void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value);
void Gateway_LogUart2TxDone(void);
void Gateway_LogUart2DeHigh(void);
void Gateway_LogUart2DeLow(void);
void Gateway_LogUart2RxResult(int ok);
void Gateway_LogUart2TxResult(int ok);
#else
static inline void Gateway_LogWriteUpstream(uint16_t addr, uint8_t value) { (void)addr; (void)value; }
static inline void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value) { (void)slave_id; (void)coil; (void)value; }
static inline void Gateway_LogFc05RecvAddr(uint16_t coil_addr, uint8_t value) { (void)coil_addr; (void)value; }
static inline void Gateway_LogFc05Range(uint16_t start, uint16_t end) { (void)start; (void)end; }
static inline void Gateway_LogFc05Mapped(uint16_t coil_addr, uint8_t slave_id, uint16_t sub_coil) { (void)coil_addr; (void)slave_id; (void)sub_coil; }
static inline void Gateway_LogFc05NoMapping(uint16_t coil_addr) { (void)coil_addr; }
static inline void Gateway_LogFc05ApplyWriteFail(uint16_t coil_addr) { (void)coil_addr; }
static inline void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value) { (void)slave_id; (void)coil; (void)value; }
static inline void Gateway_LogUart2TxDone(void) {}
static inline void Gateway_LogUart2DeHigh(void) {}
static inline void Gateway_LogUart2DeLow(void) {}
static inline void Gateway_LogUart2RxResult(int ok) { (void)ok; }
static inline void Gateway_LogUart2TxResult(int ok) { (void)ok; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_WRITE_LOG_H */
