/**
 * @file modbus_master_log.c
 * @brief UART2 하위 폴링(HPSB/LPSB) 디버그 로그: MODBUS_MASTER_DEBUG_LOG=1 시 UART1로 출력.
 *        Gateway write (PC FC05 898..909 → UART2): GATEWAY_WRITE_DEBUG_LOG=1 시 UART1로 출력.
 */
#include "app_config.h"
#include "modbus_master.h"
#include "modbus_table.h"
#include "io_map.h"
#include "main.h"
#include "gateway_write_log.h"
#include <stdio.h>
#include <string.h>

#if MODBUS_MASTER_DEBUG_LOG

static const char *slave_name(uint8_t slave_id)
{
    switch (slave_id) {
        case 1: return "HPSB";
        case 2: return "LPSB1";
        case 4: return "LPSB2";
        case 8: return "LPSB3";
        default: return "?";
    }
}

static void uart_send(const char *msg)
{
    size_t len = strlen(msg);
    if (len > 0)
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

void ModbusMaster_LogSubPollStart(uint8_t slave_id)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "SUB POLL start slave=%u (%s)\r\n", (unsigned)slave_id, slave_name(slave_id));
    if (n > 0) uart_send(buf);
}

void ModbusMaster_LogSubPollTxOk(uint8_t slave_id)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "SUB slave=%u (%s) TX OK\r\n", (unsigned)slave_id, slave_name(slave_id));
    if (n > 0) uart_send(buf);
}

void ModbusMaster_LogSubPollRxTimeout(uint8_t slave_id)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "SUB slave=%u (%s) RX timeout\r\n", (unsigned)slave_id, slave_name(slave_id));
    if (n > 0) uart_send(buf);
}

void ModbusMaster_LogSubPollRxLen(uint8_t slave_id, uint16_t len)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "SUB slave=%u (%s) RX len=%u\r\n", (unsigned)slave_id, slave_name(slave_id), (unsigned)len);
    if (n > 0) uart_send(buf);
}

void ModbusMaster_LogSubPollOk(uint8_t slave_id)
{
    SlaveId_t s = (SlaveId_t)slave_id;
    if (!IS_VALID_SLAVE_ID(s)) return;

    uint8_t coils = 0;
    uint8_t discrete = 0;
    for (int i = 0; i < 8; i++) {
        if (ModbusTable_GetCoil(s, (uint16_t)i)) coils |= (1u << i);
        if (ModbusTable_GetDiscrete(s, (uint16_t)i)) discrete |= (1u << i);
    }
    uint16_t r1 = ModbusTable_GetInputReg(s, 1);
    uint16_t r2 = ModbusTable_GetInputReg(s, 2);
    uint16_t r3 = ModbusTable_GetInputReg(s, 3);

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "SUB slave=%u OK coils=0x%02X dis=0x%02X raw=[%u,%u,%u]\r\n",
                    (unsigned)slave_id, coils, discrete, (unsigned)r1, (unsigned)r2, (unsigned)r3);
    if (n > 0) uart_send(buf);
}

void ModbusMaster_LogSubPollFail(uint8_t slave_id, const char *reason)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "SUB slave=%u fail: %s\r\n", (unsigned)slave_id, reason ? reason : "?");
    if (n > 0) uart_send(buf);
}

#else

void ModbusMaster_LogSubPollStart(uint8_t slave_id) { (void)slave_id; }
void ModbusMaster_LogSubPollTxOk(uint8_t slave_id) { (void)slave_id; }
void ModbusMaster_LogSubPollRxTimeout(uint8_t slave_id) { (void)slave_id; }
void ModbusMaster_LogSubPollRxLen(uint8_t slave_id, uint16_t len) { (void)slave_id; (void)len; }
void ModbusMaster_LogSubPollOk(uint8_t slave_id) { (void)slave_id; }
void ModbusMaster_LogSubPollFail(uint8_t slave_id, const char *reason) { (void)slave_id; (void)reason; }

#endif /* MODBUS_MASTER_DEBUG_LOG */

#if GATEWAY_WRITE_DEBUG_LOG

extern UART_HandleTypeDef huart1;
static void gw_uart_send(const char *msg)
{
    size_t len = strlen(msg);
    if (len > 0)
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

static const char *gw_slave_board_name(uint8_t slave_id)
{
    switch (slave_id) {
        case 1: return "HPSB";
        case 2: return "LPSB1";
        case 4: return "LPSB2";
        case 8: return "LPSB3";
        default: return "?";
    }
}

void Gateway_LogWriteUpstream(uint16_t addr, uint8_t value)
{
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] command parsed: FC05 addr=%u val=%u\r\n", (unsigned)addr, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value)
{
    const char *board = gw_slave_board_name(slave_id);
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] resolved target board=%s slave_id=%u FC=05 coil=%u val=%u\r\n", board, (unsigned)slave_id, (unsigned)coil, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value)
{
    const char *board = gw_slave_board_name(slave_id);
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] UART2 TX start: FC05 slave=%u (%s) coil=%u val=%u\r\n", (unsigned)slave_id, board, (unsigned)coil, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogUart2TxDone(void)
{
    gw_uart_send("[GW] USART2 TX done\r\n");
}

void Gateway_LogUart2DeHigh(void)
{
	gw_uart_send("[GW] USART2 DE=HIGH\r\n");
}

void Gateway_LogUart2DeLow(void)
{
	gw_uart_send("[GW] USART2 DE=LOW\r\n");
}

void Gateway_LogUart2RxResult(int ok)
{
    gw_uart_send(ok ? "[GW] UART2 RX OK\r\n" : "[GW] UART2 RX timeout or invalid\r\n");
}

void Gateway_LogUart2TxResult(int ok)
{
    gw_uart_send(ok ? "[GW] final gateway result OK\r\n" : "[GW] final gateway result FAIL\r\n");
}

void Gateway_LogFc05RetryTry1Fail(uint8_t slave_id, uint16_t coil, const char *reason)
{
    char buf[112];
    int n = snprintf(buf, sizeof(buf), "[MB] FC05 try1 fail (slave=%u coil=%u reason=%s)\r\n",
                     (unsigned)slave_id, (unsigned)coil, reason ? reason : "?");
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogFc05RetryStart(void)
{
    gw_uart_send("[MB] FC05 retry...\r\n");
}

void Gateway_LogFc05RetryTry2Result(int ok)
{
    gw_uart_send(ok ? "[MB] FC05 try2 success\r\n" : "[MB] FC05 try2 fail\r\n");
}

void Gateway_LogFc05RecvAddr(uint16_t coil_addr, uint8_t value)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] FC05 recv addr=%u value=%u\r\n", (unsigned)coil_addr, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogFc05Range(uint16_t start, uint16_t end)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] checking range %u~%u\r\n", (unsigned)start, (unsigned)end);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogFc05Mapped(uint16_t coil_addr, uint8_t slave_id, uint16_t sub_coil)
{
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] mapped addr=%u -> slave=%u coil=%u\r\n", (unsigned)coil_addr, (unsigned)slave_id, (unsigned)sub_coil);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogFc05NoMapping(uint16_t coil_addr)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] no gateway mapping for coil %u\r\n", (unsigned)coil_addr);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogFc05ApplyWriteFail(uint16_t coil_addr)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] ApplyWrite failed for coil %u\r\n", (unsigned)coil_addr);
    if (n > 0) gw_uart_send(buf);
}

#endif /* GATEWAY_WRITE_DEBUG_LOG */

#if FC05_GW_STEP_LOG

extern UART_HandleTypeDef huart1;
static void gw_step_send(const char *msg)
{
    size_t len = strlen(msg);
    if (len > 0)
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

void Gateway_LogFc05StepRecvFromPc(void)
{
    gw_step_send("[GW] FC05 recv from PC\r\n");
}

void Gateway_LogFc05StepRawCoilValue(uint16_t coil, uint8_t val)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[GW] raw coil addr=%u value=%u\r\n", (unsigned)coil, (unsigned)val);
    if (n > 0) gw_step_send(buf);
}

void Gateway_LogFc05StepMappingResult(uint8_t slave_id, uint8_t fc, uint16_t sub_addr)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] mapping result: target slave=%u fc=%u sub_addr=%u\r\n", (unsigned)slave_id, (unsigned)fc, (unsigned)sub_addr);
    if (n > 0) gw_step_send(buf);
}

void Gateway_LogFc05StepNoMapping(uint16_t coil)
{
    char buf[56];
    int n = snprintf(buf, sizeof(buf), "[GW] no mapping for coil=%u\r\n", (unsigned)coil);
    if (n > 0) gw_step_send(buf);
}

void Gateway_LogFc05StepBeforeUart2Tx(void)
{
    gw_step_send("[GW] before USART2 tx\r\n");
}

void Gateway_LogFc05StepAfterUart2TxComplete(void)
{
    gw_step_send("[GW] after USART2 tx complete\r\n");
}

void Gateway_LogFc05StepBeforeUart2RxWait(void)
{
    gw_step_send("[GW] before USART2 rx wait\r\n");
}

void Gateway_LogFc05StepUart2RxTimeout(void)
{
    gw_step_send("[GW] USART2 rx timeout\r\n");
}

void Gateway_LogFc05StepUart2RxException(uint8_t exc_byte)
{
    char buf[56];
    int n = snprintf(buf, sizeof(buf), "[GW] USART2 rx exception byte=0x%02X\r\n", (unsigned)exc_byte);
    if (n > 0) gw_step_send(buf);
}

void Gateway_LogFc05StepUart2RxOk(void)
{
    gw_step_send("[GW] USART2 rx ok\r\n");
}

void Gateway_LogFc05StepBeforeSendExceptionToPc(uint8_t exc_byte)
{
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[GW] before sending exception to PC exc=0x%02X\r\n", (unsigned)exc_byte);
    if (n > 0) gw_step_send(buf);
}

void Gateway_LogFc05StepBeforeSendNormalToPc(void)
{
    gw_step_send("[GW] before sending normal response to PC\r\n");
}

void Gateway_LogFc05StepCleanupDone(void)
{
    gw_step_send("[GW] cleanup done / busy flag clear\r\n");
}

void Gateway_LogFc05StepLocalException04(void)
{
    gw_step_send("[GW] local exception 0x04\r\n");
}

void Gateway_LogFc05StepSubboardException(const uint8_t *rx_buf, uint16_t len)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "[GW] subboard returned exception ");
    if (n <= 0) return;
    size_t off = (size_t)n;
    uint16_t max_i = (len > 8u) ? 8u : len;
    for (uint16_t i = 0; i < max_i && off < sizeof(buf) - 4; i++)
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%02X ", rx_buf[i]);
    snprintf(buf + off, sizeof(buf) - off, "\r\n");
    gw_step_send(buf);
}

#endif /* FC05_GW_STEP_LOG */

#if FC05_COIL_DIAG_LOG

extern UART_HandleTypeDef huart1;
static void fc05_diag_send(const char *msg)
{
    size_t len = strlen(msg);
    if (len > 0)
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

void Gateway_LogFc05DiagRecv(uint16_t coil, uint8_t val)
{
    char buf[72];
    int n = snprintf(buf, sizeof(buf), "[GW] FC05 recv coil=%u val=%u\r\n", (unsigned)coil, (unsigned)val);
    if (n > 0) fc05_diag_send(buf);
}

void Gateway_LogFc05DiagRange(uint16_t start, uint16_t end)
{
    char buf[72];
    int n = snprintf(buf, sizeof(buf), "[GW] try range %u~%u\r\n", (unsigned)start, (unsigned)end);
    if (n > 0) fc05_diag_send(buf);
}

void Gateway_LogFc05DiagNoMapping(uint16_t coil)
{
    char buf[72];
    int n = snprintf(buf, sizeof(buf), "[GW] no mapping for coil=%u\r\n", (unsigned)coil);
    if (n > 0) fc05_diag_send(buf);
}

void Gateway_LogFc05DiagMapped(uint16_t coil, uint8_t slave_id, uint16_t sub_coil)
{
    char buf[88];
    int n = snprintf(buf, sizeof(buf), "[GW] mapped coil=%u -> slave=%u fc=05 sub_coil=%u\r\n", (unsigned)coil, (unsigned)slave_id, (unsigned)sub_coil);
    if (n > 0) fc05_diag_send(buf);
}

void Gateway_LogFc05DiagApplyFail(uint16_t coil)
{
    char buf[72];
    int n = snprintf(buf, sizeof(buf), "[GW] ApplyWrite failed coil=%u\r\n", (unsigned)coil);
    if (n > 0) fc05_diag_send(buf);
}

#endif /* FC05_COIL_DIAG_LOG */

#if FC06_DEBUG_LOG

#include <stdio.h>
extern UART_HandleTypeDef huart1;
static void fc06_uart_send(const char *msg)
{
    size_t len = strlen(msg);
    if (len > 0)
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

void Gateway_LogFc06Received(uint16_t addr, uint16_t value)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] FC06 received from PC addr=%u val=%u\r\n", (unsigned)addr, (unsigned)value);
    if (n > 0) fc06_uart_send(buf);
}

void Gateway_LogFc06MappedLocal(uint16_t addr, uint16_t value)
{
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "[GW] FC06 mapped to local (no sub) addr=%u value=%u\r\n", (unsigned)addr, (unsigned)value);
    if (n > 0) fc06_uart_send(buf);
}

void Gateway_LogFc06SendingResponseToPc(const uint8_t *frame, uint16_t len)
{
    (void)frame;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[GW] sending response to PC len=%u\r\n", (unsigned)len);
    if (n > 0) fc06_uart_send(buf);
}

void Gateway_LogFc06ResponseHex(const uint8_t *frame, uint16_t len)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "[GW] response HEX: ");
    if (n <= 0) return;
    size_t off = (size_t)n;
    uint16_t max_i = (len > 24u) ? 24u : len;
    for (uint16_t i = 0; i < max_i && off < sizeof(buf) - 4; i++)
        off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%02X ", frame[i]);
    snprintf(buf + off, sizeof(buf) - off, "\r\n");
    fc06_uart_send(buf);
}

#endif /* FC06_DEBUG_LOG */
