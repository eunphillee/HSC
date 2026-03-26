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
#include "app_config.h"
#include "app_scheduler.h"
#include "aggregator.h"
#include "aggregated_status.h"
#include "upstream_pc_protocol.h"
#include "upstream_slave_uart1.h"
#include "modbus_master.h"
#include "gateway_actions.h"
#include "led_status.h"
#include "pc_test_aa_stream.h"
#include "reset_reason.h"
#include "wwdg_service.h"
#include "system_config.h"
#include "system_sync.h"
#include <stdio.h>
#include <string.h>
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
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

WWDG_HandleTypeDef hwwdg;

/* USER CODE BEGIN PV */
static aggregated_status_t aggregated_status;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_WWDG_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#if UART2_RS485_SUB_TXRX_TEST_ENABLE
static void rs485_test_tx(void)
{
  static const char msg[] = "MB->SUB TEST\r\n";

  /* DE=1: TX 모드, 전송 완료(TC) 대기 후 DE=0: RX 모드로 복귀 */
  HAL_GPIO_WritePin(RS_485_DE_RE_GPIO_Port, RS_485_DE_RE_Pin, GPIO_PIN_SET);
  (void)HAL_UART_Transmit(&huart2, (uint8_t *)msg, (uint16_t)(sizeof(msg) - 1u), 100);

  uint32_t start = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {
    if ((HAL_GetTick() - start) > 20u) {
      break; /* TC 타임아웃: 혹시라도 UART가 멈추면 영구 block 방지 */
    }
  }
  HAL_GPIO_WritePin(RS_485_DE_RE_GPIO_Port, RS_485_DE_RE_Pin, GPIO_PIN_RESET);
}
#endif

#if MB_UART2_ASCII_BRIDGE_TEST
/* OKOK\r\n 시퀀스를 스트림에서 안정적으로 탐지 */
static uint8_t s_okok_window[6];
static uint8_t s_okok_window_len;

/* USART1(상위 RS485) 전송 시 DE 토글:
 * ascii bridge 모드는 Modbus slave 경로와 달리 DE 제어를 자동으로 하지 않으므로,
 * 여기서 직접 DE=TX / TC wait / DE=RX 순서를 강제한다. */
static void ascii_bridge_uart1_rs485_set_tx(void)
{
#if RS485_DE_ACTIVE_HIGH
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
#endif
}

static void ascii_bridge_uart1_rs485_set_rx(void)
{
#if RS485_DE_ACTIVE_HIGH
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
#endif
}

static void ascii_bridge_log_uart1(const char *s)
{
  if (s == NULL) return;
  ascii_bridge_uart1_rs485_set_tx();
  (void)HAL_UART_Transmit(&huart1, (const uint8_t *)s, (uint16_t)strlen(s), 100);
  /* TC까지 대기 후 RX로 복귀 (마지막 바이트 잘림 방지) */
  {
    uint32_t start = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) {
      if ((HAL_GetTick() - start) > 20u) {
        break;
      }
    }
  }
  ascii_bridge_uart1_rs485_set_rx();
}

static void ascii_bridge_tick(void)
{
  uint8_t b;
  while (HAL_UART_Receive(&huart2, &b, 1, 0) == HAL_OK) {
    /* Sliding window detection: last 6 bytes == "OKOK\r\n" */
    if (s_okok_window_len < 6u) {
      s_okok_window[s_okok_window_len++] = b;
    } else {
      for (uint8_t i = 0; i < 5u; i++) s_okok_window[i] = s_okok_window[i + 1u];
      s_okok_window[5] = b;
    }

    if (s_okok_window_len == 6u) {
      static const uint8_t seq_okok[6] = { 'O', 'K', 'O', 'K', '\r', '\n' };
      if (memcmp(s_okok_window, seq_okok, 6u) == 0) {
        /* Exact payload match: 출력은 한 줄만 */
        ascii_bridge_log_uart1("[HPSB->MB] OKOK\r\n");
        s_okok_window_len = 0u; /* 1개 시퀀스당 1회 로그 */
      }
    }
  }
}
#endif

#if MB_UART1_TX_OK_STREAM_TEST
static uint32_t s_mb_ok_last_tick;

static void mb_uart1_rs485_set_tx(void)
{
#if RS485_DE_ACTIVE_HIGH
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
#endif
}

static void mb_uart1_rs485_set_rx(void)
{
#if RS485_DE_ACTIVE_HIGH
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
#endif
}

static void mb_uart1_send_mb_ok(void)
{
  static const char msg[] = "MB_OK\r\n";
  const uint16_t len = (uint16_t)(sizeof(msg) - 1u); /* exclude trailing '\0' */

  mb_uart1_rs485_set_tx();
  (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, len, 100);

  /* TC까지 대기 후 RX로 복귀 (송신 잘림 방지) */
  uint32_t start = HAL_GetTick();
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) {
    if ((HAL_GetTick() - start) > 20u) {
      break; /* 영구 block 방지 */
    }
  }
  mb_uart1_rs485_set_rx();
}
#endif

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
  ResetReason_CaptureAndClear();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_WWDG_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* EEPROM 설정 로드: 실패 또는 검증 실패 시 기본값 저장 후 적용 */
  {
    system_config_t cfg;
    if (SystemConfig_Load(&cfg) != 0 || SystemConfig_Validate(&cfg) != 0) {
      SystemConfig_SetDefaults(&cfg);
      SystemConfig_Save(&cfg);
    }
#if USE_PC_TEST_UART1_SLAVE
    /* PC 테스트 툴은 9600 고정. EEPROM baud와 무관하게 UART1=9600으로 통신 보장. */
    huart1.Init.BaudRate = 9600;
#else
    huart1.Init.BaudRate = (uint32_t)cfg.baudrate;
#endif
    (void)HAL_UART_Init(&huart1);
#if SYSTEM_CONFIG_BOOT_LOG
    SystemConfig_LogToUart((void *)&huart1);
#endif
  }
  /* 정상 동작 모드: 스케줄러/통신/집계/LED 상태 및 WWDG 서비스 초기화. */
  WwdgService_Init(&hwwdg);
  AppScheduler_Init();
  ModbusMaster_Init();
  AggregatedStatus_Clear(&aggregated_status);
  SystemSync_Init();
#if !MB_UART2_ASCII_BRIDGE_TEST
#if !USE_PC_TEST_UART1_SLAVE
  /* NOTE: UpstreamPC uses USART2 ReceiveToIdle_IT.
   * When PC_TEST_UART1_SLAVE is enabled, USART2 is reserved for downstream Modbus Master (sub RS485).
   * Running both on USART2 causes RX competition and truncated FC04 responses. */
  UpstreamPC_Init();
#endif
#endif
#if USE_PC_TEST_UART1_SLAVE && !ENABLE_PC_TEST_AA_STREAM
  UpstreamSlaveUart1_Init();
#endif
  LED_Status_Init();
#if ENABLE_PC_TEST_AA_STREAM
  PcTestAA_Init();
#endif
#if USE_PC_TEST_UART1_SLAVE && !ENABLE_PC_TEST_AA_STREAM
  /* 부팅 시 LED2 한 번 펄스 ... */
  LED_Status_OnUart1RxEvent();
  LED_Status_OnUart1SlaveTx();
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if 0  /* PB12(DE) 스코프 테스트: 1초마다 PB12+LED01 토글. 확인 후 0으로 끄기 */
    {
      static uint32_t s_pb12_toggle_tick;
      uint32_t now = HAL_GetTick();
      if ((now - s_pb12_toggle_tick) >= 1000u) {
        s_pb12_toggle_tick = now;
        HAL_GPIO_TogglePin(RS_485_DE_RE_GPIO_Port, RS_485_DE_RE_Pin);
        HAL_GPIO_TogglePin(LED01_GPIO_Port, LED01_Pin);
      }
    }
#endif

#if UART2_RS485_SUB_TXRX_TEST_ENABLE
    /* USART2(RS485 하단) TX 생존 테스트: 1초마다 송신 */
    {
      static uint32_t s_rs485_tx_tick = 0;
      uint32_t now = HAL_GetTick();
      if (s_rs485_tx_tick == 0u) s_rs485_tx_tick = now;
      if ((now - s_rs485_tx_tick) >= 1000u) {
        s_rs485_tx_tick = now;
        rs485_test_tx();
      }
    }
#endif

#if MB_UART1_TX_OK_STREAM_TEST
    /* USART1(상위 RS485, PC 링크) 테스트: 1초마다 "MB_OK\r\n" 송신 */
    {
      uint32_t now = HAL_GetTick();
      if (s_mb_ok_last_tick == 0u) s_mb_ok_last_tick = now;
      if ((now - s_mb_ok_last_tick) >= 1000u) {
        s_mb_ok_last_tick = now;
        mb_uart1_send_mb_ok();
      }
    }
#endif

    /* 1ms 주기 기반 스케줄러/업무 처리 */
    LED_Status_Tick_1ms();
    AppScheduler_Update();

#if ENABLE_PC_TEST_AA_STREAM
    PcTestAA_Tick(&aggregated_status);
#else
#if MB_UART2_ASCII_BRIDGE_TEST
    ascii_bridge_tick();
#else
#if !USE_PC_TEST_UART1_SLAVE
    if (AppScheduler_IsDue(TASK_UPSTREAM_POLL))
      UpstreamPC_Poll(&aggregated_status);
#endif
#if USE_PC_TEST_UART1_SLAVE || MODBUS_MASTER_POLL_ENABLE
    if (AppScheduler_IsDue(TASK_DOWNSTREAM_MODBUS)) {
#if USE_PC_TEST_UART1_SLAVE
      UpstreamSlaveUart1_Poll(&aggregated_status);
#endif
#if MODBUS_MASTER_POLL_ENABLE
      ModbusMaster_Poll();
#endif
    }
#endif
#endif
#endif
    if (AppScheduler_IsDue(TASK_AGGREGATE_UPDATE))
      SystemSync_Update(&aggregated_status, HAL_GetTick());
    Gateway_Action_Update();
#if !MB_UART2_ASCII_BRIDGE_TEST
#if !USE_PC_TEST_UART1_SLAVE
    if (AppScheduler_IsDue(TASK_UPSTREAM_SEND_STATUS))
      UpstreamPC_SendStatus(&aggregated_status);
#endif
#endif

    /* 진단 단계(윈도우 위반 배제):
     * - 비즈니스 루프는 유지
     * - 리프레시는 20ms마다 1회만 수행(5.5ms~49ms 윈도우 내) */
    static uint32_t last_wwdg_ms = 0;
    uint32_t now_ms = HAL_GetTick();
    if ((uint32_t)(now_ms - last_wwdg_ms) >= 20u) {
      (void)HAL_WWDG_Refresh(&hwwdg);
      last_wwdg_ms = now_ms;
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 100000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

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
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief WWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_WWDG_Init(void)
{

  /* USER CODE BEGIN WWDG_Init 0 */

  /* USER CODE END WWDG_Init 0 */

  /* USER CODE BEGIN WWDG_Init 1 */

  /* USER CODE END WWDG_Init 1 */
  hwwdg.Instance = WWDG;
  hwwdg.Init.Prescaler = WWDG_PRESCALER_8;
  hwwdg.Init.Window = 120;
  hwwdg.Init.Counter = 127;
  hwwdg.Init.EWIMode = WWDG_EWI_DISABLE;
  if (HAL_WWDG_Init(&hwwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN WWDG_Init 2 */

  /* USER CODE END WWDG_Init 2 */

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, RELAY3_EN_Pin|RELAY4_EN_Pin|RELAY1_EN_Pin|RELAY2_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, PC_RESET_EN_Pin|PC_ON_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, PC_RS485_DE_RE_Pin|LED03_Pin|LED04_Pin|RS_485_DE_RE_Pin
                          |LED01_Pin|LED02_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : RELAY3_EN_Pin RELAY4_EN_Pin RELAY1_EN_Pin RELAY2_EN_Pin */
  GPIO_InitStruct.Pin = RELAY3_EN_Pin|RELAY4_EN_Pin|RELAY1_EN_Pin|RELAY2_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : DI_01_Pin DI_02_Pin DI_03_Pin DI_04_Pin
                           DI_05_Pin DI_06_Pin DI_07_Pin DI_08_Pin */
  GPIO_InitStruct.Pin = DI_01_Pin|DI_02_Pin|DI_03_Pin|DI_04_Pin
                          |DI_05_Pin|DI_06_Pin|DI_07_Pin|DI_08_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PC_RESET_EN_Pin PC_ON_EN_Pin */
  GPIO_InitStruct.Pin = PC_RESET_EN_Pin|PC_ON_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PC_LED_IN_Pin */
  GPIO_InitStruct.Pin = PC_LED_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PC_LED_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC_RS485_DE_RE_Pin RS_485_DE_RE_Pin */
  GPIO_InitStruct.Pin = PC_RS485_DE_RE_Pin|RS_485_DE_RE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : LED03_Pin LED04_Pin LED01_Pin LED02_Pin */
  GPIO_InitStruct.Pin = LED03_Pin|LED04_Pin|LED01_Pin|LED02_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

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
