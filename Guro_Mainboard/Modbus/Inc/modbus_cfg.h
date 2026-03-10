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

/* Timing (character time at 9600 baud ~ 1.04 ms per char) */
#define MODBUS_RESPONSE_TIMEOUT_MS    50
#define MODBUS_FRAME_DELAY_MS         5
/* DE/RE (PB12): delay after asserting TX before first byte; 0=off, 1=typical for MAX3485 */
#ifndef MODBUS_DE_TX_SETTLE_MS
#define MODBUS_DE_TX_SETTLE_MS       1
#endif

/* Buffer sizes */
#define MODBUS_RTU_RX_BUF_SIZE        64
#define MODBUS_RTU_TX_BUF_SIZE        64
#define MODBUS_MAX_PDU_LEN            64

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CFG_H */
