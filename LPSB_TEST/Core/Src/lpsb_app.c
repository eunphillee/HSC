/**
 ******************************************************************************
 * @file    lpsb_app.c
 * @brief   LPSB: LED 순차점등, SSR 제어, ID 스위치, heartbeat.
 ******************************************************************************
 */
#include "lpsb_app.h"
#include "main.h"

static uint8_t  s_slave_id = 2;
static uint8_t  s_ssr[3] = {0, 0, 0};
static uint16_t s_heartbeat = 0;
static uint32_t s_heartbeat_tick = 0;
static uint32_t s_led04_blink_tick = 0;

static GPIO_TypeDef *const SSR_Port[] = { SSR1_EN_GPIO_Port, SSR2_EN_GPIO_Port, SSR3_EN_GPIO_Port };
static uint16_t const SSR_Pin[]       = { SSR1_EN_Pin,       SSR2_EN_Pin,       SSR3_EN_Pin       };
static GPIO_TypeDef *const LED_Port[] = { LED01_GPIO_Port,   LED02_GPIO_Port,   LED03_GPIO_Port   };
static uint16_t const LED_Pin[]       = { LED01_Pin,         LED02_Pin,         LED03_Pin         };

void LPSB_LED_Sequence(void)
{
  const uint32_t d = 120;
  HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET);
  HAL_Delay(d);
  HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET);
  HAL_Delay(d);
  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_RESET);
  HAL_Delay(d);
  HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET);
  HAL_Delay(d);
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET);
}

void LPSB_LED_RxBlink(void)
{
  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET);
  s_led04_blink_tick = HAL_GetTick();
}

void LPSB_Heartbeat(void)
{
  uint32_t now = HAL_GetTick();
  if (now - s_led04_blink_tick < 80u)
    return;
  if (now - s_heartbeat_tick >= 500u)
  {
    s_heartbeat_tick = now;
    s_heartbeat++;
    /* 이전에는 여기서 LED01을 heartbeat 용으로 토글했지만,
       이제 LED01~3은 SSR 상태 표시 전용으로 사용하므로 토글을 제거한다. */
  }
}

void LPSB_SSR_Set(uint8_t ch, uint8_t on)
{
  if (ch >= 3) return;
  s_ssr[ch] = on ? 1u : 0u;
  HAL_GPIO_WritePin(SSR_Port[ch], SSR_Pin[ch], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  /* SSR 상태를 LED1/2/3에 그대로 반영 (회로상 LED는 active-low: LOW=ON, HIGH=OFF) */
  HAL_GPIO_WritePin(LED_Port[ch], LED_Pin[ch], on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

uint8_t LPSB_SSR_Get(uint8_t ch)
{
  if (ch >= 3) return 0;
  return s_ssr[ch];
}

uint8_t LPSB_ID_Read(void)
{
  uint8_t b0 = HAL_GPIO_ReadPin(ID_BIT1_GPIO_Port, ID_BIT1_Pin) == GPIO_PIN_SET ? 1u : 0u;
  uint8_t b1 = HAL_GPIO_ReadPin(ID_BIT2_GPIO_Port, ID_BIT2_Pin) == GPIO_PIN_SET ? 1u : 0u;
  uint8_t b2 = HAL_GPIO_ReadPin(ID_BIT3_GPIO_Port, ID_BIT3_Pin) == GPIO_PIN_SET ? 1u : 0u;
  uint8_t b3 = HAL_GPIO_ReadPin(ID_BIT4_GPIO_Port, ID_BIT4_Pin) == GPIO_PIN_SET ? 1u : 0u;
  uint8_t id = (uint8_t)(b0 * 1u + b1 * 2u + b2 * 4u + b3 * 8u);
  if (id == 0u || id > 15u) id = 2u;
  return id;
}

uint8_t LPSB_GetSlaveID(void)
{
  return s_slave_id;
}

/** 부팅 시 1회: ID 스위치 읽어 s_slave_id 설정. */
void LPSB_App_Init(void)
{
  s_slave_id = LPSB_ID_Read();
}

uint16_t LPSB_GetHeartbeatCount(void)
{
  return s_heartbeat;
}
