/**
 * @file hpsb_ct_adc.h
 * @brief HPSB CT ADC: DMA + TIM3 3ch scan, integer AVG + PKPK, current ON/OFF.
 */
#ifndef HPSB_CT_ADC_H
#define HPSB_CT_ADC_H

#include "stm32f0xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Calibrate ADC and start DMA circular sampling (call once at startup). */
void HpsbCtAdc_Start(ADC_HandleTypeDef *hadc);

/** Call from HAL_ADC_ConvHalfCpltCallback (half_index=0) / ConvCpltCallback (half_index=1). */
void HpsbCtAdc_OnDmaBlockReady(uint8_t half_index);

/** Call from main loop: process any pending DMA block and update cached values. */
void HpsbCtAdc_Update(void);

/** Return last computed values (for Modbus register refresh). */
void HpsbCtAdc_GetSnapshot(uint16_t avg[3], uint16_t pkpk[3], uint16_t cur_on[3]);

#ifdef __cplusplus
}
#endif

#endif /* HPSB_CT_ADC_H */
