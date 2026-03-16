/**
 ******************************************************************************
 * @file    adc_config.h
 * @brief   ADC 전류감지용 PA3 단일채널 전환. ConfigChannel만 사용(DeInit/Init/Calibration 반복 없음).
 ******************************************************************************
 */
#ifndef ADC_CONFIG_H
#define ADC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"

/** 전류감지 측정 전: ADC를 PA3(CH3) 단일채널만 변환하도록 설정. PA4/PA5 제외. */
void ADC_ConfigForPA3Only(ADC_HandleTypeDef *hadc);

/** 전류감지 측정 후: ADC를 CH3/CH4/CH5 3채널 스캔으로 복원 (오프셋/기타 기능용). */
void ADC_ConfigRestoreThreeChannels(ADC_HandleTypeDef *hadc);

#ifdef __cplusplus
}
#endif

#endif /* ADC_CONFIG_H */
