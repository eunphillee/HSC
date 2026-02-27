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
#include "stm32f2xx_hal.h"

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
#define RELAY3_EN_Pin GPIO_PIN_2
#define RELAY3_EN_GPIO_Port GPIOE
#define RELAY4_EN_Pin GPIO_PIN_3
#define RELAY4_EN_GPIO_Port GPIOE
#define DI_01_Pin GPIO_PIN_4
#define DI_01_GPIO_Port GPIOE
#define DI_02_Pin GPIO_PIN_5
#define DI_02_GPIO_Port GPIOE
#define DI_03_Pin GPIO_PIN_6
#define DI_03_GPIO_Port GPIOE
#define PC_RESET_EN_Pin GPIO_PIN_0
#define PC_RESET_EN_GPIO_Port GPIOC
#define PC_ON_EN_Pin GPIO_PIN_1
#define PC_ON_EN_GPIO_Port GPIOC
#define PC_LED_IN_Pin GPIO_PIN_2
#define PC_LED_IN_GPIO_Port GPIOC
#define RS485_DE_Pin GPIO_PIN_1
#define RS485_DE_GPIO_Port GPIOB
#define DI_04_Pin GPIO_PIN_7
#define DI_04_GPIO_Port GPIOE
#define DI_05_Pin GPIO_PIN_8
#define DI_05_GPIO_Port GPIOE
#define DI_06_Pin GPIO_PIN_9
#define DI_06_GPIO_Port GPIOE
#define DI_07_Pin GPIO_PIN_10
#define DI_07_GPIO_Port GPIOE
#define DI_08_Pin GPIO_PIN_11
#define DI_08_GPIO_Port GPIOE
#define LED03_Pin GPIO_PIN_10
#define LED03_GPIO_Port GPIOB
#define LED04_Pin GPIO_PIN_11
#define LED04_GPIO_Port GPIOB
#define EEP_I2C_SDA_Pin GPIO_PIN_9
#define EEP_I2C_SDA_GPIO_Port GPIOC
#define EEP_I2C_SCL_Pin GPIO_PIN_8
#define EEP_I2C_SCL_GPIO_Port GPIOA
#define RS485_RX_Pin GPIO_PIN_9
#define RS485_RX_GPIO_Port GPIOA
#define RS485_TX_Pin GPIO_PIN_10
#define RS485_TX_GPIO_Port GPIOA
#define SEN1_I2C_SCL_Pin GPIO_PIN_6
#define SEN1_I2C_SCL_GPIO_Port GPIOB
#define SEN1_I2C_SDA_Pin GPIO_PIN_7
#define SEN1_I2C_SDA_GPIO_Port GPIOB
#define LED01_Pin GPIO_PIN_8
#define LED01_GPIO_Port GPIOB
#define LED02_Pin GPIO_PIN_9
#define LED02_GPIO_Port GPIOB
#define RELAY1_EN_Pin GPIO_PIN_0
#define RELAY1_EN_GPIO_Port GPIOE
#define RELAY2_EN_Pin GPIO_PIN_1
#define RELAY2_EN_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
