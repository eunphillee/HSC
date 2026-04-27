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
#include "upstream_slave_h2tech.h"
#include "modbus_master.h"
#include "gateway_actions.h"
#include "led_status.h"
#include "pc_test_aa_stream.h"
#include "reset_reason.h"
#include "wwdg_service.h"
#include "system_config.h"
#include "system_sync.h"
#include "board_rtc.h"
#include "main_auto_link.h"
#include "output_state_nvm.h"
#include "eeprom_24c02.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* EEPROM 단순 read/write 테스트 주소.
 * 레이아웃: 0x00~0x3F SystemConfig, 0x40~0x7F output_state_nvm A/B → 0xA0 이후 안전 */
#define EEPROM_BOOT_TEST_ADDR  0xA0u
#define EEPROM_BOOT_TEST_VAL   100u   /* 0x64 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;

RTC_HandleTypeDef hrtc;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_tx;

WWDG_HandleTypeDef hwwdg;

/* USER CODE BEGIN PV */
static aggregated_status_t aggregated_status;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_WWDG_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief EEPROM 단순 read/write 부팅 테스트.
 *        주소 0xA0에 100(0x64)을 쓰고 즉시 읽어 일치 여부를 UART1로 출력.
 *        output_state_nvm/SystemConfig 영역(0x00~0x7F)과 겹치지 않음.
 *        이 함수는 부팅 1회만 호출하고 필요 없으면 호출부를 주석 처리하면 됩니다.
 */
__attribute__((unused)) static void Eeprom_RunBootTest(void)
{
    extern UART_HandleTypeDef huart1;

    uint8_t w = EEPROM_BOOT_TEST_VAL;
    uint8_t r = 0u;
    int wr = EEPROM_Write(EEPROM_BOOT_TEST_ADDR, &w, 1u);
    int rd = EEPROM_Read (EEPROM_BOOT_TEST_ADDR, &r, 1u);

    char buf[96];
    int n;

    n = snprintf(buf, sizeof(buf),
                 "[EEPROM-TEST] WR ret=%d val=%u\r\n", wr, (unsigned)w);
    if (n > 0) (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);

    n = snprintf(buf, sizeof(buf),
                 "[EEPROM-TEST] RD ret=%d val=%u\r\n", rd, (unsigned)r);
    if (n > 0) (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);

    int pass = (wr == 0) && (rd == 0) && (r == EEPROM_BOOT_TEST_VAL);
    n = snprintf(buf, sizeof(buf),
                 "[EEPROM-TEST] addr=0x%02X %s\r\n",
                 (unsigned)EEPROM_BOOT_TEST_ADDR, pass ? "PASS" : "FAIL");
    if (n > 0) (void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
}
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
  MX_DMA_Init();
  MX_WWDG_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  BoardRtc_Init();
  /* EEPROM 설정 로드: 실패 또는 검증 실패 시 기본값 저장 후 적용 */
  {
    system_config_t cfg;
    if (SystemConfig_Load(&cfg) != 0 || SystemConfig_Validate(&cfg) != 0) {
      SystemConfig_SetDefaults(&cfg);
      SystemConfig_Save(&cfg);
    }
    /* 출력 상태 로드(HPSB/LPSB 복원용). Mainboard RELAY1~4는 EEPROM으로 GPIO 복원하지 않음(항상 0 시작). */
    {
      output_state_nvm_t out_state;
      (void)OutputStateNvm_Load(&out_state);
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
    UpstreamSlave_InitMainboardSlavePending();
  }
  /* 정상 동작 모드: 스케줄러/통신/집계/LED 상태 및 WWDG 서비스 초기화. */
  WwdgService_Init(&hwwdg);
  AppScheduler_Init();
  ModbusMaster_Init();
  /* 부팅 즉시 모든 하위보드 1회 poll 요청: PC 미연결 상태에서도 comm_ok 확정 후 NVM 복원이 동작하도록 함.
   * poll 성공 → comm_ok=1 → OutputStateNvm_RestoreSubBoardsIfNeeded가 복원 실행. */
  ModbusMaster_RequestOnDemandPoll((uint16_t)SLAVE_ID_HPSB  | (uint16_t)SLAVE_ID_LPSB1 |
                                   (uint16_t)SLAVE_ID_LPSB2 | (uint16_t)SLAVE_ID_LPSB3);
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
  MainAutoLink_Init();
  {
    const output_state_nvm_t *out_state = OutputStateNvm_Get();
    if (out_state != NULL) {
      OutputStateNvm_ApplyMainboardRelays(out_state);
    }
  }
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
    MainAutoLink_Tick();
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
      OutputStateNvm_RestoreSubBoardsIfNeeded();
      OutputStateNvm_UpdateRestoreOkFromFeedback();
      OutputStateNvm_KeepAliveIfNeeded();
      OutputStateNvm_FlushIfDirty();
/* [STATE_MB] 로그는 MODBUS_MASTER_DEBUG_LOG=1 시에만 출력.
       * 기본 0 상태에서 이 printf가 UART1(PC Modbus 라인)에 섞이면
       * 모든 FC04 응답 프레임이 깨져 No Response / Unable to decode response 발생. */
#if MODBUS_MASTER_DEBUG_LOG
      {
        static uint32_t s_state_mb_last_tick;
        uint32_t tnow = HAL_GetTick();
        if (s_state_mb_last_tick == 0u)
          s_state_mb_last_tick = tnow;
        if ((uint32_t)(tnow - s_state_mb_last_tick) >= 1000u) {
          s_state_mb_last_tick = tnow;
          (void)printf("[STATE_MB] relay1=%u relay2=%u relay3=%u (HPSB)\r\n",
                       ModbusTable_GetCoil(SLAVE_ID_HPSB, 0) ? 1u : 0u,
                       ModbusTable_GetCoil(SLAVE_ID_HPSB, 1) ? 1u : 0u,
                       ModbusTable_GetCoil(SLAVE_ID_HPSB, 2) ? 1u : 0u);
          (void)printf("[STATE_MB] ssr1=%u ssr2=%u ssr3=%u (LPSB)\r\n",
                       ModbusTable_GetCoil(SLAVE_ID_LPSB1, 0) ? 1u : 0u,
                       ModbusTable_GetCoil(SLAVE_ID_LPSB1, 1) ? 1u : 0u,
                       ModbusTable_GetCoil(SLAVE_ID_LPSB1, 2) ? 1u : 0u);
        }
      }
#endif
#endif
    }
#endif
#endif
#endif
    UpstreamSlave_PcWatchdogTick();
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
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
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
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
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
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

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
  huart2.Init.BaudRate = 38400;
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
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

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
