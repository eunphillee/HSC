/**
 * @file io_map.c
 * @brief MAIN board: local DIO read/write (GPIO) using enum mapping.
 *        HPSB current: read from aggregated Modbus image (HPSB InputReg 1,2,3).
 */
#include "io_map.h"
#include "modbus_table.h"
#include "main.h"

/* Map DI enum to GPIO pin/port */
static const struct { uint16_t pin; GPIO_TypeDef *port; } main_di_map[MAIN_DI_COUNT] = {
    { DI_01_Pin, DI_01_GPIO_Port },
    { DI_02_Pin, DI_02_GPIO_Port },
    { DI_03_Pin, DI_03_GPIO_Port },
    { DI_04_Pin, DI_04_GPIO_Port },
    { DI_05_Pin, DI_05_GPIO_Port },
    { DI_06_Pin, DI_06_GPIO_Port },
    { DI_07_Pin, DI_07_GPIO_Port },
    { DI_08_Pin, DI_08_GPIO_Port },
};

static const struct { uint16_t pin; GPIO_TypeDef *port; } main_do_map[MAIN_DO_COUNT] = {
    { RELAY1_EN_Pin, RELAY1_EN_GPIO_Port },
    { RELAY2_EN_Pin, RELAY2_EN_GPIO_Port },
    { RELAY3_EN_Pin, RELAY3_EN_GPIO_Port },
    { RELAY4_EN_Pin, RELAY4_EN_GPIO_Port },
};

uint8_t IO_Main_ReadDI(MainDiChannel_t ch)
{
    if (ch >= MAIN_DI_COUNT) return 0;
    return (HAL_GPIO_ReadPin(main_di_map[ch].port, main_di_map[ch].pin) == GPIO_PIN_SET) ? 1 : 0;
}

void IO_Main_WriteDO(MainDoChannel_t ch, uint8_t value)
{
    if (ch >= MAIN_DO_COUNT) return;
    HAL_GPIO_WritePin(main_do_map[ch].port, main_do_map[ch].pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t IO_Main_ReadDO(MainDoChannel_t ch)
{
    if (ch >= MAIN_DO_COUNT) return 0;
    return (HAL_GPIO_ReadPin(main_do_map[ch].port, main_do_map[ch].pin) == GPIO_PIN_SET) ? 1 : 0;
}

uint16_t IO_Main_ReadDI_Bitmap(void)
{
    uint16_t v = 0;
    for (int i = 0; i < MAIN_DI_COUNT && i < 16; i++)
        v |= (IO_Main_ReadDI((MainDiChannel_t)i) ? (1u << i) : 0);
    return v;
}

uint16_t IO_Main_ReadDO_Bitmap(void)
{
    uint16_t v = 0;
    for (int i = 0; i < MAIN_DO_COUNT && i < 4; i++)
        v |= (IO_Main_ReadDO((MainDoChannel_t)i) ? (1u << i) : 0);
    return v;
}

void IO_Main_WriteDO_Bitmap(uint16_t bitmap)
{
    /* Only bits 0..3 valid; bit3 RESERVED (write 0). */
    uint16_t v = bitmap & 0x0Fu;
    for (int i = 0; i < MAIN_DO_COUNT; i++)
        IO_Main_WriteDO((MainDoChannel_t)i, (v >> i) & 1u);
}

void IO_Main_ReadAllDI(uint8_t *bits)
{
    for (int i = 0; i < MAIN_DI_COUNT; i++)
        bits[i] = IO_Main_ReadDI((MainDiChannel_t)i);
}

uint16_t IO_ReadHpsbCurrentRaw(uint8_t ch_0_to_2)
{
    if (ch_0_to_2 > 2) return 0;
    return ModbusTable_GetInputReg(SLAVE_ID_HPSB, (uint16_t)(1 + ch_0_to_2));
}
