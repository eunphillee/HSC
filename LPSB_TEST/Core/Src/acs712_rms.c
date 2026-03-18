/**
 ******************************************************************************
 * @file    acs712_rms.c
 * @brief   ACS712-05B AC current RMS measurement using ADC+DMA+Timer TRGO.
 ******************************************************************************
 */
#include "acs712_rms.h"
#include <math.h>
#include <string.h>

/* --- Module globals (single instance; sufficient for board verification) --- */
static ADC_HandleTypeDef *s_hadc = NULL;

/* Interleaved scan DMA buffer: [ADC1,ADC2,ADC3, ADC1,ADC2,ADC3, ...] */
#define ACS712_DMA_TOTAL_LEN   (ACS712_DMA_BUF_LEN * 3u)
static uint16_t s_dma_buf[ACS712_DMA_TOTAL_LEN];
static volatile uint8_t s_half_ready = 0;
static volatile uint8_t s_full_ready = 0;
static uint8_t s_offset_done = 0;

static inline float adc_to_v(uint16_t adc)
{
  return ((float)adc * ACS712_ADC_VREF_V) / ACS712_ADC_FULL_SCALE_F;
}

static float measure_offset_v_1ch_interleaved(const uint16_t *buf, size_t samples_per_ch, size_t ch_index)
{
  float sum = 0.0f;
  for (size_t i = 0; i < samples_per_ch; i++)
  {
    sum += adc_to_v(buf[i * 3u + ch_index]);
  }
  return sum / (float)samples_per_ch;
}

static float calc_irms_a_1ch_interleaved(const uint16_t *buf, size_t samples_per_ch, size_t ch_index, float offset_v)
{
  float sum_sq = 0.0f;
  for (size_t i = 0; i < samples_per_ch; i++)
  {
    float v = adc_to_v(buf[i * 3u + ch_index]);
    float centered = v - offset_v;
    sum_sq += centered * centered;
  }
  float rms_v = sqrtf(sum_sq / (float)samples_per_ch);
  float a = rms_v / ACS712_SENS_V_PER_A;
  if (a < ACS712_NOISE_FLOOR_A) a = 0.0f;
  return a;
}

static uint16_t calc_avg_adc_1ch_interleaved(const uint16_t *buf, size_t samples_per_ch, size_t ch_index)
{
  uint32_t sum = 0;
  for (size_t i = 0; i < samples_per_ch; i++) sum += buf[i * 3u + ch_index];
  return (uint16_t)(sum / (uint32_t)samples_per_ch);
}

static uint16_t calc_pkpk_adc_1ch_interleaved(const uint16_t *buf, size_t samples_per_ch, size_t ch_index)
{
  uint16_t vmin = 0xFFFFu;
  uint16_t vmax = 0u;
  for (size_t i = 0; i < samples_per_ch; i++)
  {
    uint16_t v = buf[i * 3u + ch_index];
    if (v < vmin) vmin = v;
    if (v > vmax) vmax = v;
  }
  return (vmax >= vmin) ? (uint16_t)(vmax - vmin) : 0u;
}

HAL_StatusTypeDef ACS712_RMS_Start(ADC_HandleTypeDef *hadc,
                                  uint32_t adc_channel)
{
  if (hadc == NULL) return HAL_ERROR;
  s_hadc = hadc;

  (void)adc_channel; /* Scan mode config is expected to be done in MX_ADC_Init(). */

  /* Clear flags/state */
  memset((void *)s_dma_buf, 0, sizeof(s_dma_buf));
  s_half_ready = 0;
  s_full_ready = 0;
  s_offset_done = 0;

  if (HAL_ADC_Start_DMA(hadc, (uint32_t *)s_dma_buf, ACS712_DMA_TOTAL_LEN) != HAL_OK)
    return HAL_ERROR;

  return HAL_OK;
}

void ACS712_RMS_Stop(ADC_HandleTypeDef *hadc)
{
  if (hadc)
    (void)HAL_ADC_Stop_DMA(hadc);
}

void ACS712_RMS_OnDmaBlockReady(uint8_t half_index)
{
  if (half_index == 0)
    s_half_ready = 1;
  else
    s_full_ready = 1;
}

uint8_t ACS712_RMS_Poll(ACS712_RmsState *state)
{
  if (state == NULL) return 0;

  /* Offset measurement: wait until first full buffer is ready for stable average */
  if (!s_offset_done)
  {
    if (s_full_ready)
    {
      s_full_ready = 0;
      const size_t n = ACS712_DMA_BUF_LEN;
      for (size_t ch = 0; ch < 3u; ch++)
      {
        state->offset_v_ch[ch] = measure_offset_v_1ch_interleaved(s_dma_buf, n, ch);
        state->last_irms_a_ch[ch] = 0.0f;
        state->last_avg_adc_ch[ch] = calc_avg_adc_1ch_interleaved(s_dma_buf, n, ch);
        state->last_pkpk_adc_ch[ch] = calc_pkpk_adc_1ch_interleaved(s_dma_buf, n, ch);
      }
      /* Legacy mirrors: ADC3 (index 2) */
      state->offset_v = state->offset_v_ch[2];
      state->last_irms_a = 0.0f;
      state->last_avg_adc = state->last_avg_adc_ch[2];
      state->last_pkpk_adc = state->last_pkpk_adc_ch[2];
      s_offset_done = 1;
      return 1;
    }
    return 0;
  }

  /* Compute RMS from whichever half is ready (keeps latency low) */
  if (s_half_ready)
  {
    s_half_ready = 0;
    const uint16_t *blk = &s_dma_buf[0];
    const size_t n = ACS712_DMA_BUF_LEN / 2u;
    for (size_t ch = 0; ch < 3u; ch++)
    {
      state->last_irms_a_ch[ch] = calc_irms_a_1ch_interleaved(blk, n, ch, state->offset_v_ch[ch]);
      state->last_avg_adc_ch[ch] = calc_avg_adc_1ch_interleaved(blk, n, ch);
      state->last_pkpk_adc_ch[ch] = calc_pkpk_adc_1ch_interleaved(blk, n, ch);
    }
    /* Legacy mirrors: ADC3 (index 2) */
    state->offset_v = state->offset_v_ch[2];
    state->last_irms_a = state->last_irms_a_ch[2];
    state->last_avg_adc = state->last_avg_adc_ch[2];
    state->last_pkpk_adc = state->last_pkpk_adc_ch[2];
    return 1;
  }
  if (s_full_ready)
  {
    s_full_ready = 0;
    const uint16_t *blk = &s_dma_buf[ACS712_DMA_TOTAL_LEN / 2u];
    const size_t n = ACS712_DMA_BUF_LEN / 2u;
    for (size_t ch = 0; ch < 3u; ch++)
    {
      state->last_irms_a_ch[ch] = calc_irms_a_1ch_interleaved(blk, n, ch, state->offset_v_ch[ch]);
      state->last_avg_adc_ch[ch] = calc_avg_adc_1ch_interleaved(blk, n, ch);
      state->last_pkpk_adc_ch[ch] = calc_pkpk_adc_1ch_interleaved(blk, n, ch);
    }
    /* Legacy mirrors: ADC3 (index 2) */
    state->offset_v = state->offset_v_ch[2];
    state->last_irms_a = state->last_irms_a_ch[2];
    state->last_avg_adc = state->last_avg_adc_ch[2];
    state->last_pkpk_adc = state->last_pkpk_adc_ch[2];
    return 1;
  }
  return 0;
}

const uint16_t *ACS712_RMS_GetBuffer(void)
{
  return s_dma_buf;
}

