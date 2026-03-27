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
static volatile uint16_t s_poll_enable_mask;
/* On-demand poll mask: bits = SLAVE_ID_xxx. Non-zero일 때만 downstream polling 실행.
 * PC 요청이 없으면 0으로 유지 → 자동 polling 없음 (요청 기반 통신 정책). */
static volatile uint16_t s_ondemand_poll_mask;
/* 현재 polling 중인 slave ID (on-demand mask 해제용) */
static uint8_t s_current_poll_slave_id;
static uint32_t     response_deadline;
static uint8_t      tx_buf[MODBUS_RTU_TX_BUF_SIZE];
static uint8_t      rx_buf[MODBUS_RTU_RX_BUF_SIZE];
static uint16_t     rx_len;
static uint8_t      last_slave_responded;
static uint8_t      comm_ok[SLAVE_ID_COUNT]; /* 0 = HPSB, 1 = LPSB */
/* While a gateway write (FC05) is in progress, stop poll from consuming UART2 RX bytes. */
static volatile uint8_t s_write_in_progress;
static volatile uint8_t s_uart2_locked_for_txn;
static volatile ModbusMasterFc05Err_t s_last_fc05_err = MODBUS_MASTER_FC05_ERR_NONE;

#define SLAVE_TO_INDEX(s)  ((uint8_t)SLAVE_ID_TO_TABLE_INDEX(s))

static void uart2_mb_log(const char *msg);
static void uart2_mb_log_hex(const char *prefix, const uint8_t *buf, uint16_t len);
static void uart2_mb_lock_rx_it(void);
static void uart2_mb_unlock_rx_it(void);

/* Last sub-poll failure snapshot (for PC diagnostics) */
static volatile uint8_t s_last_sub_fail_slave;
static volatile uint8_t s_last_sub_fail_fc;
static volatile uint16_t s_last_sub_fail_rx_len;
static volatile ModbusSubFailReason_t s_last_sub_fail_reason = MODBUS_SUB_FAIL_NONE;

/* Per-slave sub-poll failure table (for PC diagnostics) */
enum { SUB_FAIL_SID_COUNT = 4u };
static volatile uint8_t s_sub_fail_fc_tbl[SUB_FAIL_SID_COUNT];
static volatile uint16_t s_sub_fail_rx_len_tbl[SUB_FAIL_SID_COUNT];
static volatile ModbusSubFailReason_t s_sub_fail_reason_tbl[SUB_FAIL_SID_COUNT];

static int sub_fail_idx_from_slave(uint8_t sid)
{
    switch (sid) {
    case (uint8_t)SLAVE_ID_HPSB:  return 0;
    case (uint8_t)SLAVE_ID_LPSB1: return 1; /* 실제 슬레이브 ID=2 */
    case (uint8_t)SLAVE_ID_LPSB2: return 2; /* 실제 슬레이브 ID=4 */
    case (uint8_t)SLAVE_ID_LPSB3: return 3; /* 실제 슬레이브 ID=8 */
    default: return -1;
    }
}

static void set_last_sub_fail(uint8_t slave, uint8_t fc, ModbusSubFailReason_t reason, uint16_t len)
{
    s_last_sub_fail_slave = slave;
    s_last_sub_fail_fc = fc;
    s_last_sub_fail_reason = reason;
    s_last_sub_fail_rx_len = len;

    {
        int idx = sub_fail_idx_from_slave(slave);
        if (idx >= 0) {
            s_sub_fail_fc_tbl[idx] = fc;
            s_sub_fail_reason_tbl[idx] = reason;
            s_sub_fail_rx_len_tbl[idx] = len;
        }
    }
#if MODBUS_MASTER_DEBUG_LOG
    {
        char b[128];
        int n = snprintf(b, sizeof(b), "[UART2-MB] subfail slave=%u fc=%02X reason=%u rx_len=%u\r\n",
                         (unsigned)slave, (unsigned)fc, (unsigned)reason, (unsigned)len);
        if (n > 0) uart2_mb_log(b);
    }
#endif
}

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

#define DE_RX_GUARD_MS  1  /* Delay after TX before DE->RX so last byte leaves driver (PB12) */

/* FC05 전송 전, 이전 FC04 poll 응답(예: HPSB 37바이트)이 버스에 아직 남아 있는 경우
 * RS485 버스 충돌 및 응답 오인을 방지하기 위해 버스 무음이 될 때까지 드레인한다.
 * max_ms=50: HPSB FC04 응답 최대 38ms + 여유. silence=4: 3.5 char-time(9600baud≈3.6ms) 초과. */
#define FC05_STALE_RX_DRAIN_MS      50u
#define FC05_STALE_RX_SILENCE_MS     4u

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

static volatile uint32_t s_uart2_ore_count;
static volatile uint32_t s_uart2_ore_count_snapshot;

static void uart2_clear_ore_if_any(void)
{
	/* If Overrun occurs, RX may stall until ORE is cleared. */
	if (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_ORE) != RESET) {
		__HAL_UART_CLEAR_OREFLAG(&MODBUS_UART);
		s_uart2_ore_count++;
	}
	s_uart2_ore_count_snapshot = s_uart2_ore_count;
}

uint32_t ModbusMaster_GetUart2OreCount(void)
{
	return (uint32_t)s_uart2_ore_count;
}

/* ---------- UART2 interrupt-driven RX ring buffer ---------- */
enum { UART2_RB_SIZE = 256u };
static volatile uint8_t s_rb[UART2_RB_SIZE];
static volatile uint16_t s_rb_head;
static volatile uint16_t s_rb_tail;
static uint8_t s_uart2_rx_byte;
static volatile uint8_t s_uart2_rx_it_suspended;

static void rb_clear(void)
{
	s_rb_head = 0u;
	s_rb_tail = 0u;
}

static uint16_t rb_count(void)
{
	uint16_t h = s_rb_head, t = s_rb_tail;
	return (h >= t) ? (uint16_t)(h - t) : (uint16_t)(UART2_RB_SIZE - (t - h));
}

static void rb_push(uint8_t b)
{
	uint16_t next = (uint16_t)((s_rb_head + 1u) % UART2_RB_SIZE);
	if (next == s_rb_tail) {
		/* overflow: drop oldest */
		s_rb_tail = (uint16_t)((s_rb_tail + 1u) % UART2_RB_SIZE);
	}
	s_rb[s_rb_head] = b;
	s_rb_head = next;
}

static int rb_pop(uint8_t *out)
{
	if (s_rb_tail == s_rb_head) return 0;
	*out = s_rb[s_rb_tail];
	s_rb_tail = (uint16_t)((s_rb_tail + 1u) % UART2_RB_SIZE);
	return 1;
}

void ModbusMaster_OnUart2Byte(uint8_t b)
{
	/* FC05 blocking RX path uses HAL_UART_Receive; don't steal bytes while write in progress. */
	if (s_write_in_progress || s_uart2_rx_it_suspended) return;
	rb_push(b);
}

static void uart2_rx_it_start(void)
{
	s_uart2_rx_it_suspended = 0;
	(void)HAL_UART_Receive_IT(&huart2, &s_uart2_rx_byte, 1u);
}

static void uart2_rx_it_suspend(void)
{
	s_uart2_rx_it_suspended = 1;
	(void)HAL_UART_AbortReceive_IT(&huart2);
}

static void uart2_rx_it_resume(void)
{
	if (!s_uart2_rx_it_suspended) return;
	s_uart2_rx_it_suspended = 0;
	(void)HAL_UART_Receive_IT(&huart2, &s_uart2_rx_byte, 1u);
}

/** Flush USART2 RX ring/HW before TX. Never flush after TX. */
static void uart2_flush_rx(void)
{
	rb_clear();
	uart2_clear_ore_if_any();
	__HAL_UART_FLUSH_DRREGISTER(&MODBUS_UART);
}

/* RXNEIE가 suspend된 상태에서 직접 SR/DR 폴링으로 잔여 바이트를 소비한다.
 * HAL_UART_Receive를 쓰지 않아 ORE 상태에서도 안정적으로 동작한다. */
static void uart2_drain_stale_rx_window(uint32_t max_ms)
{
    uint32_t start   = HAL_GetTick();
    uint32_t last_rx = start;
    while ((HAL_GetTick() - start) < max_ms) {
        uint32_t sr = MODBUS_UART.Instance->SR;
        if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
            /* SR을 먼저 읽고 DR을 읽으면 RXNE + ORE 동시 클리어(RM0090 기준) */
            volatile uint8_t b = (uint8_t)MODBUS_UART.Instance->DR;
            (void)b;
            last_rx = HAL_GetTick();
        }
        if ((HAL_GetTick() - last_rx) >= FC05_STALE_RX_SILENCE_MS) {
            break;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart == &huart2) {
		uint8_t b = s_uart2_rx_byte;
		ModbusMaster_OnUart2Byte(b);
		if (!s_uart2_rx_it_suspended) {
			(void)HAL_UART_Receive_IT(&huart2, &s_uart2_rx_byte, 1u);
		}
	}
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
    /* On-demand 정책: pending이 없으면 즉시 반환 (자동 polling 없음). */
    if (s_ondemand_poll_mask == 0u) return;

    PollEntry_t e;
    /* find next enabled entry: s_poll_enable_mask AND s_ondemand_poll_mask 모두 통과해야 함 */
    uint8_t found = 0u;
    for (uint16_t tries = 0u; tries < POLL_TABLE_SIZE; tries++) {
        if (ModbusTable_GetPollEntry(poll_index, &e) != 0) return;
        if (((uint16_t)e.slave_id & s_poll_enable_mask) != 0u &&
            ((uint16_t)e.slave_id & s_ondemand_poll_mask) != 0u)
        {
            found = 1u;
            break;
        }
        poll_index++;
        if (poll_index >= POLL_TABLE_SIZE) poll_index = 0;
    }
    if (!found) return;
    uint8_t target_slave = (uint8_t)e.slave_id;
    s_current_poll_slave_id = target_slave;

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

static uint16_t expected_resp_len_bytes(const PollEntry_t *e)
{
    if (!e) return 0u;
    switch (e->entry_type) {
    case POLL_ENTRY_READ_INPUT_REG:
    case POLL_ENTRY_READ_HOLDING:
        return (uint16_t)(5u + 2u * e->count);
    case POLL_ENTRY_READ_COIL:
    case POLL_ENTRY_READ_DISCRETE: {
        uint16_t bc = (uint16_t)((e->count + 7u) / 8u);
        return (uint16_t)(5u + bc);
    }
    default:
        return 0u;
    }
}

static void advance_poll_index(void)
{
    poll_index++;
    if (poll_index >= POLL_TABLE_SIZE) poll_index = 0;
}

/* poll 1건 완료(성공/실패 무관) 시 on-demand 마스크에서 해당 slave 비트 해제 */
static void ondemand_clear_slave(uint8_t slave_id)
{
    s_ondemand_poll_mask &= ~(uint16_t)slave_id;
}

static void parse_response(void)
{
    PollEntry_t e;
    if (ModbusTable_GetPollEntry(poll_index, &e) != 0) {
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        ondemand_clear_slave(s_current_poll_slave_id);
        advance_poll_index();
        return;
    }
    if (rx_len < 5) {
        PollEntry_t te;
        if (ModbusTable_GetPollEntry(poll_index, &te) == 0) {
            set_last_sub_fail((uint8_t)te.slave_id, expected_fc_for_entry(te.entry_type), MODBUS_SUB_FAIL_RX_TOO_SHORT, rx_len);
        }
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        ondemand_clear_slave(s_current_poll_slave_id);
        advance_poll_index();
        return;
    }

    uint8_t slave = (uint8_t)e.slave_id;
#if MODBUS_MASTER_DEBUG_LOG
    ModbusMaster_LogSubPollRxLen(slave, rx_len);
#endif
    uint8_t recv_fc = rx_buf[1];
    if (rx_buf[0] != slave) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "slave mismatch");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        set_last_sub_fail(slave, recv_fc, MODBUS_SUB_FAIL_SLAVE_MISMATCH, rx_len);
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        ondemand_clear_slave(slave);
        advance_poll_index();
        return;
    }
    uint8_t exp_fc = expected_fc_for_entry(e.entry_type);
    if ((recv_fc & 0x7F) != exp_fc) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "FC mismatch");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        set_last_sub_fail(slave, recv_fc, MODBUS_SUB_FAIL_FC_MISMATCH, rx_len);
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        ondemand_clear_slave(slave);
        advance_poll_index();
        return;
    }
    if (ModbusRTU_CRC16Check(rx_buf, rx_len) != 0) {
#if MODBUS_MASTER_DEBUG_LOG
        ModbusMaster_LogSubPollFail(slave, "CRC fail");
#endif
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        set_last_sub_fail(slave, recv_fc, MODBUS_SUB_FAIL_CRC_FAIL, rx_len);
        {
            uint16_t exp_len = expected_resp_len_bytes(&e);
            char b[180];
            int n = snprintf(b, sizeof(b),
                             "[UART2-MB] crc_fail slave=%u fc=%02X exp_len=%u rx_len=%u rb=%u ore=%lu\r\n",
                             (unsigned)slave,
                             (unsigned)recv_fc,
                             (unsigned)exp_len,
                             (unsigned)rx_len,
                             (unsigned)rb_count(),
                             (unsigned long)s_uart2_ore_count_snapshot);
            if (n > 0) uart2_mb_log(b);
        }
        if (rx_len > 0u) uart2_mb_log_hex("crc_fail rx partial", rx_buf, rx_len);
        uart2_mb_unlock_rx_it();
        state = MST_IDLE;
        ondemand_clear_slave(slave);
        advance_poll_index();
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
            uint16_t regs[MODBUS_INPUT_REG_IMG_MAX];
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
        /* Success: clear per-slave fail table entry */
        {
            int idx = sub_fail_idx_from_slave(slave);
            if (idx >= 0) {
                s_sub_fail_fc_tbl[idx] = recv_fc;
                s_sub_fail_reason_tbl[idx] = MODBUS_SUB_FAIL_NONE;
                s_sub_fail_rx_len_tbl[idx] = rx_len;
            }
        }
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
        comm_ok[SLAVE_TO_INDEX(e.slave_id)] = 0;
        set_last_sub_fail(slave, recv_fc, MODBUS_SUB_FAIL_PARSE_FAIL, rx_len);
    }
    uart2_mb_unlock_rx_it();
    state = MST_IDLE;
    ondemand_clear_slave(slave);
    advance_poll_index();
}

void ModbusMaster_Init(void)
{
    state = MST_IDLE;
    poll_index = 0;
    rx_len = 0;
    last_slave_responded = 0;
    memset(comm_ok, 0, sizeof(comm_ok));
    s_uart2_locked_for_txn = 0;
    set_last_sub_fail(0u, 0u, MODBUS_SUB_FAIL_NONE, 0u);
    ModbusTable_ClearAllImages();
    set_de_rx();
    rb_clear();
    /* On-demand 정책: 초기에는 polling 비활성. PC 요청 시만 활성화. */
    s_ondemand_poll_mask = 0u;
    s_current_poll_slave_id = 0u;
    /* PC Test 모드 여부 무관하게 모든 슬레이브 on-demand 폴 허용.
     * s_ondemand_poll_mask가 0이면 어차피 폴 안 됨. */
    s_poll_enable_mask = (uint16_t)(SLAVE_ID_HPSB | SLAVE_ID_LPSB1 | SLAVE_ID_LPSB2 | SLAVE_ID_LPSB3);
    uart2_rx_it_start();
}

void ModbusMaster_Poll(void)
{
    if (s_write_in_progress) return;
    uart2_clear_ore_if_any();
    /* Consume RX bytes from ring buffer */
    uint8_t byte;
    while (rx_len < MODBUS_RTU_RX_BUF_SIZE && rb_pop(&byte)) {
        rx_buf[rx_len++] = byte;
    }

    switch (state) {
        case MST_IDLE:
            send_request();
            break;

        case MST_WAIT_RESPONSE:
            if (HAL_GetTick() >= response_deadline) {
                PollEntry_t te;
                if (ModbusTable_GetPollEntry(poll_index, &te) == 0) {
                    comm_ok[SLAVE_TO_INDEX(te.slave_id)] = 0;
                    set_last_sub_fail((uint8_t)te.slave_id, expected_fc_for_entry(te.entry_type), MODBUS_SUB_FAIL_TIMEOUT, rx_len);
#if MODBUS_MASTER_DEBUG_LOG
                    ModbusMaster_LogSubPollRxTimeout((uint8_t)te.slave_id);
                    ModbusMaster_LogSubPollFail((uint8_t)te.slave_id, "timeout");
#endif
                }
                if (rx_len > 0u) {
                    uart2_mb_log_hex("timeout rx partial", rx_buf, rx_len);
                }
                {
                    uint16_t exp_len = expected_resp_len_bytes(&te);
                    char b[180];
                    int n = snprintf(b, sizeof(b),
                                     "[UART2-MB] timeout slave=%u fc=%02X exp_len=%u rx_len=%u rb=%u ore=%lu\r\n",
                                     (unsigned)te.slave_id,
                                     (unsigned)expected_fc_for_entry(te.entry_type),
                                     (unsigned)exp_len,
                                     (unsigned)rx_len,
                                     (unsigned)rb_count(),
                                     (unsigned long)s_uart2_ore_count_snapshot);
                    if (n > 0) uart2_mb_log(b);
                }
                uart2_mb_log("[UART2-MB] timeout waiting response\r\n");
                uart2_mb_unlock_rx_it();
                state = MST_IDLE;
                ondemand_clear_slave(s_current_poll_slave_id);
                advance_poll_index();
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
                        set_last_sub_fail((uint8_t)te.slave_id, (uint8_t)(fc & 0x7Fu), MODBUS_SUB_FAIL_EXCEPTION, rx_len);
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
                    ondemand_clear_slave(s_current_poll_slave_id);
                    advance_poll_index();
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
            break;

        default:
            state = MST_IDLE;
            break;
    }
}

void ModbusMaster_GetLastSubFail(uint8_t *slave_id, uint8_t *fc, ModbusSubFailReason_t *reason, uint16_t *out_rx_len)
{
    if (slave_id) *slave_id = (uint8_t)s_last_sub_fail_slave;
    if (fc) *fc = (uint8_t)s_last_sub_fail_fc;
    if (reason) *reason = (ModbusSubFailReason_t)s_last_sub_fail_reason;
    if (out_rx_len) *out_rx_len = (uint16_t)s_last_sub_fail_rx_len;
}

void ModbusMaster_GetSubFailForSlave(SlaveId_t slave, uint8_t *fc, ModbusSubFailReason_t *reason, uint16_t *rx_len)
{
    int idx = sub_fail_idx_from_slave((uint8_t)slave);
    if (idx < 0) {
        if (fc) *fc = 0u;
        if (reason) *reason = MODBUS_SUB_FAIL_NONE;
        if (rx_len) *rx_len = 0u;
        return;
    }
    if (fc) *fc = (uint8_t)s_sub_fail_fc_tbl[idx];
    if (reason) *reason = (ModbusSubFailReason_t)s_sub_fail_reason_tbl[idx];
    if (rx_len) *rx_len = (uint16_t)s_sub_fail_rx_len_tbl[idx];
}

uint8_t ModbusMaster_IsBusy(void)
{
    return (uint8_t)((state != MST_IDLE) || (s_write_in_progress != 0u));
}

void ModbusMaster_SetPollEnableMask(uint16_t slave_id_mask)
{
    uint16_t m = (uint16_t)(slave_id_mask & (uint16_t)(SLAVE_ID_HPSB | SLAVE_ID_LPSB1 | SLAVE_ID_LPSB2 | SLAVE_ID_LPSB3));
    if (m == 0u) m = (uint16_t)SLAVE_ID_HPSB;
    s_poll_enable_mask = m;
}

/**
 * @brief PC 요청에 의해 지정 slave(들)를 1회 polling하도록 요청.
 *        On-demand 정책: PC 명령 없이는 downstream polling을 수행하지 않음.
 * @param slave_mask SLAVE_ID_xxx 비트 OR 조합 (예: SLAVE_ID_HPSB | SLAVE_ID_LPSB1)
 */
void ModbusMaster_RequestOnDemandPoll(uint16_t slave_mask)
{
    uint16_t valid = (uint16_t)(SLAVE_ID_HPSB | SLAVE_ID_LPSB1 | SLAVE_ID_LPSB2 | SLAVE_ID_LPSB3);
    s_ondemand_poll_mask |= (uint16_t)(slave_mask & valid);
}

int ModbusMaster_WriteCoil(SlaveId_t slave, uint16_t coil_addr, uint8_t value)
{
    s_last_fc05_err = MODBUS_MASTER_FC05_ERR_NONE;
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
    /* FC05는 HAL_UART_Receive()로 응답을 블로킹 수신하므로,
     * 링버퍼 RX IT가 바이트를 훔치지 않도록 UART2 RX IT를 잠시 중지한다. */
    uart2_rx_it_suspend();
    /* 링버퍼 + HW RX 클리어 후, 이전 FC04 poll 응답(예: HPSB 37바이트)이
     * 버스에서 전송 완료될 때까지 드레인한다.
     * → RS485 충돌 및 응답 바이트 오인 방지 */
    uart2_flush_rx();
    uart2_drain_stale_rx_window(FC05_STALE_RX_DRAIN_MS);
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
        s_last_fc05_err = MODBUS_MASTER_FC05_ERR_TIMEOUT;
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxTimeout();
#endif
    } else if (rx_buf_fc05[0] != (uint8_t)target_slave) {
        /* 슬레이브 주소 불일치: 다른 장치(예: HPSB FC04 응답)의 프레임 잔류 */
        s_last_fc05_err = MODBUS_MASTER_FC05_ERR_INVALID_RESP;
    } else if (rx_buf_fc05[1] & 0x80) {
        /* 올바른 슬레이브에서 온 exception 응답 */
        s_last_fc05_err = MODBUS_MASTER_FC05_ERR_EXCEPTION;
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxException(rx_buf_fc05[1]);
        Gateway_LogFc05StepSubboardException(rx_buf_fc05, MODBUS_FC05_RESPONSE_LEN);
#endif
    } else {
#if FC05_GW_STEP_LOG
        Gateway_LogFc05StepUart2RxOk();
#endif
    }
    int is_exception = (rx == HAL_OK &&
                        rx_buf_fc05[0] == (uint8_t)target_slave &&
                        (rx_buf_fc05[1] & 0x80u) != 0u);
    int rx_ok = 0;
    if (!is_exception && rx == HAL_OK) {
        rx_ok = (ModbusRTU_ValidateFC05Response(rx_buf_fc05, MODBUS_FC05_RESPONSE_LEN, target_slave) == 0);
        if (!rx_ok) s_last_fc05_err = MODBUS_MASTER_FC05_ERR_INVALID_RESP;
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
    uart2_rx_it_resume();
    uart2_mb_unlock_rx_it();
    s_write_in_progress = 0;
#if FC05_GW_STEP_LOG
    Gateway_LogFc05StepCleanupDone();
#endif
    if (rx_ok) s_last_fc05_err = MODBUS_MASTER_FC05_ERR_NONE;
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

ModbusMasterFc05Err_t ModbusMaster_GetLastFc05Error(void)
{
    return s_last_fc05_err;
}

uint8_t ModbusMaster_GetLastSlaveResponded(void) { return last_slave_responded; }

uint8_t ModbusMaster_IsCommOk(SlaveId_t slave)
{
    if (!IS_VALID_SLAVE_ID(slave)) return 0;
    return comm_ok[SLAVE_TO_INDEX(slave)];
}
