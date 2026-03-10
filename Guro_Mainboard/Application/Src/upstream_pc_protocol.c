/**
 * @file upstream_pc_protocol.c
 * @brief USART2 upstream: ring-buffer RX, 3.5char frame boundary, Modbus RTU slave (ID 9) + STX/ETX.
 *        RTU frame end = 4ms silence; FC-based expected length used for CRC/process. TX guard 2ms after response.
 */
#include "upstream_pc_protocol.h"
#include "main.h"
#include "app_config.h"
#include "led_status.h"
#include "modbus_rtu.h"
#include "upstream_slave_h2tech.h"
#include "system_config.h"
#include <string.h>
#include <stdio.h>

#define UPSTREAM_MODBUS_SLAVE_ID_DEFAULT  9
static inline uint8_t get_upstream_slave_id(void) {
  const system_config_t *c = SystemConfig_Get();
  return c ? (uint8_t)c->slave_id : UPSTREAM_MODBUS_SLAVE_ID_DEFAULT;
}
#define MODBUS_RESP_BUF_SIZE     (1 + 64 + 2)   /* slave_id + PDU + CRC */
#define RX_RING_SIZE             256
#define FRAME_END_MS              4             /* 3.5 char time at 9600 bps */
#define TX_GUARD_MS               2
#define UPSTREAM_DEBUG_LOG       0             /* 1 = enable one-line hex + CRC + FC/addr log */

extern UART_HandleTypeDef huart2;
#if USE_PC_TEST_UART1_SLAVE
extern UART_HandleTypeDef huart1;
#include "upstream_slave_uart1.h"
#endif

static uint8_t rx_buf[UPSTREAM_RX_BUF_SIZE];
static uint8_t tx_buf[UPSTREAM_TX_BUF_SIZE];
static uint8_t rx_accum[RX_RING_SIZE];
static volatile uint16_t rx_accum_len;
static volatile uint32_t last_rx_tick;
static uint8_t tx_busy;
static upstream_cmd_cb_t cmd_cb;

static uint32_t invalid_len_count;
static uint32_t invalid_crc_count;

#if UPSTREAM_DEBUG_LOG
__attribute__((weak)) void UpstreamPC_Log(const char *msg, int len) { (void)msg; (void)len; }
#endif

static uint8_t xor_checksum(const uint8_t *p, size_t n)
{
	uint8_t chk = 0;
	while (n--) chk ^= *p++;
	return chk;
}

void UpstreamPC_Init(void)
{
	rx_accum_len = 0;
	last_rx_tick = 0;
	tx_busy = 0;
	cmd_cb = NULL;
	invalid_len_count = 0;
	invalid_crc_count = 0;
	(void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_buf, UPSTREAM_RX_BUF_SIZE);
}

void UpstreamPC_UART_RxEventCallback(uint16_t Size)
{
	if (Size == 0 || Size > UPSTREAM_RX_BUF_SIZE) return;
	uint32_t now = HAL_GetTick();
	if ((uint16_t)(rx_accum_len + Size) <= RX_RING_SIZE) {
		memcpy(rx_accum + rx_accum_len, rx_buf, (size_t)Size);
		rx_accum_len += (uint16_t)Size;
	} else {
		rx_accum_len = 0; /* overflow: discard */
	}
	last_rx_tick = now;
	(void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_buf, UPSTREAM_RX_BUF_SIZE);
}

static void parse_frame(const uint8_t *frame, uint8_t len)
{
	if (len < 4) return; /* STX CMD CHK ETX minimum */
	if (frame[0] != UPSTREAM_STX || frame[len - 1] != UPSTREAM_ETX) return;
	uint8_t chk = xor_checksum(frame + 1, (size_t)(len - 2));
	if (chk != frame[len - 2]) return;

	LED_Status_OnRS485Activity();
	uint8_t cmd = frame[1];
	uint8_t payload_len = len - 4;
	const uint8_t *payload = payload_len > 0 ? &frame[2] : NULL;
	if (cmd_cb) cmd_cb(cmd, payload, payload_len);
}

#if UPSTREAM_DEBUG_LOG
static void log_frame(const uint8_t *frame, size_t len, int crc_ok, uint8_t fc, uint16_t addr)
{
	char buf[80];
	size_t n = 0;
	for (size_t i = 0; i < len && i < 16 && n < sizeof(buf) - 4; i++)
		n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%02X ", frame[i]);
	n += (size_t)snprintf(buf + n, sizeof(buf) - n, "| CRC:%s", crc_ok ? "OK" : "FAIL");
	if (crc_ok && (fc == 0x02 || fc == 0x03 || fc == 0x05 || fc == 0x06 || fc == 0x0F))
		snprintf(buf + n, sizeof(buf) - n, " FC%02X addr=%u", fc, (unsigned)addr);
	UpstreamPC_Log(buf, (int)strlen(buf));
}
#endif

#if UPSTREAM_PC_MODBUS_SLAVE_ENABLE
/* PC tool sends 0-based coil/register addresses; H2Map_ModbusAddrToH2Dec(start_addr) = start_addr+1. */
static void process_modbus_frame(const uint8_t *frame, size_t frame_len, const aggregated_status_t *agg)
{
	static uint8_t resp_pdu[64];
	static uint8_t modbus_resp_buf[MODBUS_RESP_BUF_SIZE];
	uint8_t fc = frame[1];
	uint16_t start_addr = (uint16_t)((frame[2] << 8) | frame[3]);
	uint16_t count = 0;
	const uint8_t *write_data = NULL;
	int resp_len = -1;

	switch (fc) {
	case 0x02:
		if (frame_len >= 8) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			resp_len = UpstreamSlave_HandleRequest(
				fc, start_addr, count, NULL, agg,
				resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x03:
		if (frame_len >= 8 && agg) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			resp_len = UpstreamSlave_HandleRequest(
				fc, start_addr, count, NULL, agg,
				resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x05:
		if (frame_len >= 8) {
			write_data = &frame[4];
			resp_len = UpstreamSlave_HandleRequest(
				fc, start_addr, 0, write_data, agg,
				resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x06:
		if (frame_len >= 8) {
			write_data = &frame[4];
			resp_len = UpstreamSlave_HandleRequest(
				fc, start_addr, 0, write_data, agg,
				resp_pdu, (uint16_t)sizeof(resp_pdu));
		}
		break;
	case 0x0F:
		if (frame_len >= 7) {
			count = (uint16_t)((frame[4] << 8) | frame[5]);
			uint8_t byte_count = frame[6];
			if (frame_len >= (size_t)(7 + byte_count) && byte_count > 0) {
				write_data = &frame[7];
				resp_len = UpstreamSlave_HandleRequest(
					fc, start_addr, count, write_data, agg,
					resp_pdu, (uint16_t)sizeof(resp_pdu));
			}
		}
		break;
	default:
		break;
	}

	if (resp_len > 0 && (size_t)(1 + resp_len + 2) <= sizeof(modbus_resp_buf)) {
		modbus_resp_buf[0] = get_upstream_slave_id();
		memcpy(&modbus_resp_buf[1], resp_pdu, (size_t)resp_len);
		ModbusRTU_AppendCRC(modbus_resp_buf, (size_t)(1 + resp_len));
		(void)HAL_UART_Transmit(&huart2, modbus_resp_buf,
			(uint16_t)(1 + resp_len + 2), 100);
		LED_Status_OnRS485Activity();
		HAL_Delay(TX_GUARD_MS);
		(void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_buf, UPSTREAM_RX_BUF_SIZE);
	}
}
#endif /* UPSTREAM_PC_MODBUS_SLAVE_ENABLE */

void UpstreamPC_Poll(const aggregated_status_t *agg)
{
	if (rx_accum_len == 0) return;
	uint32_t now = HAL_GetTick();
	if ((now - last_rx_tick) < (uint32_t)FRAME_END_MS) return;

	/* Frame end (3.5 char time silence): copy and clear ring */
	uint16_t frame_len = rx_accum_len;
	static uint8_t frame_buf[RX_RING_SIZE];
	if (frame_len > RX_RING_SIZE) frame_len = (uint16_t)RX_RING_SIZE;
	memcpy(frame_buf, rx_accum, (size_t)frame_len);
	rx_accum_len = 0;

#if UPSTREAM_PC_MODBUS_SLAVE_ENABLE
	/* Modbus RTU (slave 9): require expected length then CRC */
	if (frame_len >= 4 && frame_buf[0] == get_upstream_slave_id()) {
		size_t expected = ModbusRTU_GetExpectedRequestLength(frame_buf, (size_t)frame_len);
		if (expected == 0 || (size_t)frame_len < expected) {
#if UPSTREAM_DEBUG_LOG
			invalid_len_count++;
			log_frame(frame_buf, (size_t)frame_len, 0, 0, 0);
#endif
			return;
		}
		if (ModbusRTU_CRC16Check(frame_buf, expected) != 0) {
#if UPSTREAM_DEBUG_LOG
			invalid_crc_count++;
			log_frame(frame_buf, expected, 0, frame_buf[1], (uint16_t)((frame_buf[2]<<8)|frame_buf[3]));
#endif
			return;
		}
#if UPSTREAM_DEBUG_LOG
		log_frame(frame_buf, expected, 1, frame_buf[1], (uint16_t)((frame_buf[2]<<8)|frame_buf[3]));
#endif
		process_modbus_frame(frame_buf, expected, agg);
		return;
	}
#endif
	/* STX/ETX protocol */
	parse_frame(frame_buf, (uint8_t)frame_len);
}

/* Serialize aggregated_status into tx_buf: STX LEN [bytes...] CHK ETX. LEN = payload byte count. */
static int build_status_frame(const aggregated_status_t *s, uint8_t *out, size_t out_size)
{
	if (!s || out_size < 8) return -1;
	size_t i = 0;
	out[i++] = UPSTREAM_STX;
	uint8_t *len_ptr = &out[i++];
	out[i++] = (uint8_t)(s->timestamp_ms >> 0);
	out[i++] = (uint8_t)(s->timestamp_ms >> 8);
	out[i++] = (uint8_t)(s->timestamp_ms >> 16);
	out[i++] = (uint8_t)(s->timestamp_ms >> 24);
	out[i++] = (uint8_t)(s->env_temp_cx10 >> 0);
	out[i++] = (uint8_t)(s->env_temp_cx10 >> 8);
	out[i++] = (uint8_t)(s->env_rh_x10 >> 0);
	out[i++] = (uint8_t)(s->env_rh_x10 >> 8);
	out[i++] = s->main_di;
	out[i++] = s->main_do;
	out[i++] = s->hpsb_coils;
	out[i++] = s->hpsb_discrete;
	out[i++] = (uint8_t)(s->hpsb_status_reg >> 0);
	out[i++] = (uint8_t)(s->hpsb_status_reg >> 8);
	out[i++] = (uint8_t)(s->hpsb_alarm_reg >> 0);
	out[i++] = (uint8_t)(s->hpsb_alarm_reg >> 8);
	uint8_t  lpsb_coils = 0;
	lpsb_coils |= (s->lpsb1_coils[0] ? (1u << 0) : 0);
	lpsb_coils |= (s->lpsb1_coils[1] ? (1u << 1) : 0);
	lpsb_coils |= (s->lpsb1_coils[2] ? (1u << 2) : 0);
	uint8_t  lpsb_discrete = 0;
	uint16_t lpsb_status_reg = 0;
	uint16_t lpsb_alarm_reg = s->lpsb1_alarm_reg;
	out[i++] = lpsb_coils;
	out[i++] = lpsb_discrete;
	out[i++] = (uint8_t)(lpsb_status_reg >> 0);
	out[i++] = (uint8_t)(lpsb_status_reg >> 8);
	out[i++] = (uint8_t)(lpsb_alarm_reg >> 0);
	out[i++] = (uint8_t)(lpsb_alarm_reg >> 8);
	out[i++] = (uint8_t)(s->error_flags >> 0);
	out[i++] = (uint8_t)(s->error_flags >> 8);
	if (i + 3 > out_size) return -1;
	*len_ptr = (uint8_t)(i - 2);
	uint8_t chk = xor_checksum(&out[1], (size_t)(i - 1));
	out[i++] = chk;
	out[i++] = UPSTREAM_ETX;
	return (int)i;
}

int UpstreamPC_SendStatus(const aggregated_status_t *status)
{
	if (tx_busy) return -1;
	int len = build_status_frame(status, tx_buf, UPSTREAM_TX_BUF_SIZE);
	if (len <= 0) return -1;
	tx_busy = 1;
	if (HAL_UART_Transmit_IT(&huart2, tx_buf, (uint16_t)len) != HAL_OK) {
		tx_busy = 0;
		return -1;
	}
	return 0;
}

void UpstreamPC_SetCommandCallback(upstream_cmd_cb_t cb)
{
	cmd_cb = cb;
}

void UpstreamPC_GetInvalidCounts(uint32_t *p_len, uint32_t *p_crc)
{
	if (p_len) *p_len = invalid_len_count;
	if (p_crc) *p_crc = invalid_crc_count;
}

void UpstreamPC_TxCpltCallback(void)
{
	tx_busy = 0;
	LED_Status_OnRS485Activity();
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
#if USE_PC_TEST_UART1_SLAVE
	if (huart == &huart1) {
		UpstreamSlaveUart1_RxEventCallback(Size);
		return;
	}
#endif
	if (huart == &huart2)
		UpstreamPC_UART_RxEventCallback(Size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart == &huart2)
		UpstreamPC_TxCpltCallback();
}
