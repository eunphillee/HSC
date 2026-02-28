/**
 * @file led_status.c
 * @brief HPSB LED policy: LED1(PWR)=always ON; LED2(RELAY)=HPSB_IsAnyRelayActive(); LED3(CURRENT/FAULT)=normal/4Hz/2-blink; LED4(RS485)=30ms pulse or 1Hz idle.
 *        Board: LED01~04 LOW active (LOW=ON, HIGH=OFF). 1ms tick; flag-based, no LED toggle in ISR.
 */
#include "led_status.h"
#include "main.h"
#include "io_map.h"

#define RS485_PULSE_MS           30u
#define RS485_IDLE_THRESHOLD_MS  3000u   /* 1Hz blink only when no comm for > 3s */
#define RS485_1HZ_HALF_MS       500u
#define CURRENT_4HZ_HALF_MS     125u
#define LED3_1HZ_HALF_MS        500u
/* NORMAL: 1=solid ON, 0=1Hz blink (compile-time option) */
#ifndef LED3_NORMAL_SOLID
#define LED3_NORMAL_SOLID       1
#endif

/* LED01~04: LOW active */
#define LED_PWR_ON()    HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET)
#define LED_PWR_OFF()   HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET)
#define LED_RELAY_ON()  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET)
#define LED_RELAY_OFF() HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET)
#define LED_CUR_ON()    HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_RESET)
#define LED_CUR_OFF()   HAL_GPIO_WritePin(LED03_GPIO_Port, LED03_Pin, GPIO_PIN_SET)
#define LED_RS485_ON()  HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_RESET)
#define LED_RS485_OFF() HAL_GPIO_WritePin(LED04_GPIO_Port, LED04_Pin, GPIO_PIN_SET)

static uint32_t last_tick;

/* LED4(RS485): last_comm_tick_ms is updated only in LED_Status_OnRS485Activity(). 1Hz blink only when (now - last_comm_tick_ms) > 3000. */
static uint32_t last_comm_tick_ms;
static uint16_t rs485_pulse_ms;

/* LED3(CURRENT/FAULT): state machine phase, reset on fault change */
static volatile LED_CurrentFault_t current_fault = LED_CURRENT_NORMAL;
static LED_CurrentFault_t prev_fault = (LED_CurrentFault_t)-1;
static uint16_t cur_4hz_phase_ms;
static uint16_t cur_1hz_phase_ms;   /* for NORMAL when LED3_NORMAL_SOLID==0 */
static uint16_t sensor_phase_ms;    /* 0..1000 ms cycle for 2-blink repeat */

void LED_Status_Init(void)
{
	last_tick = HAL_GetTick();
	last_comm_tick_ms = last_tick;
	rs485_pulse_ms = 0;
	current_fault = LED_CURRENT_NORMAL;
	prev_fault = (LED_CurrentFault_t)-1;
	cur_4hz_phase_ms = 0;
	cur_1hz_phase_ms = 0;
	sensor_phase_ms = 0;

	LED_RELAY_OFF();
	LED_CUR_ON();
	LED_RS485_OFF();
	LED_PWR_ON();
}

void LED_Status_SetCurrentFault(LED_CurrentFault_t fault)
{
	current_fault = fault;
}

void LED_Status_OnRS485Activity(void)
{
	rs485_pulse_ms = RS485_PULSE_MS;
	last_comm_tick_ms = HAL_GetTick();
}

void LED_Status_Tick_1ms(void)
{
	uint32_t now = HAL_GetTick();
	uint32_t elapsed = (now >= last_tick) ? (now - last_tick) : 0;
	if (elapsed == 0) return;
	last_tick = now;
	if (elapsed > 100u) elapsed = 100u;

	/* ----- LED1 (PWR): always ON ----- */
	LED_PWR_ON();

	/* ----- LED2 (RELAY): HPSB_IsAnyRelayActive() (can later use actual output when protection/force-off added) ----- */
	if (HPSB_IsAnyRelayActive())
		LED_RELAY_ON();
	else
		LED_RELAY_OFF();

	/* ----- LED3 (CURRENT/FAULT): state machine, phase reset on fault change ----- */
	if (current_fault != prev_fault) {
		prev_fault = current_fault;
		cur_4hz_phase_ms = 0;
		cur_1hz_phase_ms = 0;
		sensor_phase_ms = 0;
	}

	switch (current_fault) {
	case LED_CURRENT_NORMAL:
#if LED3_NORMAL_SOLID
		LED_CUR_ON();
#else
		cur_1hz_phase_ms += (uint16_t)elapsed;
		if (cur_1hz_phase_ms >= LED3_1HZ_HALF_MS * 2u)
			cur_1hz_phase_ms -= LED3_1HZ_HALF_MS * 2u;
		if (cur_1hz_phase_ms < LED3_1HZ_HALF_MS)
			LED_CUR_ON();
		else
			LED_CUR_OFF();
#endif
		break;
	case LED_CURRENT_OVERCURRENT:
		cur_4hz_phase_ms += (uint16_t)elapsed;
		if (cur_4hz_phase_ms >= CURRENT_4HZ_HALF_MS * 2u)
			cur_4hz_phase_ms -= CURRENT_4HZ_HALF_MS * 2u;
		if (cur_4hz_phase_ms < CURRENT_4HZ_HALF_MS)
			LED_CUR_ON();
		else
			LED_CUR_OFF();
		break;
	case LED_CURRENT_SENSOR_ERROR:
		sensor_phase_ms += (uint16_t)elapsed;
		if (sensor_phase_ms >= 1000u)
			sensor_phase_ms -= 1000u;
		if (sensor_phase_ms < 125u || (sensor_phase_ms >= 250u && sensor_phase_ms < 375u))
			LED_CUR_ON();
		else
			LED_CUR_OFF();
		break;
	default:
		LED_CUR_ON();
		break;
	}

	/* ----- LED4 (RS485): priority 1) 30ms pulse ON, 2) if (now - last_comm_tick_ms) > 3000 then 1Hz blink, 3) else OFF ----- */
	if (rs485_pulse_ms > 0) {
		if (rs485_pulse_ms <= elapsed)
			rs485_pulse_ms = 0;
		else
			rs485_pulse_ms -= (uint16_t)elapsed;
	}

	if (rs485_pulse_ms > 0) {
		LED_RS485_ON();
	} else {
		uint32_t idle_ms = (now >= last_comm_tick_ms) ? (now - last_comm_tick_ms) : 0;
		if (idle_ms > RS485_IDLE_THRESHOLD_MS) {
			uint32_t t = (idle_ms - RS485_IDLE_THRESHOLD_MS) % (RS485_1HZ_HALF_MS * 2u);
			if (t < RS485_1HZ_HALF_MS)
				LED_RS485_ON();
			else
				LED_RS485_OFF();
		} else {
			LED_RS485_OFF();
		}
	}
}
