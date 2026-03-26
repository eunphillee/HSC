/**
 * @file adc_app.h
 * @brief LPSB ADC storage interface (DMA values written by main via ACS712_RMS_Poll).
 */
#ifndef ADC_APP_H
#define ADC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LPSB_ADC_CHANNELS  3

/** ch 0=PA3(CH3), 1=PA4(CH4), 2=PA5(CH5). */
uint16_t LPSB_ADC_GetStoredAvg(uint8_t ch);
uint16_t LPSB_ADC_GetStoredPkpk(uint8_t ch);
void LPSB_ADC_SetStoredAvg(uint8_t ch, uint16_t avg);
void LPSB_ADC_SetStoredPkpk(uint8_t ch, uint16_t pkpk);

#ifdef __cplusplus
}
#endif

#endif /* ADC_APP_H */
