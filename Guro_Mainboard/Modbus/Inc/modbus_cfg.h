/**
 * @file modbus_cfg.h
 * @brief Modbus configuration (MAIN board = Master for Subboard).
 *        HW: USART2 (PA2/PA3), DE/RE = PB12. main.h CubeMX: RS_485_DE_RE_*.
 */
#ifndef MODBUS_CFG_H
#define MODBUS_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MASTER         1
#define MODBUS_SLAVE          0

/* Subboard RS485: USART2, DE/RE = PB12 (CubeMX label: RS_485_DE_RE) */
#define MODBUS_UART           huart2
#define MODBUS_DE_GPIO_PORT   RS_485_DE_RE_GPIO_Port
#define MODBUS_DE_GPIO_PIN    RS_485_DE_RE_Pin

/* Timing at 38400 baud (1 char ~= 0.26ms, t3.5 ~= 0.9ms).
 * FC04 16regs 응답(약 37바이트)은 선로 점유만 보면 대략 10ms 수준이므로,
 * 처리 여유를 포함해도 40ms면 충분한 편이다.
 * 주의: 여러 슬레이브가 연속 미응답이면 N×timeout 동안 UART2가 점유되므로
 * timeout을 너무 크게 잡지 않는 편이 전체 복원력에 유리하다. */
#define MODBUS_RESPONSE_TIMEOUT_MS    40
#define MODBUS_FRAME_DELAY_MS         5
/* FC05 response: 8 bytes. Timeout for waiting subboard reply (lower-bus gateway). */
#define MODBUS_FC05_RESPONSE_LEN       8
#define MODBUS_FC05_RX_TIMEOUT_MS      120  /* 38400 baud 기준 충분한 여유를 남기되 fail-fast 성격 강화 */
/* Do NOT delay or flush RX after TX; slave may respond after t3.5 (~1ms at 38400). Flush only before TX. */
#define MODBUS_FC05_RX_DELAY_MS        0
/* After TX: explicit wait for UART_FLAG_TC on USART2 before DE LOW (gateway write path). */
#define MODBUS_FC05_TX_TC_TIMEOUT_MS   30
/* DE/RE (PB12): delay after asserting TX before first byte; 0=off, 1=typical for MAX3485 */
#ifndef MODBUS_DE_TX_SETTLE_MS
#define MODBUS_DE_TX_SETTLE_MS       1
#endif

/* Buffer sizes
 * Upstream slave (UART1→PC): UART1_RESP_PDU_MAX_BYTES=256, tx_frame=259 (separate, upstream_slave_uart1.c)
 * Downstream master (USART2→Sub): uses these buffers.
 *   FC04 HPSB/LPSB max response: slave(1)+FC(1)+bc(1)+16regs×2(32)+CRC(2) = 37B → 64 was fine for sub.
 *   MB_IR_MAIN_COUNT=94 regs → PDU=1+1+188=190B, RTU frame=192B
 *   UART1 FC04 full read (94 regs): unit+PDU+CRC = 1+190+2 = 193B
 *   => 256-byte buffers remain sufficient.
 *   MODBUS_MAX_PDU_LEN raised to 256 to avoid buffer overrun when sub responds with packed data. */
#define MODBUS_RTU_RX_BUF_SIZE        256
#define MODBUS_RTU_TX_BUF_SIZE        256
#define MODBUS_MAX_PDU_LEN            256

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CFG_H */
