/**
 * @file led_status.h
 * @brief MAIN board LED1~4 status: PWR always on, DI/DO/RS485 event pulses.
 *        Polling/timer-based; call LED_Status_Tick_1ms() from 1ms (or 10ms) periodic.
 */
#ifndef LED_STATUS_H
#define LED_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

void LED_Status_Init(void);
void LED_Status_Tick_1ms(void);

void LED_Status_OnDIChanged(void);
void LED_Status_OnDOChanged(void);
void LED_Status_OnRS485Activity(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_STATUS_H */
