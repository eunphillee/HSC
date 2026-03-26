/**
 * @file hpsb_ct_adc.c
 * @brief HPSB CT ADC — DMA + TIM3 4kHz 3ch scan (CH3/PA3, CH4/PA4, CH5/PA5).
 *
 * DMA buffer layout (interleaved, SCAN_DIRECTION_FORWARD):
 *   [CH3, CH4, CH5, CH3, CH4, CH5, ...]  (total = DMA_BUF_LEN * 3 entries)
 *
 * Half-complete callback → process first half (DMA_BUF_LEN/2 samples/ch).
 * Full-complete callback → process second half.
 * No float / RMS — only integer AVG and PKPK.
 * Current state: PKPK >= threshold → I=ON.
 */
#include "hpsb_ct_adc.h"
#include <string.h>
#include <stddef.h>

/* 256 samples per channel per half-block (= 128 per half-complete event). */
#define HPSB_DMA_BUF_LEN      256u
#define HPSB_DMA_TOTAL_LEN    (HPSB_DMA_BUF_LEN * 3u)

/** PKPK (ADC counts, 12-bit) above this → current ON. Tune to hardware. */
#ifndef HPSB_CT_PKPK_ON_THRESHOLD
#define HPSB_CT_PKPK_ON_THRESHOLD  64u
#endif

static ADC_HandleTypeDef *s_hadc     = NULL;
static uint16_t  s_dma_buf[HPSB_DMA_TOTAL_LEN];
static volatile uint8_t s_half_ready = 0u;
static volatile uint8_t s_full_ready = 0u;
static uint8_t   s_started           = 0u;

static uint16_t s_avg[3];
static uint16_t s_pkpk[3];
static uint16_t s_cur[3];

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

static void process_block(const uint16_t *blk, size_t n)
{
    for (size_t ch = 0u; ch < 3u; ch++) {
        s_avg[ch]  = calc_avg(blk, n, ch);
        s_pkpk[ch] = calc_pkpk(blk, n, ch);
        s_cur[ch]  = (s_pkpk[ch] >= HPSB_CT_PKPK_ON_THRESHOLD) ? 1u : 0u;
    }
}

/* ---- public API ---- */

void HpsbCtAdc_Start(ADC_HandleTypeDef *hadc)
{
    s_hadc = hadc;
    memset(s_dma_buf, 0, sizeof(s_dma_buf));
    s_half_ready = 0u;
    s_full_ready = 0u;
    s_started    = 0u;
    for (size_t i = 0u; i < 3u; i++) { s_avg[i] = 0u; s_pkpk[i] = 0u; s_cur[i] = 0u; }

    (void)HAL_ADCEx_Calibration_Start(hadc);
    if (HAL_ADC_Start_DMA(hadc, (uint32_t *)s_dma_buf, HPSB_DMA_TOTAL_LEN) == HAL_OK) {
        s_started = 1u;
    }
}

void HpsbCtAdc_OnDmaBlockReady(uint8_t half_index)
{
    if (half_index == 0u) s_half_ready = 1u;
    else                  s_full_ready = 1u;
}

void HpsbCtAdc_Update(void)
{
    if (!s_started) return;

    /* samples per channel per half-block */
    const size_t n = HPSB_DMA_BUF_LEN / 2u;

    if (s_half_ready) {
        s_half_ready = 0u;
        process_block(&s_dma_buf[0], n);
    } else if (s_full_ready) {
        s_full_ready = 0u;
        process_block(&s_dma_buf[HPSB_DMA_TOTAL_LEN / 2u], n);
    }
}

void HpsbCtAdc_GetSnapshot(uint16_t avg[3], uint16_t pkpk[3], uint16_t cur_on[3])
{
    for (unsigned i = 0u; i < 3u; i++) {
        if (avg)    avg[i]    = s_avg[i];
        if (pkpk)   pkpk[i]   = s_pkpk[i];
        if (cur_on) cur_on[i] = s_cur[i];
    }
}
