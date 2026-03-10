/**
 * @file bsp_gpio.c
 * @brief DI/DO/PC I/O. DI = opto-isolated 24V input. Relay = TBD62003 drive.
 */
#include "bsp_gpio.h"
#include "main.h"

static const struct { GPIO_TypeDef *p; uint16_t pin; } di_pins[8] = {
	{ DI_01_GPIO_Port, DI_01_Pin }, { DI_02_GPIO_Port, DI_02_Pin },
	{ DI_03_GPIO_Port, DI_03_Pin }, { DI_04_GPIO_Port, DI_04_Pin },
	{ DI_05_GPIO_Port, DI_05_Pin }, { DI_06_GPIO_Port, DI_06_Pin },
	{ DI_07_GPIO_Port, DI_07_Pin }, { DI_08_GPIO_Port, DI_08_Pin },
};

static const struct { GPIO_TypeDef *p; uint16_t pin; } relay_pins[4] = {
	{ RELAY1_EN_GPIO_Port, RELAY1_EN_Pin }, { RELAY2_EN_GPIO_Port, RELAY2_EN_Pin },
	{ RELAY3_EN_GPIO_Port, RELAY3_EN_Pin }, { RELAY4_EN_GPIO_Port, RELAY4_EN_Pin },
};

void BSP_GPIO_Init(void)
{
	/* DI/Relay/PC I/O are configured in MX_GPIO_Init(); ensure init order. */
	(void)0;
}

uint8_t BSP_ReadDI(uint8_t ch)
{
	if (ch >= 8) return 0;
	return (uint8_t)(HAL_GPIO_ReadPin(di_pins[ch].p, di_pins[ch].pin) == GPIO_PIN_SET ? 1 : 0);
}

void BSP_ReadAllDI(uint8_t *bits)
{
	uint8_t i;
	if (!bits) return;
	for (i = 0; i < 8; i++) bits[i] = BSP_ReadDI(i);
}

void BSP_WriteRelay(uint8_t ch, uint8_t on)
{
	if (ch >= 4) return;
	HAL_GPIO_WritePin(relay_pins[ch].p, relay_pins[ch].pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_WritePC_ON_EN(uint8_t level)
{
	HAL_GPIO_WritePin(PC_ON_EN_GPIO_Port, PC_ON_EN_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_WritePC_RESET_EN(uint8_t level)
{
	HAL_GPIO_WritePin(PC_RESET_EN_GPIO_Port, PC_RESET_EN_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t BSP_ReadPC_LED_IN(void)
{
	return (uint8_t)(HAL_GPIO_ReadPin(PC_LED_IN_GPIO_Port, PC_LED_IN_Pin) == GPIO_PIN_SET ? 1 : 0);
}
