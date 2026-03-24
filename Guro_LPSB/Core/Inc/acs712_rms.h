/**
 ******************************************************************************
 * @file    acs712_rms.h
 * @brief   ACS712-05B AC current RMS measurement using ADC+DMA+Timer trigger.
 *
 * Why offset must be removed:
 * - ACS712 output is centered at VCC/2. With this board's divider (0.6),
 *   the no-load center is ~1.45~1.50V at ADC input. This DC component must be
 *   removed to measure only the AC component.
 *
 * Why RMS must be used:
 * - AC current is proportional to the RMS of the waveform, not the average.
 *   Average of a symmetric AC waveform is ~0 after offset removal.
 *
 * Why sensitivity is 0.111 V/A:
 * - ACS712-05B sensitivity is 0.185 V/A at VOUT.
 * - Divider network R16=20k, R17=30k scales by 0.6 → 0.185 * 0.6 = 0.111 V/A.
 ******************************************************************************
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
  /* Legacy single-channel fields (kept for compatibility; now mirror ADC3). */
  float offset_v;     /**< measured DC offset at ADC input (V) (ADC3) */
  float last_irms_a;  /**< last computed RMS current (A) (ADC3) */
  uint16_t last_avg_adc;   /**< last block average (ADC counts) (ADC3) */
  uint16_t last_pkpk_adc;  /**< last block pkpk (ADC counts) (ADC3) */

  /* New: 3-channel scan+DMA results (ADC1/2/3 = PA3/4/5). */
  float offset_v_ch[3];       /**< DC offset per channel (V) */
  float last_irms_a_ch[3];    /**< RMS current per channel (A) */
  uint16_t last_avg_adc_ch[3];  /**< average ADC per channel (counts) */
  uint16_t last_pkpk_adc_ch[3]; /**< pkpk ADC per channel (counts) */
} ACS712_RmsState;

/** Configuration constants (board-specific) */
#define ACS712_ADC_VREF_V          (3.3f)
#define ACS712_ADC_FULL_SCALE_F    (4096.0f)      /* 12-bit */
#define ACS712_SENS_V_PER_A        (0.111f)       /* effective at ADC input */
#define ACS712_NOISE_FLOOR_A       (0.02f)        /* clamp below this */

/** Sampling configuration */
#define ACS712_DMA_BUF_LEN         (256u)         /* N samples per channel (128 or 256 recommended) */

/**
 * Initialize RMS module:
 * - ADC is expected to be configured in scan mode (CH3/CH4/CH5, forward)
 * - Start ADC DMA (circular, timer-triggered)
 *
 * @param hadc ADC handle used in project (e.g., hadc)
 * @param htim TRGO timer handle (e.g., htim3)
 * @param adc_channel ADC channel to sample (e.g., ADC_CHANNEL_4 for PA4)
 * @return HAL_OK on success
 */
HAL_StatusTypeDef ACS712_RMS_Start(ADC_HandleTypeDef *hadc,
                                  uint32_t adc_channel /* kept; ignored in scan mode */);

/** Stop ADC DMA + timer. */
void ACS712_RMS_Stop(ADC_HandleTypeDef *hadc);

/**
 * Handle DMA half/full complete events.
 * Call this from HAL_ADC_ConvHalfCpltCallback / HAL_ADC_ConvCpltCallback.
 *
 * @param half_index 0 for first half, 1 for second half
 */
void ACS712_RMS_OnDmaBlockReady(uint8_t half_index);

/**
 * Periodic processing (main loop):
 * - If a DMA block is ready, compute RMS current from that block.
 * - Offset is measured once at startup using the first full buffer.
 *
 * @param state state output
 * @return 1 if updated, 0 if no new data
 */
uint8_t ACS712_RMS_Poll(ACS712_RmsState *state);

/** Access DMA buffer (for debugging/printing if needed). */
const uint16_t *ACS712_RMS_GetBuffer(void);

#ifdef __cplusplus
}
#endif

#endif /* ACS712_RMS_H */

