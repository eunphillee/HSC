/**
 * @file acs712_rms.c
 * @brief LPSB ADC DMA: TIM3 4kHz 3ch scan, integer AVG + PKPK.
 *        No float / RMS — pure integer computation.
 *
 * DMA buffer layout (SCAN_DIRECTION_FORWARD, CH3→CH4→CH5):
 *   [CH3, CH4, CH5, CH3, CH4, CH5, ...]  (total = ACS712_DMA_BUF_LEN * 3)
 *
 * Half-complete (half_index=0) → process buf[0 .. TOTAL/2-1] (BUF_LEN/2 samples/ch).
 * Full-complete  (half_index=1) → process buf[TOTAL/2 .. TOTAL-1].
 */
#include "acs712_rms.h"
#include <string.h>

#define ACS712_DMA_TOTAL_LEN  (ACS712_DMA_BUF_LEN * 3u)

static ADC_HandleTypeDef  *s_hadc      = NULL;
static uint16_t  s_dma_buf[ACS712_DMA_TOTAL_LEN];
static volatile uint8_t s_half_ready   = 0u;
static volatile uint8_t s_full_ready   = 0u;

/* ---- private helpers ---- */

static uint16_t calc_avg(const uint16_t *buf, size_t n, size_t ch)
{
    uint32_t sum = 0u;
    for (size_t i = 0u; i < n; i++) sum += buf[i * 3u + ch];
    return (uint16_t)(sum / (uint32_t)n);
}

static uint16_t calc_pkpk(const uint16_t *buf, size_t n, size_t ch)
{
    uint16_t vmin = 0xFFFFu, vmax = 0u;
    for (size_t i = 0u; i < n; i++) {
        uint16_t v = buf[i * 3u + ch];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    return (vmax >= vmin) ? (uint16_t)(vmax - vmin) : 0u;
}

static void process_block(ACS712_RmsState *state, const uint16_t *blk, size_t n)
{
    for (size_t ch = 0u; ch < 3u; ch++) {
        state->last_avg_adc_ch[ch]  = calc_avg(blk, n, ch);
        state->last_pkpk_adc_ch[ch] = calc_pkpk(blk, n, ch);
    }
}

/* ---- public API ---- */

HAL_StatusTypeDef ACS712_RMS_Start(ADC_HandleTypeDef *hadc, uint32_t adc_channel)
{
    if (hadc == NULL) return HAL_ERROR;
    s_hadc = hadc;
    (void)adc_channel; /* Scan mode configured in MX_ADC_Init. */

    memset((void *)s_dma_buf, 0, sizeof(s_dma_buf));
    s_half_ready = 0u;
    s_full_ready = 0u;

    return HAL_ADC_Start_DMA(hadc, (uint32_t *)s_dma_buf, ACS712_DMA_TOTAL_LEN);
}

void ACS712_RMS_Stop(ADC_HandleTypeDef *hadc)
{
    if (hadc) (void)HAL_ADC_Stop_DMA(hadc);
}

void ACS712_RMS_OnDmaBlockReady(uint8_t half_index)
{
    if (half_index == 0u) s_half_ready = 1u;
    else                  s_full_ready = 1u;
}

uint8_t ACS712_RMS_Poll(ACS712_RmsState *state)
{
    if (state == NULL) return 0u;

    const size_t n = ACS712_DMA_BUF_LEN / 2u;  /* samples per channel per half-block */

    if (s_half_ready) {
        s_half_ready = 0u;
        process_block(state, &s_dma_buf[0], n);
        return 1u;
    }
    if (s_full_ready) {
        s_full_ready = 0u;
        process_block(state, &s_dma_buf[ACS712_DMA_TOTAL_LEN / 2u], n);
        return 1u;
    }
    return 0u;
}
