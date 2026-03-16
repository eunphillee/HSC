/**
 * @file at24c02.c
 * @brief AT24C02C over I2C3 (PA8/PC9). 1-byte address (0~255). Page write 8 bytes max.
 */
#include "at24c02.h"

HAL_StatusTypeDef AT24C02_Read(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *buf, uint16_t len)
{
	if (addr >= AT24C02_SIZE || !buf) return HAL_ERROR;
	uint8_t a = (uint8_t)(addr & 0xFF);
	return HAL_I2C_Mem_Read(hi2c, AT24C02_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT, buf, len, 50);
}

HAL_StatusTypeDef AT24C02_Write(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *buf, uint16_t len)
{
	if (addr >= AT24C02_SIZE || !buf) return HAL_ERROR;
	uint8_t a = (uint8_t)(addr & 0xFF);
	HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(hi2c, AT24C02_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT, buf, len, 50);
	HAL_Delay(5);
	return ret;
}

void AT24C02_Init(I2C_HandleTypeDef *hi2c)
{
	(void)hi2c;
}
