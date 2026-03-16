/**
 ******************************************************************************
 * @file    ct_current.c
 * @brief   CT 전류 환산: 오프셋 수집 → RMS(ADC) → 캘리브레이션 → 전류(A)
 *          HCT17W + R19(18Ω) + 1.65V 바이어스 회로. 50Hz 가정.
 ******************************************************************************
 */
#include "ct_current.h"
#include <math.h>

/* CT_CUR_SAMPLES 미정의/잘못된 헤더 사용 시 빌드 실패로 즉시 확인 */
_Static_assert(CT_CUR_SAMPLES == 64, "CT_CUR_SAMPLES must be 64 (check which ct_current.h is included)");

/* 무부하 DC 오프셋 (ADC LSB). 부팅 후 CT_OFFSET_COLLECT_MS 동안 수집 */
static uint16_t s_offset[CT_CHANNELS];
static uint8_t  s_offset_done;

/* 오프셋 수집용 누적값 / 카운트 */
static uint32_t s_offset_sum[CT_CHANNELS];
static uint16_t s_offset_count;
static uint32_t s_offset_start_tick;

/* 캘리브레이션 계수 I[A] = k * rms_adc. 4.77A, A1_RMS≈280 기준 K≈0.017 */
static float s_cal_k[CT_CHANNELS] = { 0.017f, 0.f, 0.f };

/* 1.65V 기준 ADC LSB (오프셋 무효 시 사용) */
#define ADC_OFFSET_1V65_LSB  2048U
#define ADC_OFFSET_MIN       500U
#define ADC_OFFSET_MAX       3700U

/**
 * @brief 채널 전환 후 샘플링 캐패시터 정착을 위한 짧은 대기 (수 us)
 */
static void adc_channel_switch_delay(void)
{
  for (volatile uint32_t i = 0; i < 50; i++)
    (void)i;
}

/**
 * @brief ADC 3채널 읽기 (CH3→CH4→CH5). 더미 스캔 1회 후 실제 값 사용.
 *        out[0]=TC_ADC01(PA3/CH3), out[1]=TC_ADC02(PA4/CH4), out[2]=TC_ADC03(PA5/CH5).
 *        채널 전환 시 이전 채널 전압이 따라가는 크로스토크 감소 목적.
 */
static void read_three_channels(ADC_HandleTypeDef *hadc, uint16_t *out)
{
  if (hadc == NULL || out == NULL) return;

  /* ADC 폴 타임아웃 2ms: 239.5사이클@14MHz≈17us, 2ms로 1초 루프·60Hz 샘플링 유지 */
  const uint32_t adc_poll_ms = 2;
  /* 1) 더미 스캔 1회: 첫 샘플 버리고 샘플링 캐패시터 정착 */
  HAL_ADC_Start(hadc);
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  (void)HAL_ADC_GetValue(hadc);
  adc_channel_switch_delay();
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  (void)HAL_ADC_GetValue(hadc);
  adc_channel_switch_delay();
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  (void)HAL_ADC_GetValue(hadc);
  adc_channel_switch_delay();

  /* 2) 실제 스캔: CH3 → CH4 → CH5 순서로 사용할 값 읽기 */
  HAL_ADC_Start(hadc);
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  out[0] = (uint16_t)HAL_ADC_GetValue(hadc);
  adc_channel_switch_delay();
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  out[1] = (uint16_t)HAL_ADC_GetValue(hadc);
  adc_channel_switch_delay();
  HAL_ADC_PollForConversion(hadc, adc_poll_ms);
  out[2] = (uint16_t)HAL_ADC_GetValue(hadc);
}

uint8_t CT_IsOffsetDone(void)
{
  return s_offset_done;
}

uint8_t CT_Offset_Collect(ADC_HandleTypeDef *hadc)
{
  if (hadc == NULL) return 0;

  if (!s_offset_done && s_offset_count == 0)
    s_offset_start_tick = HAL_GetTick();

  /* 수집 시간 초과 시 평균 계산 후 완료 */
  if (!s_offset_done && (HAL_GetTick() - s_offset_start_tick) >= CT_OFFSET_COLLECT_MS)
  {
    if (s_offset_count > 0)
    {
      for (int i = 0; i < CT_CHANNELS; i++)
      {
        uint16_t v = (uint16_t)(s_offset_sum[i] / s_offset_count);
        /* 무부하 1.65V(약 2048) 범위가 아니면 기본값 사용 (과대 RMS 방지) */
        if (v < ADC_OFFSET_MIN || v > ADC_OFFSET_MAX)
          v = (uint16_t)ADC_OFFSET_1V65_LSB;
        s_offset[i] = v;
      }
    }
    else
    {
      /* 샘플 없이 종료 시 전 채널 1.65V 기준으로 고정 */
      for (int i = 0; i < CT_CHANNELS; i++)
        s_offset[i] = (uint16_t)ADC_OFFSET_1V65_LSB;
    }
    s_offset_done = 1;
    return 1;
  }

  if (s_offset_done)
    return 1;

  /* CT_OFFSET_SAMPLE_MS 간격으로만 샘플 (첫 호출 시점부터) */
  static uint32_t last_sample_tick;
  uint32_t now = HAL_GetTick();
  if (s_offset_count > 0 && (now - last_sample_tick) < (uint32_t)CT_OFFSET_SAMPLE_MS)
    return 0;
  last_sample_tick = now;

  uint16_t raw[3];
  read_three_channels(hadc, raw);
  for (int i = 0; i < CT_CHANNELS; i++)
    s_offset_sum[i] += raw[i];
  s_offset_count++;
  return 0;
}

uint8_t CT_ReadChannelRMS(ADC_HandleTypeDef *hadc, uint8_t ch,
                          uint16_t *rms_adc, uint16_t *raw_avg)
{
  if (hadc == NULL || !s_offset_done || ch >= CT_CHANNELS || rms_adc == NULL)
    return 0;

  uint16_t samples[CT_RMS_SAMPLES];
  uint32_t sum = 0;

  for (int i = 0; i < CT_RMS_SAMPLES; i++)
  {
    uint16_t three[3];
    read_three_channels(hadc, three);
    samples[i] = three[ch];
    sum += samples[i];
  }

  if (raw_avg)
    *raw_avg = (uint16_t)(sum / CT_RMS_SAMPLES);

  /* AC 성분 제거 후 RMS: rms = sqrt( (1/N) * sum( (s[i]-offset)^2 ) ) */
  uint32_t sum_sq = 0;
  uint16_t off = s_offset[ch];
  if (off < ADC_OFFSET_MIN || off > ADC_OFFSET_MAX)
    off = (uint16_t)ADC_OFFSET_1V65_LSB;
  for (int i = 0; i < CT_RMS_SAMPLES; i++)
  {
    int32_t ac = (int32_t)samples[i] - (int32_t)off;
    sum_sq += (uint32_t)(ac * ac);
  }
  /* sqrt(sum_sq / N). 부동소수 연산 후 LSB로 반올림 */
  float rms_f = sqrtf((float)sum_sq / (float)CT_RMS_SAMPLES);
  *rms_adc = (uint16_t)(rms_f + 0.5f);
  return 1;
}

float CT_ADC_RMS_to_Ampere(uint16_t rms_adc, float cal_k)
{
  return cal_k * (float)rms_adc;
}

void CT_SetCalibration(uint8_t ch, float k)
{
  if (ch < CT_CHANNELS)
    s_cal_k[ch] = k;
}

float CT_GetCalibration(uint8_t ch)
{
  if (ch >= CT_CHANNELS) return 0.f;
  return s_cal_k[ch];
}

/* ----- 전류 유무 판정: 3채널 스캔 첫 값(ADC1) 64샘플 → AVG, RMS, PKPK, CUR (78~80 OFF / 80 이상 ON) ----- */
static uint8_t s_cur_state;   /* 0=OFF, 1=ON */
static uint8_t s_cur_count;   /* 연속 ON 또는 OFF 후보 횟수 */

uint8_t CT_CurrentDetect_ADC1(ADC_HandleTypeDef *hadc,
                              uint16_t *avg_out, uint16_t *rms_out, uint16_t *pkpk_out,
                              uint8_t *cur_on_out)
{
  if (hadc == NULL || avg_out == NULL || rms_out == NULL || pkpk_out == NULL || cur_on_out == NULL)
    return 0;

  uint16_t samples[CT_CUR_SAMPLES];
  uint32_t sum = 0;
  for (int i = 0; i < CT_CUR_SAMPLES; i++)
  {
    uint16_t three[3];
    read_three_channels(hadc, three);
    samples[i] = three[0];  /* ADC1 = CH3(PA3) 첫 번째 변환값 */
    sum += samples[i];
  }

  uint16_t avg = (uint16_t)(sum / CT_CUR_SAMPLES);
  uint32_t sum_sq = 0;
  uint16_t vmin = samples[0], vmax = samples[0];
  for (int i = 0; i < CT_CUR_SAMPLES; i++)
  {
    int32_t d = (int32_t)samples[i] - (int32_t)avg;
    sum_sq += (uint32_t)(d * d);
    if (samples[i] < vmin) vmin = samples[i];
    if (samples[i] > vmax) vmax = samples[i];
  }
  float rms_f = sqrtf((float)sum_sq / (float)CT_CUR_SAMPLES);
  uint16_t rms = (uint16_t)(rms_f + 0.5f);
  uint16_t pkpk = (vmax >= vmin) ? (uint16_t)(vmax - vmin) : 0u;

  *avg_out = avg;
  *rms_out = rms;
  *pkpk_out = pkpk;

  uint8_t on_candidate  = (rms >= CT_CUR_RMS_ON_THRESHOLD)  || (pkpk >= CT_CUR_PKPK_ON_THRESHOLD)  ? 1u : 0u;
  uint8_t off_candidate = (rms <= CT_CUR_RMS_OFF_THRESHOLD) && (pkpk <= CT_CUR_PKPK_OFF_THRESHOLD) ? 1u : 0u;

  if (on_candidate)
  {
    s_cur_count = (s_cur_state == 0u) ? (s_cur_count + 1u) : 0u;
    if (s_cur_state == 0u && s_cur_count >= CT_CUR_CONSECUTIVE)
    {
      s_cur_state = 1u;
      s_cur_count = 0u;
    }
  }
  else if (off_candidate)
  {
    s_cur_count = (s_cur_state == 1u) ? (s_cur_count + 1u) : 0u;
    if (s_cur_state == 1u && s_cur_count >= CT_CUR_CONSECUTIVE)
    {
      s_cur_state = 0u;
      s_cur_count = 0u;
    }
  }
  else
    s_cur_count = 0u;

  *cur_on_out = s_cur_state;
  return 1;
}
