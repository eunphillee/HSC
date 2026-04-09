/**
 * @file upstream_pc_protocol.h
 * @brief Upstream PC link over USART2: frame-based RX (ReceiveToIdle_IT), simple frame format, non-blocking TX.
 */
#ifndef UPSTREAM_PC_PROTOCOL_H
#define UPSTREAM_PC_PROTOCOL_H

#include "aggregated_status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPSTREAM_STX  0x02
#define UPSTREAM_ETX  0x03
#define UPSTREAM_RX_BUF_SIZE  64
#define UPSTREAM_TX_BUF_SIZE  128

void UpstreamPC_Init(void);
void UpstreamPC_Poll(const aggregated_status_t *agg);
int  UpstreamPC_SendStatus(const aggregated_status_t *status);

typedef void (*upstream_cmd_cb_t)(uint8_t cmd, const uint8_t *data, uint8_t len);
void UpstreamPC_SetCommandCallback(upstream_cmd_cb_t cb);

void UpstreamPC_UART_RxEventCallback(uint16_t Size);
void UpstreamPC_TxCpltCallback(void);
/* USART2 lower-bus Modbus transaction 구간에서는 ReceiveToIdle_IT를 잠시 중지해
 * HAL_UART_Receive(블로킹)와 RX 경합을 방지한다. */
void UpstreamPC_PauseUart2RxIT(void);
void UpstreamPC_ResumeUart2RxIT(void);

/** Optional debug: get invalid frame counts (length / CRC). Implemented when UPSTREAM_DEBUG_LOG=1 in .c. */
void UpstreamPC_GetInvalidCounts(uint32_t *p_len, uint32_t *p_crc);

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_PC_PROTOCOL_H */
