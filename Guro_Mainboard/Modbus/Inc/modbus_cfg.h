/**
 * @file modbus_cfg.h
 * @brief Modbus configuration (MAIN board = Master)
 */
#ifndef MODBUS_CFG_H
#define MODBUS_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MASTER         1
#define MODBUS_SLAVE          0

#define MODBUS_UART           huart1
#define MODBUS_DE_GPIO_PORT   RS485_DE_GPIO_Port
#define MODBUS_DE_GPIO_PIN    RS485_DE_Pin

/* Timing (character time at 9600 baud ~ 1.04 ms per char) */
#define MODBUS_RESPONSE_TIMEOUT_MS    50
#define MODBUS_FRAME_DELAY_MS         5

/* Buffer sizes */
#define MODBUS_RTU_RX_BUF_SIZE        64
#define MODBUS_RTU_TX_BUF_SIZE        64
#define MODBUS_MAX_PDU_LEN            64

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CFG_H */
