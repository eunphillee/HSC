/**
 * @file io_map.h
 * @brief LPSB: Modbus address mapping - Coil (0x), Discrete (1x), Holding (4x), Input (3x).
 *        LSB-first, 8 bits per byte. Exact mapping per MODBUS_MAPPING.md.
 */
#ifndef IO_MAP_LPSB_H
#define IO_MAP_LPSB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COIL_COUNT           8
#define DISCRETE_COUNT       8
#define HOLDING_REG_COUNT    4
#define INPUT_REG_COUNT      2

#define COIL_START           0
#define DISCRETE_START       0
#define HOLDING_START        0
#define INPUT_REG_START      0

typedef enum {
    LPSB_COIL_SSR1 = 0,
    LPSB_COIL_SSR2,
    LPSB_COIL_SSR3,
    LPSB_COIL_RESERVED_4,
    LPSB_COIL_RESERVED_5,
    LPSB_COIL_RESERVED_6,
    LPSB_COIL_RESERVED_7,
    LPSB_COIL_RESERVED_8
} LpsbCoilIdx_t;

typedef enum {
    LPSB_DISCRETE_ID_BIT1 = 0,
    LPSB_DISCRETE_ID_BIT2,
    LPSB_DISCRETE_ID_BIT3,
    LPSB_DISCRETE_ID_BIT4,
    LPSB_DISCRETE_RESERVED_5,
    LPSB_DISCRETE_RESERVED_6,
    LPSB_DISCRETE_RESERVED_7,
    LPSB_DISCRETE_RESERVED_8
} LpsbDiscreteIdx_t;

typedef enum {
    LPSB_HOLDING_STATUS = 0,
    LPSB_HOLDING_ALARM  = 1,
    LPSB_HOLDING_RESERVED_2 = 2,
    LPSB_HOLDING_RESERVED_3 = 3
} LpsbHoldingRegIdx_t;

typedef enum {
    LPSB_INPUT_REG_DISCRETE_IMAGE = 0,
    LPSB_INPUT_REG_ADC_OR_RESERVED = 1
} LpsbInputRegIdx_t;

uint8_t IO_LPSB_ReadDiscrete(uint16_t idx);
void    IO_LPSB_WriteCoil(uint16_t idx, uint8_t value);
uint8_t IO_LPSB_ReadCoil(uint16_t idx);
void    IO_LPSB_ReadAllDiscrete(uint8_t *bits);
void    IO_LPSB_ReadAllCoils(uint8_t *bits);

#ifdef __cplusplus
}
#endif

#endif /* IO_MAP_LPSB_H */
