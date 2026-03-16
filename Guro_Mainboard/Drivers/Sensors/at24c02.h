/**
 * @file at24c02.h
 * @brief AT24C02C EEPROM driver. I2C3 only (PA8/SCL, PC9/SDA).
 */
#ifndef AT24C02_H
#define AT24C02_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f2xx_hal.h"

#define AT24C02_I2C_ADDR  (0x50u << 1)
#define AT24C02_SIZE     256

void AT24C02_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef AT24C02_Read(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef AT24C02_Write(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* AT24C02_H */
