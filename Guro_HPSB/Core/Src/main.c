/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led_status.h"
#include "modbus_slave.h"
#include "modbus_cfg.h"
#include "modbus_table.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
#if HPSB_TX_TEST_ENABLE
static uint32_t s_tx_test_last_tick;
#endif
#if HPSB_RS485_TX_STRING_TEST
static uint32_t s_string_test_last_tick;
#endif
#if HPSB_OKOK_STREAM_TEST
static uint32_t s_okok_test_last_tick;
#endif
#if HPSB_MAX3485_TX_AA_TEST
static uint32_t s_aa_test_tick;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* 부팅 직후 LED2 한 번 깜빡임(300ms) — 여기 보이면 코드는 돌아가는 것. 다운로드 후 리셋 필요할 수 있음. */
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET);
  HAL_Delay(300);
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET);

#if HPSB_PA9_TEST_MODE != 1
  MX_ADC_Init();
  MX_USART1_UART_Init();
#endif
  /* USER CODE BEGIN 2 */
#if HPSB_PA9_TEST_MODE == 1
  /* PA9 GPIO 토글 테스트: PA9를 GPIO 출력으로 설정, 500ms마다 토글. 핀 자체/클럭 검증. */
  {
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_9;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
  }
  for (;;) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_9);
    HAL_Delay(500);
  }
#elif HPSB_PA9_TEST_MODE == 2
  /* PA9 UART TX 테스트: Modbus/DE 배제, 500ms마다 0x55 1바이트 송신. PA9에서 9600 8N1 파형 확인. */
  for (;;) {
    uint8_t b = 0x55;
    HAL_UART_Transmit(&huart1, &b, 1, 100);
    HAL_Delay(500);
  }
#endif
#if HPSB_RS485_PA8_TEST_MODE
  /* RS485_DE 강제 토글 진단: Modbus 비활성화, 1초마다 LOW↔HIGH.
   * 오실로로 MCU PA11(RS485_DE) / MAX3485 pin2(/RE) / pin3(DE) 동시 측정. */
  for (;;) {
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    HAL_Delay(1000);
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
    HAL_Delay(1000);
  }
#endif

#if HPSB_PA9_TEST_MODE == 0 && !HPSB_MAX3485_TX_AA_TEST
  ModbusSlave_Init();
  LED_Status_Init();
#if HPSB_TX_TEST_ENABLE
  s_tx_test_last_tick = HAL_GetTick();
#endif
#if HPSB_RS485_TX_STRING_TEST
  s_string_test_last_tick = HAL_GetTick();
#endif
#if HPSB_OKOK_STREAM_TEST
  s_okok_test_last_tick = HAL_GetTick();
#endif
#endif
#if HPSB_MAX3485_TX_AA_TEST
  s_aa_test_tick = HAL_GetTick();
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
#if 0
  /* 발열 확인용 최소 루프: #if 1 시 Modbus 비동작. 테스트 시에는 #if 0 으로 전체 루프 사용. */
  while (1) {
    __WFI();
  }
#else
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if HPSB_MAX3485_TX_AA_TEST
    /* MAX3485 동작 확인: 1초마다 0xAA 1바이트만 DE HIGH → UART TX → TC → DE LOW. LED2=전송 시 ON→OFF(코드 도는지 확인용). */
    if ((HAL_GetTick() - s_aa_test_tick) >= 1000u) {
        HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET);       /* LED2 ON (로우 액티브) — 여기 들어오면 코드는 동작 중 */
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);   /* DE HIGH → 송신 모드 */
        for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }
        {
            uint8_t b = 0xAA;
            HAL_UART_Transmit(&huart1, &b, 1, 100);
            while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) { }
        }
        HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);  /* DE LOW → 수신 모드 */
        HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET);         /* LED2 OFF */
        s_aa_test_tick = HAL_GetTick();
    }
    __WFI();
#elif HPSB_RS485_TX_STRING_TEST
    /* RS485 송신 경로 단독 검증: Modbus 배제, 1초마다 "HPSB_OK\r\n" 송신. LED2=송신 직전 ON, 직후 OFF. */
    if ((HAL_GetTick() - s_string_test_last_tick) >= 1000u) {
        HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET);  /* LED2 ON (송신 직전) */
        ModbusSlave_SendTestString("HPSB_OK\r\n", 8);
        HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET);    /* LED2 OFF (송신 완료 후) */
        s_string_test_last_tick = HAL_GetTick();
    }
    __WFI();
#elif HPSB_OKOK_STREAM_TEST
    /* ASCII 브리지 테스트: 1초마다 "OKOK\r\n" 송신 (DE=TX -> TX -> TC -> DE=RX는 ModbusSlave_SendTestString에서 처리) */
    if ((HAL_GetTick() - s_okok_test_last_tick) >= 1000u) {
        ModbusSlave_SendTestString("OKOK\r\n", 6);
        s_okok_test_last_tick = HAL_GetTick();
    }
    __WFI();
#else
    LED_Status_Tick_1ms();
#if HPSB_TX_TEST_ENABLE
    if ((HAL_GetTick() - s_tx_test_last_tick) >= 2000u) {
        ModbusSlave_SendTestFrame();
        s_tx_test_last_tick = HAL_GetTick();
    }
#endif
    ModbusSlave_Poll();
    ModbusSlave_ProcessDebugLEDs();
    __WFI();
#endif
  }
  /* USER CODE END 3 */
#endif
}

/**
  * @brief System Clock Configuration — 내부 클럭(HSI 8MHz)만 사용, PLL/HSE 미사용
  *        SYSCLK = HSI 8MHz, HCLK = PCLK1 = 8MHz. Modbus 9600 동작.
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** HSI 8MHz ON, HSI14 ON(ADC용). PLL 사용 안 함.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI14;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** SYSCLK = HSI(8MHz), AHB/APB1 = /1 → HCLK = PCLK1 = 8MHz
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC_Init(void)
{

  /* USER CODE BEGIN ADC_Init 0 */

  /* USER CODE END ADC_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC_Init 1 */

  /* USER CODE END ADC_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc.Instance = ADC1;
  hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.LowPowerAutoPowerOff = DISABLE;
  hadc.Init.ContinuousConvMode = DISABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  if (HAL_ADC_Init(&hadc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_4;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel to be converted.
  */
  sConfig.Channel = ADC_CHANNEL_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC_Init 2 */

  /* USER CODE END ADC_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  /* Set initial relay outputs to LOW (GPIO_PIN_RESET). */
  HAL_GPIO_WritePin(GPIOA, RLY_EN01_Pin|RLY_EN02_Pin|RLY_EN03_Pin, GPIO_PIN_RESET);
  /* RS485 DE idle = RX (LOW) */
  HAL_GPIO_WritePin(GPIOA, RS485_DE_Pin|LED04_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED01_Pin|LED02_Pin|LED03_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : RLY_EN01_Pin RLY_EN02_Pin RLY_EN03_Pin LED04_Pin */
  GPIO_InitStruct.Pin = RLY_EN01_Pin|RLY_EN02_Pin|RLY_EN03_Pin|LED04_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : ID_BIT1_Pin ID_BIT2_Pin ID_BIT3_Pin ID_BIT4_Pin */
  GPIO_InitStruct.Pin = ID_BIT1_Pin|ID_BIT2_Pin|ID_BIT3_Pin|ID_BIT4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : RS485_DE_Pin */
  GPIO_InitStruct.Pin = RS485_DE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(RS485_DE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED01_Pin LED02_Pin LED03_Pin */
  GPIO_InitStruct.Pin = LED01_Pin|LED02_Pin|LED03_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* HPSB RS485 idle = 수신: RS485_DE_Pin(PA11) LOW. */
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
