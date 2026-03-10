/**
 * @file upstream_slave_uart1.c
 * @brief Modbus RTU Slave on USART1 (PA9/PA10), DE/RE=PB1. ReceiveToIdle_IT, 4ms frame end, FC02/03/05/06/15.
 *        RS485 polarity: RS485_DE_ACTIVE_HIGH (app_config.h) 1=Mode A, 0=Mode B.
 */
#include "upstream_slave_uart1.h"
#include "app_config.h"
#include "main.h"
#include "led_status.h"
#include "modbus_rtu.h"
#include "upstream_slave_h2tech.h"
#include "system_config.h"
#include <string.h>

#define SLAVE_ID_DEFAULT   9
static inline uint8_t get_slave_id(void) {
  const system_config_t *c = SystemConfig_Get();
  return c ? (uint8_t)c->slave_id : SLAVE_ID_DEFAULT;
}
#define RX_BUF_SIZE        64
#define RING_SIZE          256
#define FRAME_END_MS       4
#define TX_GUARD_MS        2
#define RESP_BUF_SIZE      (1 + 64 + 2)
#define LOG_INTERVAL_MS    1000u
#define BOARD_TX_0XAA_INTERVAL_MS  500u  /* 보드→PC 0xAA 주기 송신 (BOARD_TX_0XAA_ENABLE=1일 때만) */
#define TX_RESP_GUARD_MS   150u   /* Modbus 응답 송신 후 이 시간(ms) 동안 0xAA 송신 금지 */

extern UART_HandleTypeDef huart1;

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
	static uint8_t resp_pdu[64];
	static uint8_t tx_frame[RESP_BUF_SIZE];
	uint8_t fc = frame[1];
	uint16_t start_addr = (uint16_t)((frame[2] << 8) | frame[3]);
	uint16_t count = 0;
	const uint8_t *write_data = NULL;
	int resp_len = -1;

	switch (fc) {
	case 0x02:
		if (frame_len >= 8) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			resp_len = UpstreamSlave_HandleRequest(fc, start_addr, count, NULL, agg, resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x03:
		if (frame_len >= 8 && agg) {
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

	if (resp_len > 0 && (size_t)(1 + resp_len + 2) <= sizeof(tx_frame)) {
		tx_frame[0] = get_slave_id();
		memcpy(&tx_frame[1], resp_pdu, (size_t)resp_len);
		ModbusRTU_AppendCRC(tx_frame, (size_t)(1 + resp_len));
		uint16_t tx_len = (uint16_t)(1 + resp_len + 2);
#if UPSTREAM_DEBUG_LOG
		UpstreamSlaveUart1_LogTxResponse(tx_frame, tx_len);
#endif
		last_tx_resp_tick = HAL_GetTick();  /* 응답 직후 0xAA 송신 금지 구간 시작 */
		LED_Status_OnUart1TxRespBefore();  /* LED2 50ms: 응답 송신 직전 (3단계 디버그) */
		set_de_tx();
		(void)HAL_UART_Transmit(&huart1, tx_frame, tx_len, 100);
		set_de_rx();
		if (TX_GUARD_MS > 0)
			HAL_Delay(TX_GUARD_MS);
		tx_resp_count++;   /* HAL_UART_Transmit 호출 직후에만 증가 (실제 송신 발생 시) */
		LED_Status_OnUart1SlaveTx();   /* LED3 pulse: response sent */
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
		if (frame_len >= 4 && frame_buf[0] == get_slave_id()) {
			size_t expected = ModbusRTU_GetExpectedRequestLength(frame_buf, (size_t)frame_len);
			if (expected != 0 && (size_t)frame_len >= expected) {
				if (ModbusRTU_CRC16Check(frame_buf, expected) == 0) {
					rx_frame_ok_count++;
#if UPSTREAM_DEBUG_LOG
					{
						uint16_t log_len = (expected > 16u) ? 16u : (uint16_t)expected;
						uint16_t addr = (uint16_t)((frame_buf[2] << 8) | frame_buf[3]);
						UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 1, frame_buf[1], addr);
					}
#endif
					LED_Status_OnUart1CrcOk();   /* LED3 50ms: CRC OK (3단계 디버그) */
					LED_Status_OnRS485Activity();
					process_modbus_frame(frame_buf, expected, agg);
					return;
				}
				rx_crc_fail_count++;
#if UPSTREAM_DEBUG_LOG
				{
					uint16_t log_len = (expected > 16u) ? 16u : (uint16_t)expected;
					uint16_t addr = (uint16_t)((frame_buf[2] << 8) | frame_buf[3]);
					UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 0, frame_buf[1], addr);
				}
#endif
			} else {
				rx_len_fail_count++;
#if UPSTREAM_DEBUG_LOG
				{
					uint16_t log_len = (frame_len > 16u) ? 16u : frame_len;
					uint16_t addr = (frame_len >= 4u) ? (uint16_t)((frame_buf[2] << 8) | frame_buf[3]) : 0u;
					UpstreamSlaveUart1_LogFrame(frame_buf, log_len, 0, frame_len >= 2u ? frame_buf[1] : 0u, addr);
				}
#endif
			}
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
