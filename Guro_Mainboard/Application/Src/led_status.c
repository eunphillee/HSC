/**
 * @file led_status.c
 * @brief LED1(PWR)=always ON. UART1 Slave 3단계 디버그: LED4=수신(RxEvent 50ms), LED3=CRC OK 50ms, LED2=응답송신 직전 50ms.
 *        DI/DO/RS485 이벤트도 LED2/LED3에 OR로 표시. DI debounce 20ms.
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
#define SUB_RS485_PULSE_MS   40u   /* LED4: UART2 하위 폴링 TX/RX 시 */
#define UART1_SLAVE_TX_PULSE_MS  30u  /* LED3 pulse when UART1 slave sends response */
#define UART1_RX_EVT_PULSE_MS    50u  /* LED4: UART1 receive event (any byte) */
#define UART1_CRCOK_PULSE_MS     50u  /* LED3: CRC OK frame received */
#define UART1_TX_RESP_PULSE_MS   50u  /* LED2: response send (right before Transmit) */
#define PC_TEST_AA_LED_PULSE_MS  40u  /* LED2: 0xAA 송신 시작 시 40ms 펄스 (ENABLE_PC_TEST_AA_STREAM) */
#define PC_TEST_AA_TX_OK_PULSE_MS    20u   /* LED3: 0xAA Transmit HAL_OK */
#define PC_TEST_AA_TX_ERR_ON_MS      200u  /* LED4: 0xAA Transmit HAL_BUSY/TIMEOUT/ERROR */

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
static uint16_t sub_rs485_timer_ms;  /* LED4: 하위 RS485 (UART2) activity */
static uint16_t uart1_rx_evt_timer_ms;  /* LED4: UART1 raw receive event */
static uint16_t uart1_crc_ok_timer_ms;  /* LED3: CRC OK frame received */
static uint16_t uart1_tx_resp_timer_ms; /* LED2: response send (before Transmit) */
static uint16_t pc_test_aa_led_timer_ms; /* LED2: 0xAA 송신 시 40ms 펄스 */
static uint16_t pc_test_aa_tx_ok_timer_ms;  /* LED3: 0xAA Tx HAL_OK 20ms */
static uint16_t pc_test_aa_tx_err_timer_ms; /* LED4: 0xAA Tx error 200ms */

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
	sub_rs485_timer_ms = 0;
	uart1_rx_evt_timer_ms = 0;
	uart1_crc_ok_timer_ms = 0;
	uart1_tx_resp_timer_ms = 0;
	pc_test_aa_led_timer_ms = 0;
	pc_test_aa_tx_ok_timer_ms = 0;
	pc_test_aa_tx_err_timer_ms = 0;
	di_last_raw = IO_Main_ReadDI_Bitmap();
	di_stable = di_last_raw;
	di_debounce_count = 0;
	do_last = IO_Main_ReadDO_Bitmap();

	/* Initial: LED1(PWR)=ON, LED2~4=OFF (LED4는 UART1 수신 시 50ms 펄스) */
	LED_DI_OFF();
	LED_DO_OFF();
	LED_RS485_OFF();
	LED_PWR_ON();
}

void LED_Status_Tick_1ms(void)
{
	uint32_t now = HAL_GetTick();
	uint32_t elapsed = (now >= last_tick) ? (now - last_tick) : 0;

	if (elapsed > 0) {
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
		if (sub_rs485_timer_ms > 0) {
			if (sub_rs485_timer_ms <= elapsed) sub_rs485_timer_ms = 0;
			else sub_rs485_timer_ms -= (uint16_t)elapsed;
		}
		if (uart1_rx_evt_timer_ms > 0) {
			if (uart1_rx_evt_timer_ms <= elapsed) uart1_rx_evt_timer_ms = 0;
			else uart1_rx_evt_timer_ms -= (uint16_t)elapsed;
		}
		if (uart1_crc_ok_timer_ms > 0) {
			if (uart1_crc_ok_timer_ms <= elapsed) uart1_crc_ok_timer_ms = 0;
			else uart1_crc_ok_timer_ms -= (uint16_t)elapsed;
		}
		if (uart1_tx_resp_timer_ms > 0) {
			if (uart1_tx_resp_timer_ms <= elapsed) uart1_tx_resp_timer_ms = 0;
			else uart1_tx_resp_timer_ms -= (uint16_t)elapsed;
		}
		if (pc_test_aa_led_timer_ms > 0) {
			if (pc_test_aa_led_timer_ms <= elapsed) pc_test_aa_led_timer_ms = 0;
			else pc_test_aa_led_timer_ms -= (uint16_t)elapsed;
		}
		if (pc_test_aa_tx_ok_timer_ms > 0) {
			if (pc_test_aa_tx_ok_timer_ms <= elapsed) pc_test_aa_tx_ok_timer_ms = 0;
			else pc_test_aa_tx_ok_timer_ms -= (uint16_t)elapsed;
		}
		if (pc_test_aa_tx_err_timer_ms > 0) {
			if (pc_test_aa_tx_err_timer_ms <= elapsed) pc_test_aa_tx_err_timer_ms = 0;
			else pc_test_aa_tx_err_timer_ms -= (uint16_t)elapsed;
		}
	}

	/* ----- LED2=DI/RS485/UART1 TX resp/PC_TEST_AA, LED3=DO/UART1 CRCOK/PC_TEST_AA TxOk, LED4=UART1 RX evt/PC_TEST_AA TxErr ----- */
	if (di_evt_timer_ms > 0 || rs485_timer_ms > 0 || uart1_tx_resp_timer_ms > 0 || pc_test_aa_led_timer_ms > 0) LED_DI_ON(); else LED_DI_OFF();
	if (do_evt_timer_ms > 0 || uart1_crc_ok_timer_ms > 0 || pc_test_aa_tx_ok_timer_ms > 0) LED_DO_ON(); else LED_DO_OFF();
	if (uart1_rx_evt_timer_ms > 0 || pc_test_aa_tx_err_timer_ms > 0 || sub_rs485_timer_ms > 0) LED_RS485_ON(); else LED_RS485_OFF();  /* LED4: UART1 rx / PC_TEST err / 하위 RS485 */
	LED_PWR_ON();
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

void LED_Status_OnSubRS485Activity(void)
{
	sub_rs485_timer_ms = SUB_RS485_PULSE_MS;
}

void LED_Status_OnUart1SlaveTx(void)
{
	do_evt_timer_ms = UART1_SLAVE_TX_PULSE_MS;
}

/** UART1 raw receive event (≥1 byte): pulse LED4 for 50ms (3단계 디버그 1단계). */
void LED_Status_OnUart1RxEvent(void)
{
	uart1_rx_evt_timer_ms = UART1_RX_EVT_PULSE_MS;
}

/** UART1 CRC OK frame: pulse LED3 for 50ms (3단계 디버그 2단계). */
void LED_Status_OnUart1CrcOk(void)
{
	uart1_crc_ok_timer_ms = UART1_CRCOK_PULSE_MS;
}

/** UART1 응답 송신 직전: pulse LED2 for 50ms (3단계 디버그 3단계). */
void LED_Status_OnUart1TxRespBefore(void)
{
	uart1_tx_resp_timer_ms = UART1_TX_RESP_PULSE_MS;
}

/** PC 테스트 0xAA 송신 시작 시 LED2 40ms 펄스 (ENABLE_PC_TEST_AA_STREAM). */
void LED_Status_OnPcTestAASend(void)
{
	pc_test_aa_led_timer_ms = PC_TEST_AA_LED_PULSE_MS;
}

/** PC 테스트 0xAA: HAL_UART_Transmit HAL_OK → LED3 20ms 펄스. */
void LED_Status_OnPcTestAATxOk(void)
{
	pc_test_aa_tx_ok_timer_ms = PC_TEST_AA_TX_OK_PULSE_MS;
}

/** PC 테스트 0xAA: HAL_UART_Transmit HAL_BUSY/TIMEOUT/ERROR → LED4 200ms 점등. */
void LED_Status_OnPcTestAATxError(void)
{
	pc_test_aa_tx_err_timer_ms = PC_TEST_AA_TX_ERR_ON_MS;
}

/** PC 테스트 빌드 모드 확인: LED2를 count번 빠르게 점멸 (HAL_Delay 사용). */
void LED_Status_BootBlinkPcTestAA(uint8_t count)
{
	for (uint8_t i = 0; i < count; i++) {
		LED_DI_ON();
		HAL_Delay(80);
		LED_DI_OFF();
		HAL_Delay(80);
	}
}
