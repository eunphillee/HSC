/**
 * @file at24c02.c
 * @brief AT24C02C over I2C3 (PA8/PC9). 1-byte address (0~255). Page write 8 bytes max.
 */
#include "at24c02.h"

#define AT24C02_PAGE_SIZE   8u
#define AT24C02_WRITE_MS    5u
#define AT24C02_ACK_POLL_MS 2u

static HAL_StatusTypeDef AT24C02_WaitReady(I2C_HandleTypeDef *hi2c)
{
	uint32_t t0 = HAL_GetTick();
	while (HAL_I2C_IsDeviceReady(hi2c, AT24C02_I2C_ADDR, 10, 50) != HAL_OK) {
		if ((HAL_GetTick() - t0) > 50u) return HAL_TIMEOUT;
		HAL_Delay(AT24C02_ACK_POLL_MS);
	}
	return HAL_OK;
}

HAL_StatusTypeDef AT24C02_Read(I2C_HandleTypeDef *hi2c, uint16_t addr, uint8_t *buf, uint16_t len)
{
	if (!hi2c || !buf) return HAL_ERROR;
	if (len == 0u) return HAL_OK;
	if (addr >= AT24C02_SIZE || (uint32_t)addr + (uint32_t)len > AT24C02_SIZE) return HAL_ERROR;
	uint8_t a = (uint8_t)(addr & 0xFF);
	return HAL_I2C_Mem_Read(hi2c, AT24C02_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT, buf, len, 50);
}

HAL_StatusTypeDef AT24C02_Write(I2C_HandleTypeDef *hi2c, uint16_t addr, const uint8_t *buf, uint16_t len)
{
	if (!hi2c || !buf) return HAL_ERROR;
	if (len == 0u) return HAL_OK;
	if (addr >= AT24C02_SIZE || (uint32_t)addr + (uint32_t)len > AT24C02_SIZE) return HAL_ERROR;

	uint16_t off = 0u;
	while (off < len) {
		uint16_t cur = (uint16_t)(addr + off);
		uint16_t page_start = (uint16_t)(cur & ~(AT24C02_PAGE_SIZE - 1u));
		uint16_t chunk = (uint16_t)(AT24C02_PAGE_SIZE - (cur - page_start));
		if (chunk > (uint16_t)(len - off)) chunk = (uint16_t)(len - off);

		uint8_t a = (uint8_t)(cur & 0xFFu);
		HAL_StatusTypeDef ret = HAL_I2C_Mem_Write(
			hi2c, AT24C02_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT, (uint8_t *)(buf + off), chunk, 50
		);
		if (ret != HAL_OK) return ret;

		off = (uint16_t)(off + chunk);
		HAL_Delay(AT24C02_WRITE_MS);
		if (off < len) {
			ret = AT24C02_WaitReady(hi2c);
			if (ret != HAL_OK) return ret;
		}
	}
	return HAL_OK;
}

void AT24C02_Init(I2C_HandleTypeDef *hi2c)
{
	(void)hi2c;
}
