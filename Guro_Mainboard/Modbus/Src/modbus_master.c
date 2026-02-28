/**
 * @file modbus_master.c
 * @brief MAIN board: Modbus Master - polling table driver, one transaction per Poll().
 */
#include "modbus_master.h"
#include "modbus_rtu.h"
#include "modbus_cfg.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

typedef enum {
    MST_IDLE,
    MST_SEND_REQUEST,
    MST_WAIT_RESPONSE,
    MST_PARSE_RESPONSE
} MasterState_t;

static MasterState_t state = MST_IDLE;
static uint8_t      poll_index;
static uint32_t     response_deadline;
static uint8_t      tx_buf[MODBUS_RTU_TX_BUF_SIZE];
static uint8_t      rx_buf[MODBUS_RTU_RX_BUF_SIZE];
static uint16_t     rx_len;
static uint8_t      last_slave_responded;
static uint8_t      comm_ok[SLAVE_ID_COUNT]; /* 0 = HPSB, 1 = LPSB */

#define SLAVE_TO_INDEX(s)  ((uint8_t)((s) - SLAVE_ID_FIRST))

static void set_de_tx(void)   { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET); }
static void set_de_rx(void)   { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET); }

static void send_request(void)
{
    PollEntry_t e;
    if (ModbusTable_GetPollEntry(poll_index, &e) != 0) return;

    size_t pdu_len = 0;
    switch (e.entry_type) {
        case POLL_ENTRY_READ_DISCRETE:
            pdu_len = ModbusRTU_BuildFC02(tx_buf, (uint8_t)e.slave_id, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_COIL:
            pdu_len = ModbusRTU_BuildFC01(tx_buf, (uint8_t)e.slave_id, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_HOLDING:
            pdu_len = ModbusRTU_BuildFC03(tx_buf, (uint8_t)e.slave_id, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_INPUT_REG:
            pdu_len = ModbusRTU_BuildFC04(tx_buf, (uint8_t)e.slave_id, e.start_addr, e.count);
            break;
        default:
            state = MST_IDLE;
            return;
    }
    ModbusRTU_AppendCRC(tx_buf, pdu_len);
    set_de_tx();
    HAL_UART_Transmit(&MODBUS_UART, tx_buf, (uint16_t)(pdu_len + 2), 100);
    set_de_rx();
    rx_len = 0;
    response_deadline = HAL_GetTick() + MODBUS_RESPONSE_TIMEOUT_MS;
    state = MST_WAIT_RESPONSE;
}

static void parse_response(void)
{
    PollEntry_t e;
    if (ModbusTable_GetPollEntry(poll_index, &e) != 0) {
        state = MST_IDLE;
        return;
    }
    if (rx_len < 5) {
        state = MST_IDLE;
        return;
    }

    uint8_t slave = (uint8_t)e.slave_id;
    int ok = 0;
    switch (e.entry_type) {
        case POLL_ENTRY_READ_DISCRETE: {
            uint8_t bits[MODBUS_DISCRETE_COUNT];
            ok = ModbusRTU_ParseFC02Response(rx_buf, rx_len, bits, e.count);
            if (ok == 0) ModbusTable_SetDiscreteBytes(e.slave_id, &rx_buf[3], e.count);
            break;
        }
        case POLL_ENTRY_READ_COIL: {
            uint8_t bits[MODBUS_COIL_COUNT];
            ok = ModbusRTU_ParseFC01Response(rx_buf, rx_len, bits, e.count);
            if (ok == 0) ModbusTable_SetCoilBytes(e.slave_id, &rx_buf[3], e.count);
            break;
        }
        case POLL_ENTRY_READ_HOLDING: {
            uint16_t regs[MODBUS_HOLDING_COUNT];
            ok = ModbusRTU_ParseFC03Response(rx_buf, rx_len, regs, e.count);
            if (ok == 0) ModbusTable_SetHoldingRegs(e.slave_id, e.start_addr, regs, e.count);
            break;
        }
        case POLL_ENTRY_READ_INPUT_REG: {
            uint16_t regs[MODBUS_INPUT_REG_COUNT];
            ok = ModbusRTU_ParseFC04Response(rx_buf, rx_len, regs, e.count);
            if (ok == 0) ModbusTable_SetInputRegs(e.slave_id, e.start_addr, regs, e.count);
            break;
        }
        default:
            break;
    }
    if (ok == 0) {
        last_slave_responded = slave;
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 1;
    }
    state = MST_IDLE;
}

void ModbusMaster_Init(void)
{
    state = MST_IDLE;
    poll_index = 0;
    rx_len = 0;
    last_slave_responded = 0;
    memset(comm_ok, 0, sizeof(comm_ok));
    ModbusTable_ClearAllImages();
    set_de_rx();
}

void ModbusMaster_Poll(void)
{
    /* Consume RX bytes if any */
    uint8_t byte;
    while (HAL_UART_Receive(&MODBUS_UART, &byte, 1, 0) == HAL_OK) {
        if (rx_len < MODBUS_RTU_RX_BUF_SIZE)
            rx_buf[rx_len++] = byte;
    }

    switch (state) {
        case MST_IDLE:
            poll_index = 0;
            send_request();
            break;

        case MST_WAIT_RESPONSE:
            if (HAL_GetTick() >= response_deadline) {
                PollEntry_t te;
                if (ModbusTable_GetPollEntry(poll_index, &te) == 0)
                    comm_ok[SLAVE_TO_INDEX(te.slave_id)] = 0;
                state = MST_IDLE;
                poll_index++;
                if (poll_index >= POLL_TABLE_SIZE)
                    poll_index = 0;
                send_request();
                return;
            }
            if (rx_len >= 5) {
                uint8_t exp_slave = rx_buf[0];
                (void)exp_slave;
                uint8_t fc = rx_buf[1];
                if (fc & 0x80) {
                    state = MST_IDLE;
                    poll_index++;
                    if (poll_index >= POLL_TABLE_SIZE) poll_index = 0;
                    send_request();
                    return;
                }
                if (fc == 0x01 || fc == 0x02) {
                    if (rx_len >= (uint16_t)(3 + rx_buf[2] + 2))
                        state = MST_PARSE_RESPONSE;
                } else if (fc == 0x03 || fc == 0x04) {
                    if (rx_len >= (uint16_t)(3 + rx_buf[2] + 2))
                        state = MST_PARSE_RESPONSE;
                }
            }
            break;

        case MST_PARSE_RESPONSE:
            parse_response();
            poll_index++;
            if (poll_index >= POLL_TABLE_SIZE) poll_index = 0;
            send_request();
            break;

        default:
            state = MST_IDLE;
            break;
    }
}

int ModbusMaster_WriteCoil(SlaveId_t slave, uint16_t coil_addr, uint8_t value)
{
    uint8_t pdu[8];
    size_t len = ModbusRTU_BuildFC05(pdu, (uint8_t)slave, coil_addr, value);
    ModbusRTU_AppendCRC(pdu, len);
    set_de_tx();
    HAL_StatusTypeDef s = HAL_UART_Transmit(&MODBUS_UART, pdu, (uint16_t)(len + 2), 100);
    set_de_rx();
    return (s == HAL_OK) ? 0 : -1;
}

int ModbusMaster_WriteHoldingReg(SlaveId_t slave, uint16_t reg_addr, uint16_t value)
{
    uint8_t pdu[10];
    size_t len = ModbusRTU_BuildFC06(pdu, (uint8_t)slave, reg_addr, value);
    ModbusRTU_AppendCRC(pdu, len);
    set_de_tx();
    HAL_StatusTypeDef s = HAL_UART_Transmit(&MODBUS_UART, pdu, (uint16_t)(len + 2), 100);
    set_de_rx();
    return (s == HAL_OK) ? 0 : -1;
}

uint8_t ModbusMaster_GetLastSlaveResponded(void) { return last_slave_responded; }

uint8_t ModbusMaster_IsCommOk(SlaveId_t slave)
{
    if (slave < SLAVE_ID_FIRST || slave > SLAVE_ID_LAST) return 0;
    return comm_ok[SLAVE_TO_INDEX(slave)];
}
