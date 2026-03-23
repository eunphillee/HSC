/**
 * @file hpsb_ct_adc.h
 * @brief HPSB: 3ch CT ADC — AVG, peak-peak, CURRENT ON/OFF (PKPK threshold).
 *        Samples in main loop; Modbus input regs read cached values.
 */
#ifndef HPSB_CT_ADC_H
#define HPSB_CT_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void HpsbCtAdc_Init(void);
/** Call from main loop (~100 ms internal gate). */
void HpsbCtAdc_Poll(void);
/** Last computed values (for Modbus refresh). */
void HpsbCtAdc_GetSnapshot(uint16_t avg[3], uint16_t pkpk[3], uint16_t cur_on[3]);

#ifdef __cplusplus
}
#endif

#endif /* HPSB_CT_ADC_H */
