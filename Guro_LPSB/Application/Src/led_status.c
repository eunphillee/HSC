/**
 * @file led_status.c
 * @brief LPSB LED policy:
 *   LED1(PWR)=always ON when board power valid.
 *   When LED_DIAG_COMM_OUTPUT==1 (communication diagnostic mode):
 *     LED2 = SSR1 (coil 0) status, LED3 = SSR2 (coil 1), LED4 = SSR3 (coil 2).
 *   When LED_DIAG_COMM_OUTPUT==0: LED2=any output, LED3=current/fault, LED4=RS485.
 *   Board: LED01~04 LOW active (LOW=ON, HIGH=OFF). SSR outputs: active-high (GPIO_SET=ON).
 *   Design: simplest for debugging / visual verification first; not production-polish.
 */
#include "led_status.h"
#include "main.h"
#include "io_map.h"

#ifndef LED_DIAG_COMM_OUTPUT
#define LED_DIAG_COMM_OUTPUT  1   /* 1 = LED2/3/4 show SSR1/2/3 for comm diagnostic */
#endif

#define RS485_PULSE_MS           30u
#define RS485_IDLE_THRESHOLD_MS  3000u   /* 1Hz blink only when no comm for > 3s */
#define RS485_1HZ_HALF_MS       500u
#define CURRENT_4HZ_HALF_MS     125u
#define LED3_1HZ_HALF_MS        500u
#ifndef LED3_NORMAL_SOLID
#define LED3_NORMAL_SOLID       1
#endif

/* LED01~04: LOW active (RESET=ON, SET=OFF) */
#define LED_PWR_ON()    HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_RESET)
#define LED_PWR_OFF()   HAL_GPIO_WritePin(LED01_GPIO_Port, LED01_Pin, GPIO_PIN_SET)
#define LED_OUTPUT_ON()  HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_RESET)
#define LED_OUTPUT_OFF() HAL_GPIO_WritePin(LED02_GPIO_Port, LED02_Pin, GPIO_PIN_SET)
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

#if LED_DIAG_COMM_OUTPUT
	LED_OUTPUT_OFF();
	LED_CUR_OFF();
	LED_RS485_OFF();
#else
	LED_OUTPUT_OFF();
	LED_CUR_ON();
	LED_RS485_OFF();
#endif
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

#if LED_DIAG_COMM_OUTPUT
	/* Communication diagnostic: LED2 = SSR1, LED3 = SSR2, LED4 = SSR3 (coil 0,1,2). Outputs active-high in io_map.c. */
	if (IO_LPSB_ReadCoil(LPSB_COIL_SSR1))
		LED_OUTPUT_ON();
	else
		LED_OUTPUT_OFF();
	if (IO_LPSB_ReadCoil(LPSB_COIL_SSR2))
		LED_CUR_ON();
	else
		LED_CUR_OFF();
	if (IO_LPSB_ReadCoil(LPSB_COIL_SSR3))
		LED_RS485_ON();
	else
		LED_RS485_OFF();
#else
	/* ----- LED2 (OUTPUT): any SSR active ----- */
	if (LPSB_IsAnyOutputActive())
		LED_OUTPUT_ON();
	else
		LED_OUTPUT_OFF();

	/* ----- LED3 (CURRENT/FAULT): state machine ----- */
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

	/* ----- LED4 (RS485): 30ms pulse or 1Hz when idle ----- */
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
#endif /* !LED_DIAG_COMM_OUTPUT */
}
