/**
 * @file modbus_master.c
 * @brief MAIN board: Modbus Master - polling table driver, one transaction per Poll().
 *        WriteCoil: TX then wait for FC05 response on lower bus (gateway path debug).
 */
#include "modbus_master.h"
#include "modbus_rtu.h"
#include "modbus_cfg.h"
#include "main.h"
#include "led_status.h"
#include "app_config.h"
#include "gateway_write_log.h"
#include "upstream_pc_protocol.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;  /* Modbus Master = Subboard = USART2 */

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
/* While a gateway write (FC05) is in progress, stop poll from consuming UART2 RX bytes. */
static volatile uint8_t s_write_in_progress;
static volatile uint8_t s_uart2_locked_for_txn;

#define SLAVE_TO_INDEX(s)  ((uint8_t)SLAVE_ID_TO_TABLE_INDEX(s))

static void uart2_mb_log(const char *msg);
static void uart2_mb_log_hex(const char *prefix, const uint8_t *buf, uint16_t len);
static void uart2_mb_lock_rx_it(void);
static void uart2_mb_unlock_rx_it(void);

static void set_de_tx(void)
{
	/* DE는 반드시 TX 직전에 즉시 토글되어야 한다.
	 * uart2_mb_log(huart1 전송)가 블로킹되면 DE가 늦게 HIGH로 올라가 RS485가 송신되지 않을 수 있다. */
	HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET);
	uart2_mb_log("[UART2-MB] DE=TX\r\n");
}

static void set_de_rx(void)
{
	HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET);
	uart2_mb_log("[UART2-MB] DE=RX\r\n");
}

#define DE_RX_GUARD_MS  2  /* Delay after TX before DE->RX so last byte leaves driver (PB12) */

static void uart2_mb_log(const char *msg)
{
#if MODBUS_MASTER_DEBUG_LOG
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)strlen(msg), 100);
#else
    (void)msg;
#endif
}

static void uart2_mb_log_hex(const char *prefix, const uint8_t *buf, uint16_t len)
{
    char line[192];
    int n = snprintf(line, sizeof(line), "[UART2-MB] %s len=%u data=", prefix, (unsigned)len);
    if (n < 0) return;
    for (uint16_t i = 0; i < len && i < 32u && n < (int)(sizeof(line) - 4); i++) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%02X ", buf[i]);
    }
    n += snprintf(line + n, sizeof(line) - (size_t)n, "\r\n");
    if (n > 0) uart2_mb_log(line);
}

static void uart2_mb_lock_rx_it(void)
{
    if (s_uart2_locked_for_txn) return;
    UpstreamPC_PauseUart2RxIT();
    s_uart2_locked_for_txn = 1;
    uart2_mb_log("[UART2-MB] disable rx-it for transaction\r\n");
}

static void uart2_mb_unlock_rx_it(void)
{
    if (!s_uart2_locked_for_txn) return;
    UpstreamPC_ResumeUart2RxIT();
    s_uart2_locked_for_txn = 0;
    uart2_mb_log("[UART2-MB] enable rx-it after transaction\r\n");
}

/** Flush USART2 RX FIFO (call only before TX to avoid discarding slave response). */
static void uart2_flush_rx(void)
{
	uint8_t discard;
	while (HAL_UART_Receive(&MODBUS_UART, &discard, 1, 0) == HAL_OK) { (void)discard; }
}

/**
 * Single USART2 RS485 transmit path: DE HIGH -> optional settle -> TX -> TC wait -> DE LOW.
 * Caller must flush RX before calling (so slave response is not mixed with stale data).
 */
static void uart2_tx(uint8_t *buf, uint16_t len)
{
	set_de_tx();
#if (MODBUS_DE_TX_SETTLE_MS > 0)
	HAL_Delay(MODBUS_DE_TX_SETTLE_MS);
#endif
	HAL_UART_Transmit(&MODBUS_UART, buf, len, 100);
	{
		uint32_t start = HAL_GetTick();
		while (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_TC) == RESET) {
			if ((HAL_GetTick() - start) > MODBUS_FC05_TX_TC_TIMEOUT_MS)
				break;
		}
	}
#if (DE_RX_GUARD_MS > 0)
	HAL_Delay(DE_RX_GUARD_MS);
#endif
	set_de_rx();
}

/**
 * USART2 single transaction: flush RX -> DE HIGH -> TX -> TC wait -> DE LOW -> (caller does RX wait).
 * After use, caller must: flush RX again (cleanup), clear s_write_in_progress.
 */
static void uart2_transaction_tx(uint8_t *tx_buf, uint16_t tx_len)
{
	uart2_flush_rx();
	uart2_tx(tx_buf, tx_len);
}

static void send_request(void)
{
    PollEntry_t e;
    if (ModbusTable_GetPollEntry(poll_index, &e) != 0) return;
    uint8_t target_slave = (uint8_t)e.slave_id;

    size_t pdu_len = 0;
    switch (e.entry_type) {
        case POLL_ENTRY_READ_DISCRETE:
            pdu_len = ModbusRTU_BuildFC02(tx_buf, target_slave, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_COIL:
            pdu_len = ModbusRTU_BuildFC01(tx_buf, target_slave, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_HOLDING:
            pdu_len = ModbusRTU_BuildFC03(tx_buf, target_slave, e.start_addr, e.count);
            break;
        case POLL_ENTRY_READ_INPUT_REG:
            pdu_len = ModbusRTU_BuildFC04(tx_buf, target_slave, e.start_addr, e.count);
            break;
        default:
            state = MST_IDLE;
            return;
    }
    ModbusRTU_AppendCRC(tx_buf, pdu_len);
    uart2_mb_lock_rx_it();
    uart2_mb_log_hex("tx raw", tx_buf, (uint16_t)(pdu_len + 2u));
    uart2_flush_rx();
    uart2_tx(tx_buf, (uint16_t)(pdu_len + 2));
    LED_Status_OnSubRS485Activity();
#if MODBUS_MASTER_DEBUG_LOG
    ModbusMaster_LogSubPollStart((uint8_t)e.slave_id);
    ModbusMaster_LogSubPollTxOk((uint8_t)e.slave_id);
#endif
    rx_len = 0;
    response_deadline = HAL_GetTick() + MODBUS_RESPONSE_TIMEOUT_MS;
    state = MST_WAIT_RESPONSE;
}

static uint8_t expected_fc_for_entry(PollEntryType_t t)
{
    switch (t) {
        case POLL_ENTRY_READ_DISCRETE:  return 0x02;
        case POLL_ENTRY_READ_COIL:      return 0x01;
        case POLL_ENTRY_READ_HOLDING:   return 0x03;
        case POLL_ENTRY_READ_INPUT_REG: return 0x04;
        default: return 0;
    }
}

static void parse_response(void)
{
    PollEntry_t e;
    if (ModbusTable_GetPollEntry(poll_index, &e) != 0) {
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        return;
    }
    if (rx_len < 5) {
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        return;
    }

    uint8_t slave = (uint8_t)e.slave_id;
#if MODBUS_MASTER_DEBUG_LOG
    ModbusMaster_LogSubPollRxLen(slave, rx_len);
#endif
    if (rx_buf[0] != slave) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "slave mismatch");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        return;
    }
    uint8_t exp_fc = expected_fc_for_entry(e.entry_type);
    uint8_t recv_fc = rx_buf[1];
    if ((recv_fc & 0x7F) != exp_fc) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "FC mismatch");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        return;
    }
    if (ModbusRTU_CRC16Check(rx_buf, rx_len) != 0) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "CRC fail");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        return;
    }
    uart2_mb_log_hex("rx raw", rx_buf, rx_len);

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
        LED_Status_OnSubRS485Activity();
        {
            char b[80];
            int n = snprintf(b, sizeof(b), "[UART2-MB] parsed slave=%u fc=%02X\r\n", (unsigned)slave, (unsigned)recv_fc);
            if (n > 0) uart2_mb_log(b);
        }
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollOk(slave);
#endif
    } else {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "parse fail");
#endif
    }
    uart2_mb_unlock_rx_it();
    state = MST_IDLE;
}

void ModbusMaster_Init(void)
{
    state = MST_IDLE;
    poll_index = 0;
    rx_len = 0;
    last_slave_responded = 0;
    memset(comm_ok, 0, sizeof(comm_ok));
    s_uart2_locked_for_txn = 0;
    ModbusTable_ClearAllImages();
    set_de_rx();
}

void ModbusMaster_Poll(void)
{
    if (s_write_in_progress) return;
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
                if (ModbusTable_GetPollEntry(poll_index, &te) == 0) {
                    comm_ok[SLAVE_TO_INDEX(te.slave_id)] = 0;
#if MODBUS_MASTER_DEBUG_LOG
                    ModbusMaster_LogSubPollRxTimeout((uint8_t)te.slave_id);
                    ModbusMaster_LogSubPollFail((uint8_t)te.slave_id, "timeout");
#endif
                }
                uart2_mb_log("[UART2-MB] timeout waiting response\r\n");
                uart2_mb_unlock_rx_it();
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
                    PollEntry_t te;
                    if (ModbusTable_GetPollEntry(poll_index, &te) == 0) {
                        comm_ok[SLAVE_TO_INDEX(te.slave_id)] = 0;
#if MODBUS_MASTER_DEBUG_LOG
                        ModbusMaster_LogSubPollFail((uint8_t)te.slave_id, "exception");
#endif
                    }
                    {
                        char b[96];
                        int n = snprintf(b, sizeof(b), "[UART2-MB] parsed slave=%u exception=%02X\r\n",
                                         (unsigned)rx_buf[0], (unsigned)rx_buf[2]);
                        if (n > 0) uart2_mb_log(b);
                    }
                    uart2_mb_unlock_rx_it();
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
    s_write_in_progress = 1;
    state = MST_IDLE; /* cancel any in-flight poll transaction */
    uint8_t pdu[8];
    uint8_t rx_buf_fc05[MODBUS_FC05_RESPONSE_LEN];
    uint8_t target_slave = (uint8_t)slave;
    uint16_t modbus_value = (value == 1u) ? 0xFF00u : 0x0000u;
    size_t len = ModbusRTU_BuildFC05(pdu, target_slave, coil_addr, (value == 1u) ? 1u : 0u);
    /* FC05 wire value를 명시적으로 강제: ON=0xFF00, OFF=0x0000 */
    pdu[4] = (uint8_t)(modbus_value >> 8);
    pdu[5] = (uint8_t)(modbus_value & 0xFF);
    ModbusRTU_AppendCRC(pdu, len);
    uart2_mb_lock_rx_it();
    {
        uint16_t coil_value_raw = (uint16_t)((pdu[4] << 8) | pdu[5]);
        char b[128];
        int n = snprintf(b, sizeof(b),
                         "[MB->HPSB] tx slave=%u fc=05 coil=%u value_raw=0x%04X\r\n",
                         (unsigned)target_slave, (unsigned)coil_addr, (unsigned)coil_value_raw);
        if (n > 0) uart2_mb_log(b);
    }
    uart2_mb_log_hex("MB->HPSB tx raw", pdu, (uint16_t)(len + 2u));
    uart2_mb_log_hex("tx raw", pdu, (uint16_t)(len + 2u));

#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepBeforeUart2Tx();
#endif
    Gateway_LogUart2DeHigh();
    uart2_transaction_tx(pdu, (uint16_t)(len + 2));
#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepAfterUart2TxComplete();
#endif
    Gateway_LogUart2TxDone();
    Gateway_LogUart2DeLow();
    LED_Status_OnSubRS485Activity();

#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepBeforeUart2RxWait();
#endif
    /* FC05 normal response: 8 bytes
     *   [slave][fc=05][addr_hi][addr_lo][val_hi][val_lo][crc_lo][crc_hi]
     * FC05 exception response: 5 bytes
     *   [slave][fc=85][ex_code][crc_lo][crc_hi]
     *
     * HPSB sends exception as 5 bytes. 따라서 무조건 8바이트를 기다리면 예외에서도 timeout이 발생할 수 있다. */
    HAL_StatusTypeDef rx = HAL_ERROR;
    uint32_t deadline = HAL_GetTick() + MODBUS_FC05_RX_TIMEOUT_MS;
    uint16_t rx_expected_len = MODBUS_FC05_RESPONSE_LEN; /* optimistic */

    /* 1) 먼저 앞 3바이트만 읽는다(슬레이브/FC/값또는예외코드). */
    {
        uint32_t rem_ms = 0;
        uint32_t now = HAL_GetTick();
        rem_ms = (deadline > now) ? (deadline - now) : 0u;
        rx = HAL_UART_Receive(&MODBUS_UART, rx_buf_fc05, 3u, (uint32_t)rem_ms);
    }
    if (rx == HAL_OK) {
        /* 2) exception 여부(fc 상위비트 1)로 전체 길이 결정 */
        if (rx_buf_fc05[1] & 0x80u) {
            rx_expected_len = 5u;
        } else {
            rx_expected_len = MODBUS_FC05_RESPONSE_LEN;
        }
        uint16_t rem_len = (uint16_t)(rx_expected_len - 3u);
        if (rem_len > 0u) {
            uint32_t now = HAL_GetTick();
            uint32_t rem_ms = (deadline > now) ? (deadline - now) : 0u;
            rx = HAL_UART_Receive(&MODBUS_UART, &rx_buf_fc05[3u], rem_len, rem_ms);
        }
    }

    if (rx == HAL_OK)
    {
        uart2_mb_log_hex("MB->HPSB rx raw", rx_buf_fc05, rx_expected_len);
        uart2_mb_log_hex("rx raw", rx_buf_fc05, rx_expected_len);
    }
    else
        uart2_mb_log("[UART2-MB] timeout waiting response\r\n");

    if (rx != HAL_OK) {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxTimeout();
#endif
    } else if (rx_buf_fc05[1] & 0x80) {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxException(rx_buf_fc05[1]);
        Gateway_LogFc05StepSubboardException(rx_buf_fc05, MODBUS_FC05_RESPONSE_LEN);
#endif
    } else {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxOk();
#endif
    }
    int is_exception = (rx == HAL_OK && (rx_buf_fc05[1] & 0x80u) != 0u);
    int rx_ok = 0;
    if (!is_exception && rx == HAL_OK) {
        rx_ok = (ModbusRTU_ValidateFC05Response(rx_buf_fc05, MODBUS_FC05_RESPONSE_LEN, target_slave) == 0);
    }
    if (rx_ok) {
        char b[96];
        int n = snprintf(b, sizeof(b), "[UART2-MB] parsed slave=%u fc=05\r\n", (unsigned)target_slave);
        if (n > 0) uart2_mb_log(b);
    } else if (rx == HAL_OK && (rx_buf_fc05[1] & 0x80u)) {
        char b[96];
        int n = snprintf(b, sizeof(b), "[UART2-MB] parsed slave=%u exception=%02X\r\n",
                         (unsigned)target_slave, (unsigned)rx_buf_fc05[2]);
        if (n > 0) uart2_mb_log(b);
    }
    Gateway_LogUart2RxResult(rx_ok);
    Gateway_LogUart2TxResult(rx_ok);

    /* Cleanup: flush USART2 RX so poll does not see partial/leftover bytes; always clear busy. */
    uart2_flush_rx();
    uart2_mb_unlock_rx_it();
    s_write_in_progress = 0;
#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepCleanupDone();
#endif
    return rx_ok ? 0 : -1;
}

int ModbusMaster_WriteHoldingReg(SlaveId_t slave, uint16_t reg_addr, uint16_t value)
{
    uint8_t pdu[10];
    size_t len = ModbusRTU_BuildFC06(pdu, (uint8_t)slave, reg_addr, value);
    ModbusRTU_AppendCRC(pdu, len);
    uart2_flush_rx();
    uart2_tx(pdu, (uint16_t)(len + 2));
    LED_Status_OnSubRS485Activity();
    return 0;
}

uint8_t ModbusMaster_GetLastSlaveResponded(void) { return last_slave_responded; }

uint8_t ModbusMaster_IsCommOk(SlaveId_t slave)
{
    if (!IS_VALID_SLAVE_ID(slave)) return 0;
    return comm_ok[SLAVE_TO_INDEX(slave)];
}
