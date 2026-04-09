/**
 * @file modbus_master.h
 * @brief MAIN board: Modbus Master transaction layer (one transaction per Poll).
 */
#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include "io_map.h"
#include "modbus_table.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ModbusMaster_Init(void);
void ModbusMaster_Poll(void);
void ModbusMaster_OnUart2Byte(uint8_t b);
uint8_t ModbusMaster_IsBusy(void);

typedef enum {
    MODBUS_MASTER_FC05_ERR_NONE = 0,
    MODBUS_MASTER_FC05_ERR_TIMEOUT = 1,
    MODBUS_MASTER_FC05_ERR_EXCEPTION = 2,
    MODBUS_MASTER_FC05_ERR_INVALID_RESP = 3
} ModbusMasterFc05Err_t;

/* Optional: request write (queued or immediate). Use enum addresses. */
int ModbusMaster_WriteCoil(SlaveId_t slave, uint16_t coil_addr, uint8_t value);
int ModbusMaster_WriteHoldingReg(SlaveId_t slave, uint16_t reg_addr, uint16_t value);
ModbusMasterFc05Err_t ModbusMaster_GetLastFc05Error(void);

/* Communication status for application */
uint8_t ModbusMaster_GetLastSlaveResponded(void);
uint8_t ModbusMaster_IsCommOk(SlaveId_t slave);

/* Last sub-board poll failure (for PC diagnostics) */
typedef enum {
    MODBUS_SUB_FAIL_NONE = 0,
    MODBUS_SUB_FAIL_TIMEOUT = 1,
    MODBUS_SUB_FAIL_EXCEPTION = 2,
    MODBUS_SUB_FAIL_RX_TOO_SHORT = 3,
    MODBUS_SUB_FAIL_SLAVE_MISMATCH = 4,
    MODBUS_SUB_FAIL_FC_MISMATCH = 5,
    MODBUS_SUB_FAIL_CRC_FAIL = 6,
    MODBUS_SUB_FAIL_PARSE_FAIL = 7
} ModbusSubFailReason_t;

void ModbusMaster_GetLastSubFail(uint8_t *slave_id, uint8_t *fc, ModbusSubFailReason_t *reason, uint16_t *rx_len);
void ModbusMaster_GetSubFailForSlave(SlaveId_t slave, uint8_t *fc, ModbusSubFailReason_t *reason, uint16_t *rx_len);
uint32_t ModbusMaster_GetUart2OreCount(void);

/* Optional: enable/disable downstream poll per slave (bitmask of slave IDs: 1/2/4/8) */
void ModbusMaster_SetPollEnableMask(uint16_t slave_id_mask);

/* On-demand poll: PC 요청 시 지정 slave(들)를 1회 polling 요청 (기본 IDLE, 요청 기반 통신 정책) */
void ModbusMaster_RequestOnDemandPoll(uint16_t slave_mask);

/* Optional debug log (UART2 하위 폴링): MODBUS_MASTER_DEBUG_LOG=1 시 UART1 출력. PC 통신과 분리해 별도 시리얼로 확인 권장. */
void ModbusMaster_LogSubPollStart(uint8_t slave_id);
void ModbusMaster_LogSubPollTxOk(uint8_t slave_id);
void ModbusMaster_LogSubPollRxTimeout(uint8_t slave_id);
void ModbusMaster_LogSubPollRxLen(uint8_t slave_id, uint16_t len);
void ModbusMaster_LogSubPollOk(uint8_t slave_id);
void ModbusMaster_LogSubPollFail(uint8_t slave_id, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_H */
