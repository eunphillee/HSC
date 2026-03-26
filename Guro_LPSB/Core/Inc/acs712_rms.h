/**
 * @file acs712_rms.h
 * @brief LPSB ADC DMA: TIM3 4kHz 3ch scan, integer AVG + PKPK (no float RMS).
 *        DMA buffer layout: [CH3, CH4, CH5, CH3, CH4, CH5, ...] interleaved.
 */
#ifndef ACS712_RMS_H
#define ACS712_RMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint16_t last_avg_adc_ch[3];   /**< average ADC per channel (counts), ch0=PA3, ch1=PA4, ch2=PA5 */
    uint16_t last_pkpk_adc_ch[3];  /**< peak-to-peak ADC per channel (counts) */
} ACS712_RmsState;

/** Samples per channel per full DMA buffer (half-complete = BUF_LEN/2 samples/ch). */
#define ACS712_DMA_BUF_LEN   256u

/** Start ADC DMA circular sampling (ADC must already be configured in scan mode). */
HAL_StatusTypeDef ACS712_RMS_Start(ADC_HandleTypeDef *hadc, uint32_t adc_channel);

/** Stop ADC DMA. */
void ACS712_RMS_Stop(ADC_HandleTypeDef *hadc);

/** Call from HAL_ADC_ConvHalfCpltCallback (half_index=0) / ConvCpltCallback (half_index=1). */
void ACS712_RMS_OnDmaBlockReady(uint8_t half_index);

/**
 * Main-loop poll: if a DMA block is ready, compute AVG+PKPK and update state.
 * @return 1 if updated, 0 if no new data.
 */
uint8_t ACS712_RMS_Poll(ACS712_RmsState *state);

#ifdef __cplusplus
}
#endif

#endif /* ACS712_RMS_H */
