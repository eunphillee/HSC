     /**
 * @file led_status.h
 * @brief HPSB LED1~4: PWR always on, RELAY any-on (HPSB_IsAnyRelayActive), CURRENT/FAULT (normal/overcurrent/sensor), RS485 pulse/idle blink.
 *        Call LED_Status_Tick_1ms() from 1ms periodic; set fault via LED_Status_SetCurrentFault(); RS485 from Modbus.
 *        In led_status.c: LED3_NORMAL_SOLID 1 = NORMAL solid ON, 0 = NORMAL 1Hz blink (compile-time).
 */
#ifndef LED_STATUS_H
#define LED_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

void LED_Status_Init(void);
void LED_Status_Tick_1ms(void);

/** Current/fault state for LED3 (flag; do not toggle LED in ISR) */
typedef enum {
	LED_CURRENT_NORMAL = 0,
	LED_CURRENT_OVERCURRENT,
	LED_CURRENT_SENSOR_ERROR
} LED_CurrentFault_t;

void LED_Status_SetCurrentFault(LED_CurrentFault_t fault);

/** Call on valid RS485 RX or TX (e.g. from Modbus poll context, not ISR) */
void LED_Status_OnRS485Activity(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_STATUS_H */
