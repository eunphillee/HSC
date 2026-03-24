/**
 ******************************************************************************
 * @file    adc_app.c
 * @brief   LPSB ADC 3채널(PA3/4/5) raw 및 평균. hadc는 외부(MX_ADC) 제공.
 ******************************************************************************
 */
#include "adc_app.h"
#include "main.h"

extern ADC_HandleTypeDef hadc;

#define ADC_AVG_SAMPLES  8
#define ADC_POLL_MS      2
/* PKPK 측정 시 더 많은 샘플로 AC 변화를 잡는다. */
#define ADC_PKPK_SAMPLES  32

static uint16_t s_avg[LPSB_ADC_CHANNELS];
static uint16_t s_pkpk[LPSB_ADC_CHANNELS];

static void read_three_channels(uint16_t *out)
{
  if (out == NULL) return;
  /* STM32F0: HAL_ADC_ConfigChannel()은 채널을 누적(OR)하지 않고 교체할 수 있다.
   * 따라서 "스캔 1회 후 3회 Poll" 방식은 동일 채널 값만 3번 읽는 문제가 생길 수 있어
   * 채널을 명시적으로 3→4→5로 설정 후 단일 변환으로 읽는다.
   */
  static const uint32_t ch_list[3] = {ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_5};
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  /* ACS712 출력은 RC/소스 임피던스 영향이 있을 수 있어 샘플링 타임을 충분히 준다. */
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;

  for (int i = 0; i < 3; i++)
  {
    sConfig.Channel = ch_list[i];
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
      out[i] = 0;
      continue;
    }
    (void)HAL_ADC_Start(&hadc);
    (void)HAL_ADC_PollForConversion(&hadc, ADC_POLL_MS);
    out[i] = (uint16_t)HAL_ADC_GetValue(&hadc);
    (void)HAL_ADC_Stop(&hadc);
  }
}

uint16_t LPSB_ADC_ReadRaw(uint8_t ch)
{
  if (ch >= LPSB_ADC_CHANNELS) return 0;
  uint16_t three[3];
  read_three_channels(three);
  return three[ch];
}

void LPSB_ADC_ReadThreeRaw(uint16_t *out)
{
  read_three_channels(out);
}

uint16_t LPSB_ADC_GetRawAvg(uint8_t ch)
{
  if (ch >= LPSB_ADC_CHANNELS) return 0;
  uint32_t sum = 0;
  for (int i = 0; i < ADC_AVG_SAMPLES; i++)
  {
    sum += LPSB_ADC_ReadRaw(ch);
  }
  return (uint16_t)(sum / ADC_AVG_SAMPLES);
}

void LPSB_ADC_Update(void)
{
  uint32_t sum[3] = {0, 0, 0};
  uint16_t vmin[3] = {0xFFFFu, 0xFFFFu, 0xFFFFu};
  uint16_t vmax[3] = {0u, 0u, 0u};
  for (int n = 0; n < ADC_PKPK_SAMPLES; n++)
  {
    uint16_t three[3];
    read_three_channels(three);
    if (n < ADC_AVG_SAMPLES)
    {
      sum[0] += three[0];
      sum[1] += three[1];
      sum[2] += three[2];
    }
    for (int ch = 0; ch < 3; ch++)
    {
      uint16_t v = three[ch];
      if (v < vmin[ch]) vmin[ch] = v;
      if (v > vmax[ch]) vmax[ch] = v;
    }
  }
  s_avg[0] = (uint16_t)(sum[0] / ADC_AVG_SAMPLES);
  s_avg[1] = (uint16_t)(sum[1] / ADC_AVG_SAMPLES);
  s_avg[2] = (uint16_t)(sum[2] / ADC_AVG_SAMPLES);
  for (int ch = 0; ch < 3; ch++)
  {
    s_pkpk[ch] = (vmax[ch] >= vmin[ch]) ? (uint16_t)(vmax[ch] - vmin[ch]) : 0u;
  }
}

uint16_t LPSB_ADC_GetStoredAvg(uint8_t ch)
{
  if (ch >= LPSB_ADC_CHANNELS) return 0;
  return s_avg[ch];
}

uint16_t LPSB_ADC_GetStoredPkpk(uint8_t ch)
{
  if (ch >= LPSB_ADC_CHANNELS) return 0;
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
