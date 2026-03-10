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
    int n = snprintf(buf, sizeof(buf), "[GW] upstream command from PC: FC05 addr=%u val=%u\r\n", (unsigned)addr, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogWriteMapped(uint8_t slave_id, uint16_t coil, uint8_t value)
{
    const char *board = gw_slave_board_name(slave_id);
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] target board=%s slave_id=%u channel=%u val=%u (FC05)\r\n", board, (unsigned)slave_id, (unsigned)coil, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogUart2TxStart(uint8_t slave_id, uint16_t coil, uint8_t value)
{
    const char *board = gw_slave_board_name(slave_id);
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "[GW] UART2 TX FC05 slave=%u (%s) coil=%u val=%u\r\n", (unsigned)slave_id, board, (unsigned)coil, (unsigned)value);
    if (n > 0) gw_uart_send(buf);
}

void Gateway_LogUart2TxResult(int ok)
{
    gw_uart_send(ok ? "[GW] subboard response OK\r\n" : "[GW] subboard response FAIL\r\n");
}

#endif /* GATEWAY_WRITE_DEBUG_LOG */
