/**
 * @file lpsb_ct_adc.c
 * @brief Same algorithm as HPSB: N samples → AVG, PKPK; CURRENT from PKPK threshold.
 */
#include "lpsb_ct_adc.h"
#include "main.h"
#include "stm32f0xx_hal_adc_ex.h"

extern ADC_HandleTypeDef hadc;

#ifndef LPSB_CT_ADC_SAMPLES
#define LPSB_CT_ADC_SAMPLES  48u
#endif
#ifndef LPSB_CT_ADC_UPDATE_MS
#define LPSB_CT_ADC_UPDATE_MS  100u
#endif
#ifndef LPSB_CT_PKPK_ON_THRESHOLD
#define LPSB_CT_PKPK_ON_THRESHOLD  64u
#endif

static const uint32_t s_adc_ch[3] = {
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5
};

static uint16_t s_avg[3];
static uint16_t s_pkpk[3];
static uint16_t s_cur[3];
static uint32_t s_last_ms;
static uint8_t s_inited;

static void sample_channel(unsigned idx)
{
    if (idx >= 3u) return;

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = s_adc_ch[idx];
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
        return;

    uint32_t sum = 0u;
    uint16_t vmin = 4095u;
    uint16_t vmax = 0u;

    for (unsigned n = 0u; n < LPSB_CT_ADC_SAMPLES; n++) {
        if (HAL_ADC_Start(&hadc) != HAL_OK)
            break;
        if (HAL_ADC_PollForConversion(&hadc, 100u) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc);
            continue;
        }
        uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc);
        (void)HAL_ADC_Stop(&hadc);
        sum += v;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }

    s_avg[idx] = (uint16_t)(sum / LPSB_CT_ADC_SAMPLES);
    s_pkpk[idx] = (vmax > vmin) ? (uint16_t)(vmax - vmin) : 0u;
    s_cur[idx] = (s_pkpk[idx] >= LPSB_CT_PKPK_ON_THRESHOLD) ? 1u : 0u;
}

void LpsbCtAdc_Init(void)
{
    s_last_ms = 0u;
    s_inited = 0u;
    for (unsigned i = 0u; i < 3u; i++) {
        s_avg[i] = 0u;
        s_pkpk[i] = 0u;
        s_cur[i] = 0u;
    }
    (void)HAL_ADCEx_Calibration_Start(&hadc);
    s_inited = 1u;
}

void LpsbCtAdc_Poll(void)
{
    if (!s_inited) return;
    uint32_t now = HAL_GetTick();
    if ((now - s_last_ms) < LPSB_CT_ADC_UPDATE_MS)
        return;
    s_last_ms = now;

    for (unsigned i = 0u; i < 3u; i++)
        sample_channel(i);
}

void LpsbCtAdc_GetSnapshot(uint16_t avg[3], uint16_t pkpk[3], uint16_t cur_on[3])
{
    for (unsigned i = 0u; i < 3u; i++) {
        if (avg) avg[i] = s_avg[i];
        if (pkpk) pkpk[i] = s_pkpk[i];
        if (cur_on) cur_on[i] = s_cur[i];
    }
}
