/**
 * @file led_status.c
 * @brief HPSB LED policy (Unified v2.3):
 *   LED1 = RELAY1 state, LED2 = RELAY2 state, LED3 = RELAY3 state (coil direct mirror).
 *   LED4 = 평상시 ON, RS485 RX/TX 활동 시 80ms 동안 OFF (activity blink).
 *   Board: LED01~04 LOW active (LOW=ON, HIGH=OFF). Relay outputs: active-high (GPIO_SET=ON).
 */
#include "led_status.h"
#include "main.h"
#include "io_map.h"


/* LED01~04: LOW active (RESET=ON, SET=OFF) */
#define LED1_ON()   HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET)
#define LED1_OFF()  HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET)
#define LED2_ON()   HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET)
#define LED2_OFF()  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET)
#define LED3_ON()   HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_RESET)
#define LED3_OFF()  HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_SET)
#define LED4_ON()   HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET)
#define LED4_OFF()  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET)

/* RS485 activity blink: 마지막 RX/TX 발생 시각 */
#define LED4_BLINK_OFF_MS  80u

static uint32_t s_last_tick;
static volatile uint32_t s_led4_activity_tick;

void LED_Status_Init(void)
{
    s_last_tick = HAL_GetTick();
    s_led4_activity_tick = 0u;
    LED1_OFF();
    LED2_OFF();
    LED3_OFF();
    LED4_ON();
}

void LED_Status_SetCurrentFault(LED_CurrentFault_t fault)
{
    (void)fault;   /* reserved: LED3 = RELAY3 state only */
}

void LED_Status_OnRS485Activity(void)
{
    s_led4_activity_tick = HAL_GetTick();
}

void LED_Status_Tick_1ms(void)
{
    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = (now >= s_last_tick) ? (now - s_last_tick) : 0u;
    if (elapsed == 0u) return;
    s_last_tick = now;
    if (elapsed > 100u) elapsed = 100u;

    /* ----- LED1/2/3: RELAY1/2/3 coil state (direct mirror) ----- */
    if (IO_HPSB_ReadCoil(HPSB_COIL_RLY01)) LED1_ON(); else LED1_OFF();
    if (IO_HPSB_ReadCoil(HPSB_COIL_RLY02)) LED2_ON(); else LED2_OFF();
    if (IO_HPSB_ReadCoil(HPSB_COIL_RLY03)) LED3_ON(); else LED3_OFF();

    /* ----- LED4: 평상시 ON, RS485 활동(RX/TX) 후 80ms 동안 OFF ----- */
    if ((now - s_led4_activity_tick) < LED4_BLINK_OFF_MS) {
        LED4_OFF();
    } else {
        LED4_ON();
    }
}
