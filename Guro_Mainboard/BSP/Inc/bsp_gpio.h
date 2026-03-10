/**
 * @file bsp_gpio.h
 * @brief Mainboard GPIO: DI(PE4~PE11), DO Relay(PE0~PE3), PC I/O(PC0~PC2).
 *        DI = 24V opto-isolated input. DO = TBD62003 relay driver.
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void BSP_GPIO_Init(void);
uint8_t BSP_ReadDI(uint8_t ch);           /* ch 0~7 = DI_01~DI_08 */
void BSP_ReadAllDI(uint8_t *bits);        /* bits[8] */
void BSP_WriteRelay(uint8_t ch, uint8_t on);  /* ch 0~3 = RELAY1~4, on=1 drive */
void BSP_WritePC_ON_EN(uint8_t level);    /* PC1 */
void BSP_WritePC_RESET_EN(uint8_t level); /* PC0 */
uint8_t BSP_ReadPC_LED_IN(void);          /* PC2, positive logic */

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
