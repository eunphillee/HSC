/**
 * @file eeprom_24c02.c
 * @brief IO layer for AT24C02C (256 bytes, I2C3). Page-boundary split, write-cycle wait/ACK poll.
 */
#include "eeprom_24c02.h"
#include "main.h"
#include <string.h>

/* AT24C02C: 256 bytes, 8-byte page. I2C addr 0x50 (7-bit) -> 0xA0 for HAL. */
#define EEPROM_I2C_ADDR   (0x50u << 1)
#define EEPROM_SIZE       256u
#define EEPROM_PAGE_SIZE  8u
#define EEPROM_WRITE_MS   5u
#define EEPROM_POLL_MS    2u

extern I2C_HandleTypeDef hi2c3;

static int eeprom_wait_ready(void)
{
	uint32_t t0 = HAL_GetTick();
	while (HAL_I2C_IsDeviceReady(&hi2c3, EEPROM_I2C_ADDR, 10, 50) != HAL_OK) {
		if ((HAL_GetTick() - t0) > 50u) return -1;
		HAL_Delay(EEPROM_POLL_MS);
	}
	return 0;
}

int EEPROM_Read(uint16_t addr, uint8_t *buf, uint16_t len)
{
	if (buf == NULL) return -1;
	if (len == 0) return 0;
	if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) return -1;

	uint8_t a = (uint8_t)(addr & 0xFF);
	if (HAL_I2C_Mem_Read(&hi2c3, EEPROM_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT, buf, len, 100) != HAL_OK)
		return -1;
	return 0;
}

int EEPROM_Write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
	if (buf == NULL) return -1;
	if (len == 0) return 0;
	if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) return -1;

	uint16_t off = 0;
	while (off < len) {
		uint16_t page_start = (addr + off) & ~(EEPROM_PAGE_SIZE - 1);
		uint16_t chunk = EEPROM_PAGE_SIZE - (uint16_t)((addr + off) - page_start);
		if (chunk > (len - off)) chunk = len - off;

		uint8_t a = (uint8_t)((addr + off) & 0xFF);
		if (HAL_I2C_Mem_Write(&hi2c3, EEPROM_I2C_ADDR, a, I2C_MEMADD_SIZE_8BIT,
		                      (uint8_t *)(buf + off), chunk, 100) != HAL_OK)
			return -1;
		off += chunk;
		HAL_Delay(EEPROM_WRITE_MS);
		if (off < len && eeprom_wait_ready() != 0) return -1;
	}
	return 0;
}

#define EEPROM_SELFTEST_BASE  0x40u   /* outside config A/B (0x00~0x3F) */
#define EEPROM_SELFTEST_SIZE  16u     /* 4 patterns × 4 bytes or similar */

int EEPROM_SelfTest(void)
{
	uint8_t wr[EEPROM_SELFTEST_SIZE];
	uint8_t rd[EEPROM_SELFTEST_SIZE];
	const uint8_t patterns[] = { 0x55, 0xAA, 0x00, 0xFF };
	const unsigned npat = sizeof(patterns);

	for (unsigned i = 0; i < npat; i++) {
		memset(wr, patterns[i], EEPROM_SELFTEST_SIZE);
		if (EEPROM_Write(EEPROM_SELFTEST_BASE, wr, EEPROM_SELFTEST_SIZE) != 0)
			return -1;
		if (EEPROM_Read(EEPROM_SELFTEST_BASE, rd, EEPROM_SELFTEST_SIZE) != 0)
			return -1;
		if (memcmp(wr, rd, EEPROM_SELFTEST_SIZE) != 0)
			return -1;
	}
	memset(wr, 0xFF, EEPROM_SELFTEST_SIZE);
	if (EEPROM_Write(EEPROM_SELFTEST_BASE, wr, EEPROM_SELFTEST_SIZE) != 0)
		return -1;
	return 0;
}
