/**
 ******************************************************************************
 * @file    adc_app.h
 * @brief   LPSB ADC: ACS_ADC01/02/03 (PA3/4/5) raw 읽기, N회 평균.
 ******************************************************************************
 */
#ifndef ADC_APP_H
#define ADC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LPSB_ADC_CHANNELS  3

/** 채널 0=PA3(ACS_ADC01), 1=PA4(ACS_ADC02), 2=PA5(ACS_ADC03). raw 1회 읽기. */
uint16_t LPSB_ADC_ReadRaw(uint8_t ch);

/** 3채널 한 번에 읽어 out[0..2]에 저장. (스캔 1회) */
void LPSB_ADC_ReadThreeRaw(uint16_t *out);

/** N회 샘플 평균 (기본 8회). ch 0~2. */
uint16_t LPSB_ADC_GetRawAvg(uint8_t ch);

/** 주기적으로 호출 시 최신 raw 평균 유지. 내부에서 LPSB_ADC_ReadThreeRaw 사용. */
void LPSB_ADC_Update(void);

/** 현재 저장된 채널별 raw 평균 (LPSB_ADC_Update 호출 후 유효). */
uint16_t LPSB_ADC_GetStoredAvg(uint8_t ch);

/**
 * AC 전류 측정 디버깅용: 채널별 PKPK(최대-최소)를 반환.
 * (LPSB_ADC_Update 호출 후 유효)
 */
uint16_t LPSB_ADC_GetStoredPkpk(uint8_t ch);

/**
 * DMA/RMS 등 외부 샘플링 결과를 Modbus 맵으로 내보내기 위해 저장값을 강제로 갱신.
 * (테스트 펌웨어 전용)
 */
void LPSB_ADC_SetStoredAvg(uint8_t ch, uint16_t avg);
void LPSB_ADC_SetStoredPkpk(uint8_t ch, uint16_t pkpk);

#ifdef __cplusplus
}
#endif

#endif /* ADC_APP_H */
