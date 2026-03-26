/**
 * @file adc_app.c
 * @brief LPSB ADC storage interface.
 *        AVG/PKPK values are written by main.c (via ACS712_RMS_Poll DMA path)
 *        and read by modbus_slave.c for FC04 response.
 */
#include "adc_app.h"

static uint16_t s_avg[LPSB_ADC_CHANNELS];
static uint16_t s_pkpk[LPSB_ADC_CHANNELS];

uint16_t LPSB_ADC_GetStoredAvg(uint8_t ch)
{
    if (ch >= LPSB_ADC_CHANNELS) return 0u;
    return s_avg[ch];
}

uint16_t LPSB_ADC_GetStoredPkpk(uint8_t ch)
{
    if (ch >= LPSB_ADC_CHANNELS) return 0u;
    return s_pkpk[ch];
}

void LPSB_ADC_SetStoredAvg(uint8_t ch, uint16_t avg)
{
    if (ch >= LPSB_ADC_CHANNELS) return;
    s_avg[ch] = avg;
}

void LPSB_ADC_SetStoredPkpk(uint8_t ch, uint16_t pkpk)
{
    if (ch >= LPSB_ADC_CHANNELS) return;
    s_pkpk[ch] = pkpk;
}
