/**
 * @file lpsb_ct_adc.h
 * @brief LPSB: 3ch ACS712 (or same ADC pins) — AVG, PKPK, CURRENT ON/OFF.
 */
#ifndef LPSB_CT_ADC_H
#define LPSB_CT_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void LpsbCtAdc_Init(void);
void LpsbCtAdc_Poll(void);
void LpsbCtAdc_GetSnapshot(uint16_t avg[3], uint16_t pkpk[3], uint16_t cur_on[3]);

#ifdef __cplusplus
}
#endif

#endif /* LPSB_CT_ADC_H */
