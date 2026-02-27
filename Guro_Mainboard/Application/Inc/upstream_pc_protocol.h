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
void UpstreamPC_Poll(void);
int  UpstreamPC_SendStatus(const aggregated_status_t *status);

typedef void (*upstream_cmd_cb_t)(uint8_t cmd, const uint8_t *data, uint8_t len);
void UpstreamPC_SetCommandCallback(upstream_cmd_cb_t cb);

void UpstreamPC_UART_RxEventCallback(uint16_t Size);
void UpstreamPC_TxCpltCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_PC_PROTOCOL_H */
