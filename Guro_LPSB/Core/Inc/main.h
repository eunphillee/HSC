/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SSR1_EN_Pin GPIO_PIN_0
#define SSR1_EN_GPIO_Port GPIOA
#define SSR2_EN_Pin GPIO_PIN_1
#define SSR2_EN_GPIO_Port GPIOA
#define SSR3_EN_Pin GPIO_PIN_2
#define SSR3_EN_GPIO_Port GPIOA
#define ACS_ADC01_Pin GPIO_PIN_3
#define ACS_ADC01_GPIO_Port GPIOA
#define ACS_ADC02_Pin GPIO_PIN_4
#define ACS_ADC02_GPIO_Port GPIOA
#define ACS_ADC03_Pin GPIO_PIN_5
#define ACS_ADC03_GPIO_Port GPIOA
#define ID_BIT1_Pin GPIO_PIN_0
#define ID_BIT1_GPIO_Port GPIOB
#define ID_BIT2_Pin GPIO_PIN_1
#define ID_BIT2_GPIO_Port GPIOB
#define RS485_DE_Pin GPIO_PIN_8
#define RS485_DE_GPIO_Port GPIOA
#define RS485_TX_Pin GPIO_PIN_9
#define RS485_TX_GPIO_Port GPIOA
#define RS485_RX_Pin GPIO_PIN_10
#define RS485_RX_GPIO_Port GPIOA
#define LED04_Pin GPIO_PIN_15
#define LED04_GPIO_Port GPIOA
#define ID_BIT3_Pin GPIO_PIN_3
#define ID_BIT3_GPIO_Port GPIOB
#define ID_BIT4_Pin GPIO_PIN_4
#define ID_BIT4_GPIO_Port GPIOB
#define LED01_Pin GPIO_PIN_5
#define LED01_GPIO_Port GPIOB
#define LED02_Pin GPIO_PIN_6
#define LED02_GPIO_Port GPIOB
#define LED03_Pin GPIO_PIN_7
#define LED03_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
