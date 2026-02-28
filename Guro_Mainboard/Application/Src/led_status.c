/**
 * @file led_status.c
 * @brief LED1(PWR)=always ON, LED2(DI_EVT)=80ms pulse on confirmed DI change,
 *        LED3(DO_EVT)=80ms pulse on DO change, LED4(RS485)=30ms pulse on TX/RX.
 *        DI debounce 20ms; pulse timers reload on consecutive events.
 *
 * Board: LED01~LED04 all LOW active (cathode to MCU; LOW=ON, HIGH=OFF).
 *        ON  -> GPIO_PIN_RESET,  OFF -> GPIO_PIN_SET.
 */
#include "led_status.h"
#include "main.h"
#include "io_map.h"

#define TICK_MS              1u
#define DI_DEBOUNCE_MS       20u
#define DI_EVT_PULSE_MS      80u
#define DO_EVT_PULSE_MS      80u
#define RS485_PULSE_MS       30u

/* LED01~04: all LOW active (LOW=ON, HIGH=OFF) */
#define LED_PWR_ON()    HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET)
#define LED_PWR_OFF()   HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET)
#define LED_DI_ON()     HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET)
#define LED_DI_OFF()    HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET)
#define LED_DO_ON()     HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_RESET)
#define LED_DO_OFF()    HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_SET)
#define LED_RS485_ON()  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET)
#define LED_RS485_OFF() HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET)

static uint32_t last_tick;
static uint16_t di_evt_timer_ms;
static uint16_t do_evt_timer_ms;
static uint16_t rs485_timer_ms;

/* DI: debounce state */
static uint16_t di_last_raw;
static uint16_t di_stable;
static uint16_t di_debounce_count;

/* DO: previous value for edge detect */
static uint16_t do_last;

void LED_Status_Init(void)
{
	last_tick = HAL_GetTick();
	di_evt_timer_ms = 0;
	do_evt_timer_ms = 0;
	rs485_timer_ms = 0;
	di_last_raw = IO_Main_ReadDI_Bitmap();
	di_stable = di_last_raw;
	di_debounce_count = 0;
	do_last = IO_Main_ReadDO_Bitmap();

	/* Initial: LED1(PWR)=ON, LED2~4=OFF (all LOW active) */
	LED_DI_OFF();
	LED_DO_OFF();
	LED_RS485_OFF();
	LED_PWR_ON();
}

void LED_Status_Tick_1ms(void)
{
	uint32_t now = HAL_GetTick();
	uint32_t elapsed = (now >= last_tick) ? (now - last_tick) : 0;
	if (elapsed == 0) return;
	last_tick = now;

	/* Cap elapsed to avoid big jumps */
	if (elapsed > 100u) elapsed = 100u;

	/* ----- LED1 (PWR): always ON ----- */
	LED_PWR_ON();

	/* ----- DI debounce and edge -> LED2 pulse ----- */
	uint16_t di_raw = IO_Main_ReadDI_Bitmap();
	if (di_raw != di_last_raw) {
		di_last_raw = di_raw;
		di_debounce_count = 0;
	} else {
		di_debounce_count += (uint16_t)elapsed;
		if (di_debounce_count >= DI_DEBOUNCE_MS) {
			di_debounce_count = DI_DEBOUNCE_MS;
			if (di_stable != di_raw) {
				di_stable = di_raw;
				LED_Status_OnDIChanged();
			}
		}
	}

	/* ----- DO edge -> LED3 pulse ----- */
	uint16_t do_now = IO_Main_ReadDO_Bitmap();
	if (do_now != do_last) {
		do_last = do_now;
		LED_Status_OnDOChanged();
	}

	/* ----- Pulse timers (reload on event in OnDI/OnDO/OnRS485) ----- */
	if (di_evt_timer_ms > 0) {
		if (di_evt_timer_ms <= elapsed) di_evt_timer_ms = 0;
		else di_evt_timer_ms -= (uint16_t)elapsed;
	}
	if (do_evt_timer_ms > 0) {
		if (do_evt_timer_ms <= elapsed) do_evt_timer_ms = 0;
		else do_evt_timer_ms -= (uint16_t)elapsed;
	}
	if (rs485_timer_ms > 0) {
		if (rs485_timer_ms <= elapsed) rs485_timer_ms = 0;
		else rs485_timer_ms -= (uint16_t)elapsed;
	}

	/* ----- Output state ----- */
	if (di_evt_timer_ms > 0) LED_DI_ON(); else LED_DI_OFF();
	if (do_evt_timer_ms > 0) LED_DO_ON(); else LED_DO_OFF();
	if (rs485_timer_ms > 0) LED_RS485_ON(); else LED_RS485_OFF();
}

void LED_Status_OnDIChanged(void)
{
	di_evt_timer_ms = DI_EVT_PULSE_MS;
}

void LED_Status_OnDOChanged(void)
{
	do_evt_timer_ms = DO_EVT_PULSE_MS;
}

void LED_Status_OnRS485Activity(void)
{
	rs485_timer_ms = RS485_PULSE_MS;
}
