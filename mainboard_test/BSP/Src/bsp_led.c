/**
 * @file bsp_led.c
 * @brief Mainboard LED control. All LEDs are Low active (LOW=ON, HIGH=OFF).
 */
#include "bsp_led.h"
#include "main.h"

static const struct {
	GPIO_TypeDef *port;
	uint16_t pin;
} led_pins[BSP_LED_COUNT] = {
	{ LED01_GPIO_Port, LED01_Pin },
	{ LED02_GPIO_Port, LED02_Pin },
	{ LED03_GPIO_Port, LED03_Pin },
	{ LED04_GPIO_Port, LED04_Pin },
};

void BSP_LED_Init(void)
{
	uint8_t i;
	for (i = 0; i < BSP_LED_COUNT; i++) {
		HAL_GPIO_WritePin(led_pins[i].port, led_pins[i].pin, GPIO_PIN_SET);
	}
}

void BSP_LED_On(uint8_t led_id)
{
	if (led_id >= BSP_LED_COUNT) return;
	HAL_GPIO_WritePin(led_pins[led_id].port, led_pins[led_id].pin, GPIO_PIN_RESET);
}

void BSP_LED_Off(uint8_t led_id)
{
	if (led_id >= BSP_LED_COUNT) return;
	HAL_GPIO_WritePin(led_pins[led_id].port, led_pins[led_id].pin, GPIO_PIN_SET);
}

void BSP_LED_Toggle(uint8_t led_id)
{
	if (led_id >= BSP_LED_COUNT) return;
	HAL_GPIO_TogglePin(led_pins[led_id].port, led_pins[led_id].pin);
}
