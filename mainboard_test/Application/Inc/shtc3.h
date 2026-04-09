/**
 * @file shtc3.h
 * @brief SHTC3 temperature/humidity sensor driver. HW fixed:
 *        - I2C1 (PB6=SCL, PB7=SDA) only
 */
#ifndef SHTC3_H
#define SHTC3_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f2xx_hal.h"

#define SHTC3_I2C_ADDR  (0x70u << 1)

void  SHTC3_Init(I2C_HandleTypeDef *hi2c);
int8_t SHTC3_Measure(I2C_HandleTypeDef *hi2c, float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif

#endif /* SHTC3_H */

