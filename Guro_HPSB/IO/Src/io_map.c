/**
 * @file io_map.c
 * @brief HPSB: GPIO mapping for Coils (relays) and Discrete (ID bits). LSB-first.
 */
#include "io_map.h"
#include "main.h"

/* Coil index -> GPIO (RLY_EN01, 02, 03) */
static const struct { uint16_t pin; GPIO_TypeDef *port; } coil_gpio[COIL_COUNT] = {
    { RLY_EN01_Pin, RLY_EN01_GPIO_Port },
    { RLY_EN02_Pin, RLY_EN02_GPIO_Port },
    { RLY_EN03_Pin, RLY_EN03_GPIO_Port },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }
};

/* Discrete index -> GPIO (ID_BIT1..4) */
static const struct { uint16_t pin; GPIO_TypeDef *port; } discrete_gpio[DISCRETE_COUNT] = {
    { ID_BIT1_Pin, ID_BIT1_GPIO_Port },
    { ID_BIT2_Pin, ID_BIT2_GPIO_Port },
    { ID_BIT3_Pin, ID_BIT3_GPIO_Port },
    { ID_BIT4_Pin, ID_BIT4_GPIO_Port },
    { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }
};

uint8_t IO_HPSB_ReadDiscrete(uint16_t idx)
{
    if (idx >= DISCRETE_COUNT || discrete_gpio[idx].port == NULL) return 0;
    return (HAL_GPIO_ReadPin(discrete_gpio[idx].port, discrete_gpio[idx].pin) == GPIO_PIN_SET) ? 1 : 0;
}

void IO_HPSB_WriteCoil(uint16_t idx, uint8_t value)
{
    if (idx >= COIL_COUNT || coil_gpio[idx].port == NULL) return;
    HAL_GPIO_WritePin(coil_gpio[idx].port, coil_gpio[idx].pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t IO_HPSB_ReadCoil(uint16_t idx)
{
    if (idx >= COIL_COUNT || coil_gpio[idx].port == NULL) return 0;
    return (HAL_GPIO_ReadPin(coil_gpio[idx].port, coil_gpio[idx].pin) == GPIO_PIN_SET) ? 1 : 0;
}

void IO_HPSB_ReadAllDiscrete(uint8_t *bits)
{
    for (uint16_t i = 0; i < DISCRETE_COUNT; i++)
        bits[i] = IO_HPSB_ReadDiscrete(i);
}

void IO_HPSB_ReadAllCoils(uint8_t *bits)
{
    for (uint16_t i = 0; i < COIL_COUNT; i++)
        bits[i] = IO_HPSB_ReadCoil(i);
}
