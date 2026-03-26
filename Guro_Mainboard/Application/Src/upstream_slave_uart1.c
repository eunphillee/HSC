/**
 * @file upstream_slave_uart1.c
 * @brief Modbus RTU Slave on USART1 (PA9/PA10), DE/RE=PB1. ReceiveToIdle_IT, 4ms frame end, FC01/02/03/04/05/06/15/16.
 *        RS485 polarity: RS485_DE_ACTIVE_HIGH (app_config.h) 1=Mode A, 0=Mode B.
 */
#include "upstream_slave_uart1.h"
#include "app_config.h"
#include "main.h"
#include "led_status.h"
#include "modbus_rtu.h"
#include "upstream_slave_h2tech.h"
#include "gateway_write_log.h"
#include "system_config.h"
#include <string.h>
#include <stdio.h>

#define UART1_TX_TC_TIMEOUT_MS  50

/* 현장 요구: USART1(PC↔Mainboard) 상위통신용 Slave ID는 반드시 9로 고정 */
#define UPSTREAM_UART1_FIXED_SLAVE_ID  9u
static inline uint8_t get_slave_id(void)
{
  (void)SystemConfig_Get(); /* USART1 슬레이브 ID는 EEPROM에 영향받지 않음 */
  return UPSTREAM_UART1_FIXED_SLAVE_ID;
}
#define RX_BUF_SIZE        64
#define RING_SIZE          256
#define FRAME_END_MS       4
#define TX_GUARD_MS        2
#define RESP_BUF_SIZE      (1 + 128 + 2)
#define LOG_INTERVAL_MS    1000u
#define BOARD_TX_0XAA_INTERVAL_MS  500u  /* 보드→PC 0xAA 주기 송신 (BOARD_TX_0XAA_ENABLE=1일 때만) */
#define TX_RESP_GUARD_MS   150u   /* Modbus 응답 송신 후 이 시간(ms) 동안 0xAA 송신 금지 */

extern UART_HandleTypeDef huart1;

/* 디버그 출력:
 * 이 프로젝트는 USART1 RS485 버스를 PC↔Mainboard로 사용하므로,
 * Modbus 응답 프레임과 로그 문자열이 섞이면 "No response"나 CRC 오류가 날 수 있습니다.
 * 따라서 로그는 printf(디버그 콘솔/세미호스트/RTT 등) 기반으로 출력한다고 가정합니다.
 */
#ifndef UPSTREAM_UART1_DEBUG_LOG_ENABLE
/* CRITICAL: USART1은 PC↔Mainboard RS485(Modbus) 라인이므로, printf가 UART1로 리타겟돼 있으면
 * 응답 프레임이 깨져 FC03(긴 응답)에서 간헐 timeout/CRC 문제가 발생할 수 있다.
 * 기본값은 OFF로 두고, 필요 시 SWO/RTT 등 "UART1이 아닌 경로"가 확실할 때만 ON으로 켠다.
 */
#define UPSTREAM_UART1_DEBUG_LOG_ENABLE  0
#endif

#if UPSTREAM_UART1_DEBUG_LOG_ENABLE
static void uart1_debug_log_rx_raw(const uint8_t *frame, size_t len)
{
	/* raw 바이트가 길어질 수 있으므로 앞부분만 출력 */
	size_t max_show = (len > 32u) ? 32u : len;
	char buf[180];
	int n = snprintf(buf, sizeof(buf), "[UART1] rx raw len=%u data=", (unsigned)len);
	if (n < 0) return;
	for (size_t i = 0; i < max_show; i++) {
		if ((size_t)n >= sizeof(buf) - 4u) break;
		n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%02X ", frame[i]);
	}
	(void)printf("%s\r\n", buf);
}

static void uart1_debug_log_drop_slave_mismatch(uint8_t req_slave, uint8_t my_slave)
{
	(void)printf("[UART1] drop: slave mismatch req=%u my=%u\r\n",
	              (unsigned)req_slave, (unsigned)my_slave);
}

static void uart1_debug_log_drop_crc_fail(size_t len)
{
	(void)printf("[UART1] drop: crc fail len=%u\r\n", (unsigned)len);
}

static void uart1_debug_log_drop_len_mismatch(uint8_t fc, size_t got_len, size_t exp_len)
{
	(void)printf("[UART1] drop: len mismatch fc=%02X got=%u exp=%u\r\n",
	              (unsigned)fc, (unsigned)got_len, (unsigned)exp_len);
}

static void uart1_debug_log_parsed(uint8_t fc, uint16_t addr, uint16_t count)
{
	(void)printf("[UART1] parsed fc=%02X addr=%u count=%u\r\n",
	              (unsigned)fc, (unsigned)addr, (unsigned)count);
}

static void uart1_debug_log_tx_resp_len(uint16_t tx_len)
{
	(void)printf("[UART1] tx resp len=%u\r\n", (unsigned)tx_len);
}

static void uart1_debug_log_de_tx(void)
{
	(void)printf("[UART1] DE=TX\r\n");
}

static void uart1_debug_log_de_rx(void)
{
	(void)printf("[UART1] DE=RX\r\n");
}
#else
#define uart1_debug_log_rx_raw(frame,len) ((void)0)
#define uart1_debug_log_drop_slave_mismatch(a,b) ((void)0)
#define uart1_debug_log_drop_crc_fail(len) ((void)0)
#define uart1_debug_log_drop_len_mismatch(fc,got,exp) ((void)0)
#define uart1_debug_log_parsed(fc,addr,count) ((void)0)
#define uart1_debug_log_tx_resp_len(tx_len) ((void)0)
#define uart1_debug_log_de_tx() ((void)0)
#define uart1_debug_log_de_rx() ((void)0)
#endif

/* Actual pin level: Mode A (RS485_DE_ACTIVE_HIGH=1) TX=SET/RX=RESET. main.h: RS485_DE_* = PC_RS485_DE_RE (PB1). */
#if RS485_DE_ACTIVE_HIGH
static void set_pin_tx(void) { HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET); }
static void set_pin_rx(void) { HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET); }
#else
static void set_pin_tx(void) { HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET); }
static void set_pin_rx(void) { HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET); }
#endif

#if FORCE_RS485_RX
static void set_de_tx(void) { (void)0; }  /* keep RX: PB1 fixed for receive test */
static void set_de_rx(void) { set_pin_rx(); }
#elif FORCE_RS485_TX
static void set_de_tx(void) { set_pin_tx(); }
static void set_de_rx(void) { (void)0; }  /* keep TX */
#else
static void set_de_tx(void) { set_pin_tx(); }
static void set_de_rx(void) { set_pin_rx(); }
#endif

static uint8_t rx_buf[RX_BUF_SIZE];
static uint8_t rx_ring[RING_SIZE];
static volatile uint16_t rx_ring_len;
static volatile uint32_t last_rx_tick;

/* Debug counters */
static uint32_t rx_frame_ok_count;
static uint32_t rx_crc_fail_count;
static uint32_t rx_len_fail_count;
static uint32_t tx_resp_count;
static uint32_t last_log_tick;
static uint32_t last_0xaa_tick;  /* 보드→PC 0xAA 주기 송신 */
static uint32_t last_tx_resp_tick; /* Modbus 응답 송신 시각 (0xAA 송신 금지 구간용) */

__attribute__((weak)) void UpstreamSlaveUart1_LogCounts(uint32_t rx_ok, uint32_t rx_crc_fail, uint32_t rx_len_fail, uint32_t tx_resp)
{
	(void)rx_ok;
	(void)rx_crc_fail;
	(void)rx_len_fail;
	(void)tx_resp;
}

__attribute__((weak)) void UpstreamSlaveUart1_LogFrame(const uint8_t *frame, uint16_t len, int crc_ok, uint8_t fc, uint16_t addr)
{
	(void)frame;
	(void)len;
	(void)crc_ok;
	(void)fc;
	(void)addr;
}

__attribute__((weak)) void UpstreamSlaveUart1_LogTxResponse(const uint8_t *frame, uint16_t len)
{
	(void)frame;
	(void)len;
}

static void process_modbus_frame(const uint8_t *frame, size_t frame_len, const aggregated_status_t *agg)
{
	/* FC03 addr=2000 count=40 → PDU 필요 길이 = 1(FC)+1(byte count)+80(data)=82B */
	static uint8_t resp_pdu[128];
	static uint8_t tx_frame[RESP_BUF_SIZE];
	uint8_t fc = frame[1];
	uint16_t start_addr = (uint16_t)((frame[2] << 8) | frame[3]);
	uint16_t count = 0;
	const uint8_t *write_data = NULL;
	int resp_len = -1;

	switch (fc) {
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
		if (frame_len >= 8) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			resp_len = UpstreamSlave_HandleRequest(fc, start_addr, count, NULL, agg, resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x05:
		if (frame_len >= 8) {
			write_data = &frame[4];
			resp_len = UpstreamSlave_HandleRequest(fc, start_addr, 0, write_data, agg, resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x06:
		if (frame_len >= 8) {
			write_data = &frame[4];
			resp_len = UpstreamSlave_HandleRequest(fc, start_addr, 0, write_data, agg, resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x0F:
	case 0x10:
		if (frame_len >= 7) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			uint8_t byte_count = frame[6];
			if (frame_len >= (size_t)(7 + byte_count) && byte_count > 0) {
				write_data = &frame[7];
				resp_len = UpstreamSlave_HandleRequest(fc, start_addr, count, write_data, agg, resp_pdu, (uint16_t)sizeof(resp_pdu));
			}
		}
		break;
	default:
		break;
	}

	/* FC06 등 실패 시 무응답 방지: 예외 응답(0x86 0x04) 전송 */
	if (resp_len <= 0 && (fc == 0x01 || fc == 0x02 || fc == 0x03 || fc == 0x04 || fc == 0x05 || fc == 0x06 || fc == 0x0F || fc == 0x10)) {
		resp_pdu[0] = (uint8_t)(fc | 0x80);
		resp_pdu[1] = 0x04;  /* Slave device failure */
		resp_len = 2;
	}

	if (resp_len > 0 && (size_t)(1 + resp_len + 2) <= sizeof(tx_frame)) {
		tx_frame[0] = get_slave_id();
		memcpy(&tx_frame[1], resp_pdu, (size_t)resp_len);
		ModbusRTU_AppendCRC(tx_frame, (size_t)(1 + resp_len));
		uint16_t tx_len = (uint16_t)(1 + resp_len + 2);
#if UPSTREAM_DEBUG_LOG
		UpstreamSlaveUart1_LogTxResponse(tx_frame, tx_len);
#endif
#if FC06_DEBUG_LOG
		if (fc == 0x06) {
			Gateway_LogFc06SendingResponseToPc(tx_frame, tx_len);
			Gateway_LogFc06ResponseHex(tx_frame, tx_len);
		}
#endif
		last_tx_resp_tick = HAL_GetTick();
		LED_Status_OnUart1TxRespBefore();
		uart1_debug_log_tx_resp_len(tx_len);
		uart1_debug_log_de_tx();
		set_de_tx();
		/* 응답 길이(FC04 diag 32regs 등)가 길면 100ms timeout이 타이트할 수 있어 여유를 둔다. */
		(void)HAL_UART_Transmit(&huart1, tx_frame, tx_len, 200);
		/* 마지막 바이트가 나갈 때까지 대기 후 DE → RX (응답 잘림/No Response 방지) */
		{
			uint32_t start = HAL_GetTick();
			while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) {
				if ((HAL_GetTick() - start) > UART1_TX_TC_TIMEOUT_MS)
					break;
			}
		}
		uart1_debug_log_de_rx();
		set_de_rx();
		if (TX_GUARD_MS > 0)
			HAL_Delay(TX_GUARD_MS);
		tx_resp_count++;
		LED_Status_OnUart1SlaveTx();
		LED_Status_OnRS485Activity();
		(void)HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buf, RX_BUF_SIZE);
	}
}

void UpstreamSlaveUart1_Init(void)
{
	rx_ring_len = 0;
	last_rx_tick = 0;
	rx_frame_ok_count = 0;
	rx_crc_fail_count = 0;
	rx_len_fail_count = 0;
	tx_resp_count = 0;
	last_log_tick = HAL_GetTick();
	last_tx_resp_tick = 0;
	set_de_rx();   /* Boot: always RX first (PB1=RX); FORCE_RS485_TX=1 then overrides below */
#if FORCE_RS485_TX
	set_de_tx();   /* Optional: fix PB1=TX for test */
#endif
	last_0xaa_tick = HAL_GetTick();
	(void)HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buf, RX_BUF_SIZE);
}

void UpstreamSlaveUart1_GetCounts(uint32_t *rx_ok, uint32_t *rx_crc_fail, uint32_t *rx_len_fail, uint32_t *tx_resp)
{
	if (rx_ok) *rx_ok = rx_frame_ok_count;
	if (rx_crc_fail) *rx_crc_fail = rx_crc_fail_count;
	if (rx_len_fail) *rx_len_fail = rx_len_fail_count;
	if (tx_resp) *tx_resp = tx_resp_count;
}

void UpstreamSlaveUart1_RxEventCallback(uint16_t Size)
{
	if (Size == 0 || Size > RX_BUF_SIZE) return;
	/* Indicate "at least 1 byte received" (before CRC/Modbus) for receive test */
	LED_Status_OnUart1RxEvent();
	uint32_t now = HAL_GetTick();
	if ((uint16_t)(rx_ring_len + Size) <= RING_SIZE) {
		memcpy(rx_ring + rx_ring_len, rx_buf, (size_t)Size);
		rx_ring_len += (uint16_t)Size;
	} else {
		rx_ring_len = 0;
	}
	last_rx_tick = now;
	(void)HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buf, RX_BUF_SIZE);
}

void UpstreamSlaveUart1_Poll(const aggregated_status_t *agg)
{
	uint32_t now = HAL_GetTick();

	/* 완성된 Modbus 프레임이 있으면 먼저 처리 (이 경로에서는 0xAA 송신 없음 → 응답과 0xAA 혼선 방지) */
	if (rx_ring_len > 0 && (now - last_rx_tick) >= (uint32_t)FRAME_END_MS) {
		/* 1s periodic log */
		if ((now - last_log_tick) >= LOG_INTERVAL_MS) {
			last_log_tick = now;
			UpstreamSlaveUart1_LogCounts(rx_frame_ok_count, rx_crc_fail_count, rx_len_fail_count, tx_resp_count);
		}

		uint16_t frame_len = rx_ring_len;
		static uint8_t frame_buf[RING_SIZE];
		if (frame_len > RING_SIZE) frame_len = (uint16_t)RING_SIZE;
		memcpy(frame_buf, rx_ring, (size_t)frame_len);
		rx_ring_len = 0;

		/* FC03 addr=2100 cnt=2 요청: 8바이트 (SlaveId=9 + FC=03 + Addr 0x0834 + Cnt 2 + CRC2).
		 * ModbusRTU_GetExpectedRequestLength(FC03) = 8. CRC = Modbus RTU (poly 0xA001), LSB first. */
		uart1_debug_log_rx_raw(frame_buf, frame_len);

		if (frame_len >= 4u) {
			uint8_t req_slave = frame_buf[0];
			uint8_t my_slave  = get_slave_id();

			/* Slave ID mismatch: 조용히 drop하지 말고 원인을 로그로 남김 */
			if (req_slave != my_slave) {
				uart1_debug_log_drop_slave_mismatch(req_slave, my_slave);
				return;
			}

			size_t expected = ModbusRTU_GetExpectedRequestLength(frame_buf, (size_t)frame_len);
			/* Expected length mismatch (FC에 따라 8/7+byte_count 등):
			 * 유효한 프레임이 아니므로 처리하지 않고 drop. */
			if (!(expected != 0u && (size_t)frame_len >= expected)) {
				rx_len_fail_count++;
				uart1_debug_log_drop_len_mismatch(frame_buf[1], frame_len, expected);
#if UPSTREAM_DEBUG_LOG
				{
					uint16_t log_len = (frame_len > 16u) ? 16u : frame_len;
					uint16_t addr = (frame_len >= 4u) ? (uint16_t)((frame_buf[2] << 8) | frame_buf[3]) : 0u;
					UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 0,
					                            frame_len >= 2u ? frame_buf[1] : 0u, addr);
				}
#endif
				return;
			}

			/* CRC check */
			if (ModbusRTU_CRC16Check(frame_buf, expected) == 0) {
				rx_frame_ok_count++;
#if UPSTREAM_DEBUG_LOG
				{
					uint16_t log_len = (expected > 16u) ? 16u : (uint16_t)expected;
					uint16_t addr = (uint16_t)((frame_buf[2] << 8) | frame_buf[3]);
					UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 1, frame_buf[1], addr);
				}
#endif

#if UPSTREAM_UART1_DEBUG_LOG_ENABLE
				/* 정상 parse 로그 (요구 포맷) */
				{
					uint16_t addr = (uint16_t)((frame_buf[2] << 8) | frame_buf[3]);
					uint16_t count = (uint16_t)((frame_buf[4] << 8) | frame_buf[5]);
					uart1_debug_log_parsed(frame_buf[1], addr, count);
				}
#endif

				LED_Status_OnUart1CrcOk();   /* LED3 50ms: CRC OK (3단계 디버그) */
				LED_Status_OnRS485Activity();
				process_modbus_frame(frame_buf, expected, agg);
				return;
			}

			/* CRC fail */
			rx_crc_fail_count++;
			uart1_debug_log_drop_crc_fail(expected);
#if UPSTREAM_DEBUG_LOG
			{
				uint16_t log_len = (expected > 16u) ? 16u : (uint16_t)expected;
				uint16_t addr = (uint16_t)((frame_buf[2] << 8) | frame_buf[3]);
				UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 0, frame_buf[1], addr);
			}
#endif
		}
		return;
	}

	/* 유휴 시에만 0xAA 송신: Modbus 처리 중이 아니고, 응답 직후 금지 구간이 지났을 때만 */
#if BOARD_TX_0XAA_ENABLE
	if (rx_ring_len == 0
	    && (now - last_tx_resp_tick) >= TX_RESP_GUARD_MS
	    && (now - last_0xaa_tick) >= BOARD_TX_0XAA_INTERVAL_MS) {
		last_0xaa_tick = now;
		static const uint8_t byte_0xaa = 0xAA;
		set_pin_tx();
		(void)HAL_UART_Transmit(&huart1, (uint8_t *)&byte_0xaa, 1, 50);
		set_pin_rx();
		if (TX_GUARD_MS > 0)
			HAL_Delay(TX_GUARD_MS);
		LED_Status_OnUart1SlaveTx();
		(void)HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buf, RX_BUF_SIZE);
	}
#endif

	if (rx_ring_len == 0) {
		if ((now - last_log_tick) >= LOG_INTERVAL_MS) {
			last_log_tick = now;
			UpstreamSlaveUart1_LogCounts(rx_frame_ok_count, rx_crc_fail_count, rx_len_fail_count, tx_resp_count);
		}
		return;
	}
	/* rx_ring_len > 0 but frame not yet complete (waiting FRAME_END_MS) */
}
