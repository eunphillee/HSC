/**
 * @file modbus_cfg.h
 * @brief Modbus configuration (HPSB = Slave, address 1)
 */
#ifndef MODBUS_CFG_HPSB_H
#define MODBUS_CFG_HPSB_H

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MASTER         0
#define MODBUS_SLAVE          1

#define MODBUS_SLAVE_ADDR     1
#define MODBUS_UART           huart1
#define MODBUS_DE_GPIO_PORT   RS485_DE_GPIO_Port
#define MODBUS_DE_GPIO_PIN    RS485_DE_Pin

#define MODBUS_RTU_RX_BUF_SIZE    64
#define MODBUS_RTU_TX_BUF_SIZE    64
#define MODBUS_MAX_PDU_LEN        64

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CFG_HPSB_H */
