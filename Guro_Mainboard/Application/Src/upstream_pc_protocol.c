/**
 * @file upstream_pc_protocol.c
 * @brief USART2 upstream: ReceiveToIdle_IT, frame parser, non-blocking TX. Overrides HAL UART callbacks for huart2.
 */
#include "upstream_pc_protocol.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

static uint8_t rx_buf[UPSTREAM_RX_BUF_SIZE];
static uint8_t tx_buf[UPSTREAM_TX_BUF_SIZE];
static volatile uint8_t rx_ready;
static uint8_t tx_busy;
static upstream_cmd_cb_t cmd_cb;

static uint8_t xor_checksum(const uint8_t *p, size_t n)
{
	uint8_t chk = 0;
	while (n--) chk ^= *p++;
	return chk;
}

void UpstreamPC_Init(void)
{
	rx_ready = 0;
	tx_busy = 0;
	cmd_cb = NULL;
	(void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_buf, UPSTREAM_RX_BUF_SIZE);
}

void UpstreamPC_UART_RxEventCallback(uint16_t Size)
{
	if (Size > 0 && Size <= UPSTREAM_RX_BUF_SIZE)
		rx_ready = (uint8_t)Size;
}

static void parse_frame(uint8_t len)
{
	if (len < 4) return; /* STX CMD CHK ETX minimum */
	if (rx_buf[0] != UPSTREAM_STX || rx_buf[len - 1] != UPSTREAM_ETX) return;
	uint8_t chk = xor_checksum(rx_buf + 1, (size_t)(len - 2));
	if (chk != rx_buf[len - 2]) return;

	uint8_t cmd = rx_buf[1];
	uint8_t payload_len = len - 4; /* between CMD and CHK */
	const uint8_t *payload = payload_len > 0 ? &rx_buf[2] : NULL;
	if (cmd_cb) cmd_cb(cmd, payload, payload_len);
}

void UpstreamPC_Poll(void)
{
	if (rx_ready) {
		uint8_t len = rx_ready;
		rx_ready = 0;
		parse_frame(len);
		(void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_buf, UPSTREAM_RX_BUF_SIZE);
	}
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
	/* Backward-compatible compact LPSB summary:
	 * - lpsb_coils: LPSB1 coils[0..2] packed into bits[0..2]
	 * - lpsb_discrete/status_reg: not modeled in aggregated_status_t -> 0
	 * - lpsb_alarm_reg: use LPSB1 alarm reg
	 */
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

/* Called from HAL when UART TX IT completes: clear tx_busy. */
void UpstreamPC_TxCpltCallback(void)
{
	tx_busy = 0;
}

/* Override weak HAL callbacks for USART2 (PC link). */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart == &huart2)
		UpstreamPC_UART_RxEventCallback(Size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart == &huart2)
		UpstreamPC_TxCpltCallback();
}
