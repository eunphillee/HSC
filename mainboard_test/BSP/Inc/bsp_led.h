/**
 * @file bsp_led.h
 * @brief Mainboard status LED driver (Low active).
 *        PB8=LED01, PB9=LED02, PB10=LED03, PB11=LED04.
 *        LOW = LED ON, HIGH = LED OFF. Initial state = OFF (HIGH).
 */
#ifndef BSP_LED_H
#define BSP_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* LED IDs (Low active: on = GPIO LOW, off = GPIO HIGH) */
#define BSP_LED_01  0
#define BSP_LED_02  1
#define BSP_LED_03  2
#define BSP_LED_04  3
#define BSP_LED_COUNT 4

void BSP_LED_Init(void);
void BSP_LED_On(uint8_t led_id);   /* LED ON  -> GPIO LOW  */
void BSP_LED_Off(uint8_t led_id);  /* LED OFF -> GPIO HIGH */
void BSP_LED_Toggle(uint8_t led_id);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
