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

static uint16_t s_dma_buf[ACS712_DMA_BUF_LEN];
static volatile uint8_t s_half_ready = 0;
static volatile uint8_t s_full_ready = 0;
static uint8_t s_offset_done = 0;

static inline float adc_to_v(uint16_t adc)
{
  return ((float)adc * ACS712_ADC_VREF_V) / ACS712_ADC_FULL_SCALE_F;
}

static float measure_offset_v(const uint16_t *buf, size_t n)
{
  float sum = 0.0f;
  for (size_t i = 0; i < n; i++)
  {
    sum += adc_to_v(buf[i]);
  }
  return sum / (float)n;
}

static float calc_irms_a(const uint16_t *buf, size_t n, float offset_v)
{
  float sum_sq = 0.0f;
  for (size_t i = 0; i < n; i++)
  {
    float v = adc_to_v(buf[i]);
    float centered = v - offset_v;
    sum_sq += centered * centered;
  }
  float rms_v = sqrtf(sum_sq / (float)n);
  float a = rms_v / ACS712_SENS_V_PER_A;
  if (a < ACS712_NOISE_FLOOR_A) a = 0.0f;
  return a;
}

static uint16_t calc_avg_adc(const uint16_t *buf, size_t n)
{
  uint32_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += buf[i];
  return (uint16_t)(sum / (uint32_t)n);
}

static uint16_t calc_pkpk_adc(const uint16_t *buf, size_t n)
{
  uint16_t vmin = 0xFFFFu;
  uint16_t vmax = 0u;
  for (size_t i = 0; i < n; i++)
  {
    uint16_t v = buf[i];
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

  /* Configure single ADC channel for DMA sampling */
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = adc_channel;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  /* Give enough sampling time for sensor output / divider RC */
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK)
    return HAL_ERROR;

  /* Clear flags/state */
  memset((void *)s_dma_buf, 0, sizeof(s_dma_buf));
  s_half_ready = 0;
  s_full_ready = 0;
  s_offset_done = 0;

  if (HAL_ADC_Start_DMA(hadc, (uint32_t *)s_dma_buf, ACS712_DMA_BUF_LEN) != HAL_OK)
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
      state->offset_v = measure_offset_v(s_dma_buf, ACS712_DMA_BUF_LEN);
      state->last_irms_a = 0.0f;
      state->last_avg_adc = calc_avg_adc(s_dma_buf, ACS712_DMA_BUF_LEN);
      state->last_pkpk_adc = calc_pkpk_adc(s_dma_buf, ACS712_DMA_BUF_LEN);
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
    size_t n = ACS712_DMA_BUF_LEN / 2u;
    state->last_irms_a = calc_irms_a(blk, n, state->offset_v);
    state->last_avg_adc = calc_avg_adc(blk, n);
    state->last_pkpk_adc = calc_pkpk_adc(blk, n);
    return 1;
  }
  if (s_full_ready)
  {
    s_full_ready = 0;
    const uint16_t *blk = &s_dma_buf[ACS712_DMA_BUF_LEN / 2u];
    size_t n = ACS712_DMA_BUF_LEN / 2u;
    state->last_irms_a = calc_irms_a(blk, n, state->offset_v);
    state->last_avg_adc = calc_avg_adc(blk, n);
    state->last_pkpk_adc = calc_pkpk_adc(blk, n);
    return 1;
  }
  return 0;
}

const uint16_t *ACS712_RMS_GetBuffer(void)
{
  return s_dma_buf;
}

