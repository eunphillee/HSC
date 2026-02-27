/**
 * @file io_map.h
 * @brief HPSB: Modbus address mapping - Coil (0x), Discrete (1x), Holding (4x), Input (3x).
 *        LSB-first, 8 bits per byte. Exact mapping per MODBUS_MAPPING.md.
 */
#ifndef IO_MAP_HPSB_H
#define IO_MAP_HPSB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Address counts (0-based Modbus addresses) */
#define COIL_COUNT           8
#define DISCRETE_COUNT       8
#define HOLDING_REG_COUNT    4
#define INPUT_REG_COUNT     7

#define COIL_START           0
#define DISCRETE_START       0
#define HOLDING_START        0
#define INPUT_REG_START      0

/* Coil indices (0x) - map to relay outputs */
typedef enum {
    HPSB_COIL_RLY01 = 0,
    HPSB_COIL_RLY02,
    HPSB_COIL_RLY03,
    HPSB_COIL_RESERVED_4,
    HPSB_COIL_RESERVED_5,
    HPSB_COIL_RESERVED_6,
    HPSB_COIL_RESERVED_7,
    HPSB_COIL_RESERVED_8
} HpsbCoilIdx_t;

/* Discrete input indices (1x) - map to ID bits / DI */
typedef enum {
    HPSB_DISCRETE_ID_BIT1 = 0,
    HPSB_DISCRETE_ID_BIT2,
    HPSB_DISCRETE_ID_BIT3,
    HPSB_DISCRETE_ID_BIT4,
    HPSB_DISCRETE_RESERVED_5,
    HPSB_DISCRETE_RESERVED_6,
    HPSB_DISCRETE_RESERVED_7,
    HPSB_DISCRETE_RESERVED_8
} HpsbDiscreteIdx_t;

/* Holding register indices (4x) */
typedef enum {
    HPSB_HOLDING_STATUS = 0,
    HPSB_HOLDING_ALARM  = 1,
    HPSB_HOLDING_RESERVED_2 = 2,
    HPSB_HOLDING_RESERVED_3 = 3
} HpsbHoldingRegIdx_t;

/* Input register indices (3x): Reg0=DI image, Reg1..3=CT raw ch1..3, Reg4..6=CT RMS x100 (optional, 0 for v1) */
typedef enum {
    HPSB_INPUT_REG_DISCRETE_IMAGE = 0,
    HPSB_INPUT_REG_CT_CH1_RAW = 1,
    HPSB_INPUT_REG_CT_CH2_RAW = 2,
    HPSB_INPUT_REG_CT_CH3_RAW = 3,
    HPSB_INPUT_REG_CT_CH1_RMS_X100 = 4,
    HPSB_INPUT_REG_CT_CH2_RMS_X100 = 5,
    HPSB_INPUT_REG_CT_CH3_RMS_X100 = 6
} HpsbInputRegIdx_t;

uint8_t IO_HPSB_ReadDiscrete(uint16_t idx);
void    IO_HPSB_WriteCoil(uint16_t idx, uint8_t value);
uint8_t IO_HPSB_ReadCoil(uint16_t idx);
void    IO_HPSB_ReadAllDiscrete(uint8_t *bits);
void    IO_HPSB_ReadAllCoils(uint8_t *bits);

#ifdef __cplusplus
}
#endif

#endif /* IO_MAP_HPSB_H */
