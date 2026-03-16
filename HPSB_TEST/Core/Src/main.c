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
#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ct_current.h"
#include "adc_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 1이면 최소 안전 모드(전원+LED+스위치만). 0이면 RS485+릴레이+ADC 전체 하드웨어 검증. */
#define HPSB_TEST_MINIMAL_SAFE_MODE 0
/* 1이면 송신 단순화: 1초마다 TEST\\r\\n 만 전송 (보드/PC 원인 분리용). 0이면 스테이지 또는 전체 포맷. */
#define HPSB_TEST_SIMPLE_TX  0
/* 송신 문자열 단계적 확대 (긴 문자열 수신 원인 분리). 1~5 사용, 0이면 스테이지 비활성(기존 전체 포맷). */
#define HPSB_TEST_TX_STAGE   1
/* 1이면 ADC1만 64샘플 읽어 AVG/RMS/PKPK 계산, 임계값으로 CUR=ON/OFF 판정 후 1초마다 전송. (D8,9,10 제거 회로 기준) */
#define HPSB_TEST_CUR_DETECT 1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* HPSB_TEST: 1초 주기 상태 전송, 릴레이 순차, ADC */
static uint32_t s_last_tick;
static uint32_t s_seq;
static uint8_t  s_relay_phase;     /* 0~3: R1만ON, R2만ON, R3만ON, 전부OFF */
static uint8_t  s_led01_toggle;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void RS485_Send(const uint8_t *buf, uint16_t len);
static void Relay_UpdateFromPhase(void);
static void LED_UpdateFromRelay(void);
#if HPSB_TEST_MINIMAL_SAFE_MODE
static void MX_GPIO_Init_MinimalSafe(void);  /* RS485/ADC/릴레이 핀 구동 없음, LED·스위치만 */
#endif
/* 클럭 직후 즉시 모든 GPIO 입력(구동 없음) → 전류 급상승 방지 */
static void MX_GPIO_Init_AllInputSafe(void);
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
  /* 전류 급상승 방지: 클럭 확립 직후 모든 GPIO를 즉시 입력(하이-Z)으로 고정 */
  MX_GPIO_Init_AllInputSafe();
  /* USER CODE END SysInit */

#if HPSB_TEST_MINIMAL_SAFE_MODE
  /* 최소 안전 모드: UART/ADC 초기화 없음. LED·스위치만 사용. */
  MX_GPIO_Init_MinimalSafe();
  /* USER CODE BEGIN 2 */
  s_last_tick = HAL_GetTick();
  /* USER CODE END 2 */
#else
  /* 일반 모드: LED/릴레이/DE 등만 출력으로 재설정. UART/ADC 사용. */
  MX_GPIO_Init();
  MX_ADC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
  s_last_tick = HAL_GetTick();
  s_seq = 0;
  s_relay_phase = 0;
  s_led01_toggle = 0;
  Relay_UpdateFromPhase();
  LED_UpdateFromRelay();
  /* USER CODE END 2 */
#endif

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if HPSB_TEST_MINIMAL_SAFE_MODE
    /* LED01만 1초마다 점멸, 그 외 구동 없음. 전류/발열 확인용. */
    uint32_t now = HAL_GetTick();
    if ((now - s_last_tick) >= 1000u)
    {
      s_last_tick = now;
      HAL_GPIO_TogglePin(LED01_GPIO_Port, LED01_Pin);
    }
    __WFI();
#else
    uint32_t now = HAL_GetTick();

#if !HPSB_TEST_SIMPLE_TX && !HPSB_TEST_CUR_DETECT
    /* 부팅 후 3초: 무부하 오프셋 수집. SIMPLE_TX/CUR_DETECT 모드에서는 생략 */
    if (!CT_IsOffsetDone())
    {
      (void)CT_Offset_Collect(&hadc);
      static uint32_t s_offset_led_tick;
      if ((now - s_offset_led_tick) >= 500u)
      {
        s_offset_led_tick = now;
        HAL_GPIO_TogglePin(LED01_GPIO_Port, LED01_Pin);
      }
      __WFI();
      continue;
    }
#endif

    if ((now - s_last_tick) >= 1000u)
    {
#if HPSB_TEST_CUR_DETECT
      /* 3채널 스캔 첫 값(ADC1) 64샘플 → ADC1_AVG, RMS, PKPK, CUR (78~80 OFF / 80 이상 ON) */
      uint16_t avg, rms, pkpk;
      uint8_t cur_on;
      if (CT_CurrentDetect_ADC1(&hadc, &avg, &rms, &pkpk, &cur_on))
      {
        char line[80];
        int n = snprintf(line, sizeof(line),
            "ADC1_AVG=%u,RMS=%u,PKPK=%u,CUR=%s\r\n",
            (unsigned)avg, (unsigned)rms, (unsigned)pkpk, cur_on ? "ON" : "OFF");
        if (n > 0)
        {
          uint16_t send_len = (uint16_t)((n >= (int)sizeof(line)) ? sizeof(line) - 1 : n);
          if (send_len >= 2)
          {
            line[send_len - 2] = '\r';
            line[send_len - 1] = '\n';
            HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET);
            RS485_Send((const uint8_t *)line, send_len);
            HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET);
          }
        }
      }
      s_last_tick = HAL_GetTick();
#elif HPSB_TEST_SIMPLE_TX
      /* 단순 송신: 1초마다 TEST\\r\\n 만 전송 */
      static const uint8_t simple_msg[] = "TEST\r\n";
      HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET);  /* 송신 시 LED01 ON (로우 액티브) */
      RS485_Send(simple_msg, (uint16_t)(sizeof(simple_msg) - 1));
      HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET);    /* 송신 후 LED01 OFF */
      s_last_tick = HAL_GetTick();
#else
      s_seq++;
      s_relay_phase = (s_relay_phase + 1) % 4;
      Relay_UpdateFromPhase();
      LED_UpdateFromRelay();
      uint16_t a1_raw = 0, a2_raw = 0, a3_raw = 0;
      uint16_t rms1 = 0;
      uint16_t rms_dummy;
      float i1 = 0.f;
      float k1 = CT_GetCalibration(0);
      if (CT_ReadChannelRMS(&hadc, 0, &rms1, &a1_raw))
        i1 = CT_ADC_RMS_to_Ampere(rms1, k1);
      (void)CT_ReadChannelRMS(&hadc, 1, &rms_dummy, &a2_raw);
      (void)CT_ReadChannelRMS(&hadc, 2, &rms_dummy, &a3_raw);
      uint16_t i1x100 = (uint16_t)(i1 * 100.f + 0.5f);
      uint8_t r1 = (s_relay_phase == 0) ? 1 : 0;
      uint8_t r2 = (s_relay_phase == 1) ? 1 : 0;
      uint8_t r3 = (s_relay_phase == 2) ? 1 : 0;
#if HPSB_TEST_TX_STAGE >= 1 && HPSB_TEST_TX_STAGE <= 3
      (void)r1;
      (void)r2;
      (void)r3;
      (void)i1x100;
#elif HPSB_TEST_TX_STAGE == 4
      (void)r1;
      (void)r2;
      (void)r3;
#endif

#define HPSB_TEST_LINE_SIZE  280
      char line[HPSB_TEST_LINE_SIZE];
      int n = -1;
#if HPSB_TEST_TX_STAGE == 1
      n = snprintf(line, sizeof(line), "ADC=%u\r\n", (unsigned)a1_raw);
#elif HPSB_TEST_TX_STAGE == 2
      n = snprintf(line, sizeof(line), "A1=%u,A2=%u,A3=%u\r\n",
          (unsigned)a1_raw, (unsigned)a2_raw, (unsigned)a3_raw);
#elif HPSB_TEST_TX_STAGE == 3
      n = snprintf(line, sizeof(line), "A1=%u,RMS=%u\r\n", (unsigned)a1_raw, (unsigned)rms1);
#elif HPSB_TEST_TX_STAGE == 4
      n = snprintf(line, sizeof(line), "A1=%u,RMS=%u,I1x100=%u\r\n",
          (unsigned)a1_raw, (unsigned)rms1, (unsigned)i1x100);
#elif HPSB_TEST_TX_STAGE == 5
      n = snprintf(line, sizeof(line),
          "MS=%lu,HPSB_TEST,SEQ=%lu,R1=%u,R2=%u,R3=%u,"
          "A1_RAW=%u,A1_RMS=%u,I1x100=%u,A2_RAW=%u,A3_RAW=%u\r\n",
          (unsigned long)now, (unsigned long)s_seq, r1, r2, r3,
          a1_raw, rms1, (unsigned)i1x100, a2_raw, a3_raw);
#else
      /* 스테이지 0: 전체 포맷 (float 없이 I1x100) */
      uint32_t k1e4 = (uint32_t)(k1 * 10000.f + 0.5f);
      n = snprintf(line, sizeof(line),
          "<MSG>MS=%lu,HPSB_TEST,SEQ=%lu,R1=%u,R2=%u,R3=%u,"
          "A1_RAW=%u,A1_RMS=%u,K=0.%04lu,I1x100=%u,"
          "A2_RAW=%u,I2=unused,A3_RAW=%u,I3=unused\r\n",
          (unsigned long)now, (unsigned long)s_seq, r1, r2, r3,
          a1_raw, rms1, (unsigned long)k1e4, (unsigned)i1x100,
          a2_raw, a3_raw);
#endif
      /* snprintf 검사: n < 0 오류, n >= sizeof(line) 잘림. 끝은 항상 \r\n */
      if (n > 0)
      {
        uint16_t send_len;
        if (n >= (int)sizeof(line))
          send_len = (uint16_t)(sizeof(line) - 1);  /* 잘림 시 버퍼 끝 직전까지 */
        else
          send_len = (uint16_t)n;
        if (send_len >= 2)
        {
          line[send_len - 2] = '\r';
          line[send_len - 1] = '\n';
          HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET);
          RS485_Send((const uint8_t *)line, send_len);
          HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET);
        }
      }
#undef HPSB_TEST_LINE_SIZE
      s_last_tick = HAL_GetTick();
#endif
    }
    __WFI();
#endif
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
/**
  * @brief HSI 8MHz 전용 (PLL/HSE 미사용) — 크리스탈/부품 불량 시에도 MCU 보호.
  *        CubeMX 재생성 시 이 블록이 덮어쓰이면 다시 HSI 전용으로 되돌려 둘 것.
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI14;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;   /* PLL 사용 안 함 */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;  /* HSI 8MHz 직접 */
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
   *  TC_ADC01=PA3=CH3, TC_ADC02=PA4=CH4, TC_ADC03=PA5=CH5 (CubeMX ADC_IN3/4/5).
   *  스캔 순서 CH3→CH4→CH5 → 로그 ADC1 = 첫 번째 결과 = TC_ADC01(PA3).
   *  샘플링 시간 239.5 사이클: 채널 전환 시 크로스토크 감소.
   */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** STM32F0 ADC calibration (권장: 초기화 직후 1회) */
  if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC_Init 2 */

  /* USER CODE END ADC_Init 2 */

}

/**
  * @brief 전류감지용: ADC 재초기화 없이 CH3(PA3)만 변환하도록 ConfigChannel만 변경.
  *        DeInit/Init/Calibration 사용 안 함 (STM32F0 반복 시 불안정).
  */
void ADC_ConfigForPA3Only(ADC_HandleTypeDef *hadc)
{
  if (hadc == NULL) return;
  HAL_ADC_Stop(hadc);
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = ADC_CHANNEL_3;  /* PA3 = ADC_IN3 (STM32F030) */
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  (void)HAL_ADC_ConfigChannel(hadc, &sConfig);
}

/**
  * @brief 전류감지 측정 후: ConfigChannel만으로 CH3/CH4/CH5 3채널 스캔 복원. 재초기화 없음.
  */
void ADC_ConfigRestoreThreeChannels(ADC_HandleTypeDef *hadc)
{
  if (hadc == NULL) return;
  HAL_ADC_Stop(hadc);
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  sConfig.Channel = ADC_CHANNEL_3;
  if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK) return;
  sConfig.Channel = ADC_CHANNEL_4;
  if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK) return;
  sConfig.Channel = ADC_CHANNEL_5;
  (void)HAL_ADC_ConfigChannel(hadc, &sConfig);
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
  HAL_GPIO_WritePin(GPIOA, RLY_EN01_Pin|RLY_EN02_Pin|RLY_EN03_Pin|RS485_DE_Pin
                          |LED04_Pin, GPIO_PIN_RESET);

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

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
 * @brief 클럭 확립 직후 호출. 모든 GPIO를 즉시 입력(풀 없음)으로 두어 구동 전류 제거.
 *        전류 급상승 방지용. 이후 MX_GPIO_Init 또는 MX_GPIO_Init_MinimalSafe에서 필요한 핀만 재설정.
 */
static void MX_GPIO_Init_AllInputSafe(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Pin = (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}

#if HPSB_TEST_MINIMAL_SAFE_MODE
/**
 * @brief 최소 안전 모드: RS485/ADC/릴레이 핀은 전부 입력(구동 없음). LED·스위치만 사용.
 *        전원+LED+스위치만 연결된 보드에서 전류/발열 원인 확인용.
 */
static void MX_GPIO_Init_MinimalSafe(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 사용 안 함: PA0(RLY1), PA1(RLY2), PA2(RLY3), PA3~5(ADC), PA9(TX), PA10(RX), PA11(DE) → 입력, 구동 없음 */
  GPIO_InitStruct.Pin = RLY_EN01_Pin | RLY_EN02_Pin | RLY_EN03_Pin
      | TC_ADC01_Pin | TC_ADC02_Pin | TC_ADC03_Pin
      | RS485_TX_Pin | RS485_RX_Pin | RS485_DE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LED: 출력, 초기 OFF(로우 액티브이므로 HIGH) */
  HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = LED01_Pin | LED02_Pin | LED03_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = LED04_Pin;
  HAL_GPIO_Init(LED04_GPIO_Port, &GPIO_InitStruct);

  /* 스위치(ID): 입력 */
  GPIO_InitStruct.Pin = ID_BIT1_Pin | ID_BIT2_Pin | ID_BIT3_Pin | ID_BIT4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
#endif

/**
 * @brief RS485 반이중 송신: DE HIGH → UART TX → TC 대기 → DE LOW
 */
static void RS485_Send(const uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return;
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
  for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }
  HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 100);
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) { }
  /* 마지막 바이트(\\r\\n)가 라인에 완전히 나갈 때까지 짧게 대기 후 DE LOW */
  for (volatile uint32_t d = 0; d < 1000; d++) { (void)d; }
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
}

static void Relay_UpdateFromPhase(void)
{
  HAL_GPIO_WritePin(RLY_EN01_GPIO_Port, RLY_EN01_Pin, (s_relay_phase == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RLY_EN02_GPIO_Port, RLY_EN02_Pin, (s_relay_phase == 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RLY_EN03_GPIO_Port, RLY_EN03_Pin, (s_relay_phase == 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void LED_UpdateFromRelay(void)
{
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, (s_relay_phase == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, (s_relay_phase == 1) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, (s_relay_phase == 2) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
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
