/**
 ******************************************************************************
 * @file    ct_current.h
 * @brief   CT 전류 환산: ADC raw → 오프셋 제거 → RMS → 전류(A)
 *          HCT17W + burden 18Ω + 1.65V 바이어스 회로 기준.
 ******************************************************************************
 */
#ifndef CT_CURRENT_H
#define CT_CURRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f0xx_hal.h"

/* 채널 수 (TC_ADC01, 02, 03) */
#define CT_CHANNELS  3

/* 오프셋 수집: 부팅 후 이 시간(ms) 동안 무부하로 가정하고 DC 오프셋 측정 */
#define CT_OFFSET_COLLECT_MS    3000
/* 오프셋 수집 시 샘플 간격(ms) */
#define CT_OFFSET_SAMPLE_MS     50
/* RMS 계산용 샘플 개수. 16=빠른 1초 주기, 64=정밀(주기 길어짐) */
#define CT_RMS_SAMPLES          16
/* RMS 샘플 간격(ms): 현재 미사용. 루프 내 HAL_Delay 제거로 샘플링 가속. */
/* #define CT_RMS_SAMPLE_DELAY_MS  1 */

/**
 * @brief 오프셋 수집 완료 여부 (1이면 정상 구간에서 RMS 사용 가능)
 */
uint8_t CT_IsOffsetDone(void);

/**
 * @brief 부팅 후 무부하 구간에서 DC 오프셋 수집. 1초 루프에서 주기적으로 호출.
 * @param hadc ADC 핸들 (main.c의 hadc 전달)
 * @return 1이면 수집 완료, 0이면 아직 수집 중
 */
uint8_t CT_Offset_Collect(ADC_HandleTypeDef *hadc);

/**
 * @brief 한 채널에 대해 N회 샘플 → 오프셋 제거 → RMS(ADC LSB) 계산
 * @param hadc   ADC 핸들
 * @param ch     채널 인덱스 0=TC_ADC01, 1=TC_ADC02, 2=TC_ADC03
 * @param rms_adc RMS 값 (ADC LSB 단위, 출력)
 * @param raw_avg  샘플 평균(raw, 로그용, NULL 가능)
 * @return 1 성공, 0 실패(오프셋 미준비 등)
 */
uint8_t CT_ReadChannelRMS(ADC_HandleTypeDef *hadc, uint8_t ch,
                          uint16_t *rms_adc, uint16_t *raw_avg);

/**
 * @brief RMS(ADC LSB) → 전류(A) 변환. I = cal_k * rms_adc
 * @param rms_adc RMS 값 (ADC LSB)
 * @param cal_k   캘리브레이션 계수 (나중에 실제 전류계로 맞춤)
 * @return 전류 [A]
 */
float CT_ADC_RMS_to_Ampere(uint16_t rms_adc, float cal_k);

/**
 * @brief 채널별 캘리브레이션 계수 설정/조회 (임시 변수, 나중에 4.77A 기준으로 조정)
 */
void CT_SetCalibration(uint8_t ch, float k);
float CT_GetCalibration(uint8_t ch);

/* ----- 전류 유무 판정 (CUR=ON/OFF), PKPK 기준: 44 이하 OFF / 45 이상 ON ----- */
#define CT_CUR_SAMPLES            64   /* 1초마다 ADC1 샘플 개수 */
/* ON 후보: PKPK >= 45 이면 히터 ON */
#define CT_CUR_RMS_ON_THRESHOLD   100  /* ON은 PKPK만 사용 (RMS만으로 ON 안 함) */
#define CT_CUR_PKPK_ON_THRESHOLD  45
/* OFF 후보: PKPK <= 44 이면 히터 OFF */
#define CT_CUR_RMS_OFF_THRESHOLD  30   /* OFF 시 RMS 조건 완화 */
#define CT_CUR_PKPK_OFF_THRESHOLD 44
/* 이 회수만큼 연속으로 ON(또는 OFF) 후보일 때만 CUR 상태 전환. 순간 노이즈 방지 */
#define CT_CUR_CONSECUTIVE        2

/**
 * @brief ADC1(3채널 스캔 첫 값) 64샘플로 AVG/RMS/PKPK 계산 후 임계값으로 전류 유무 판정.
 *        (PKPK 44 이하 OFF / 45 이상 ON)
 * @param hadc     ADC 핸들
 * @param avg_out  평균 (ADC LSB) → 로그 ADC1_AVG
 * @param rms_out  RMS (AC 성분, ADC LSB)
 * @param pkpk_out Peak-to-Peak (max - min)
 * @param cur_on_out 1=CUR=ON, 0=CUR=OFF
 * @return 1 성공, 0 실패
 */
uint8_t CT_CurrentDetect_ADC1(ADC_HandleTypeDef *hadc,
                              uint16_t *avg_out, uint16_t *rms_out, uint16_t *pkpk_out,
                              uint8_t *cur_on_out);

#ifdef __cplusplus
}
#endif

#endif /* CT_CURRENT_H */
