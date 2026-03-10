/**
 * @file shtc3.c
 * @brief SHTC3 over I2C1 (PB6/PB7). Use hi2c = I2C1 handle only.
 */
#include "shtc3.h"

#define SHTC3_CMD_WAKEUP  0x3517
#define SHTC3_CMD_MEASURE 0x7866

static I2C_HandleTypeDef *shtc3_i2c;

void SHTC3_Init(I2C_HandleTypeDef *hi2c)
{
	shtc3_i2c = hi2c;
	uint8_t w[2] = { (uint8_t)(SHTC3_CMD_WAKEUP >> 8), (uint8_t)(SHTC3_CMD_WAKEUP & 0xFF) };
	(void)HAL_I2C_Master_Transmit(shtc3_i2c, SHTC3_I2C_ADDR, w, 2, 10);
	HAL_Delay(1);
}

int8_t SHTC3_Measure(I2C_HandleTypeDef *hi2c, float *temp_c, float *rh_pct)
{
	uint8_t w[2] = { (uint8_t)(SHTC3_CMD_MEASURE >> 8), (uint8_t)(SHTC3_CMD_MEASURE & 0xFF) };
	uint8_t r[6];

	if (HAL_I2C_Master_Transmit(hi2c, SHTC3_I2C_ADDR, w, 2, 10) != HAL_OK) return -1;
	HAL_Delay(15);
	if (HAL_I2C_Master_Receive(hi2c, SHTC3_I2C_ADDR, r, 6, 20) != HAL_OK) return -1;

	uint16_t raw_t = (uint16_t)((r[0] << 8) | r[1]);
	uint16_t raw_rh = (uint16_t)((r[3] << 8) | r[4]);

	if (temp_c) *temp_c = -45.0f + 175.0f * (float)raw_t / 65535.0f;
	if (rh_pct) *rh_pct = 100.0f * (float)raw_rh / 65535.0f;
	return 0;
}

