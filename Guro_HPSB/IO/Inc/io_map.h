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
#define INPUT_REG_COUNT     16

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

/* Input register indices (3x / FC04 Input Register): Unified Rule v1.1 (0-based) */
typedef enum {
    HPSB_INPUT_REG_ALIVE_STATUS = 0,  /* reg0  alive/status (1=normal) */
    HPSB_INPUT_REG_ERROR_CODE   = 1,  /* reg1  error code */
    HPSB_INPUT_REG_RELAY1_STATE = 2,  /* reg2  relay1 state */
    HPSB_INPUT_REG_RELAY2_STATE = 3,  /* reg3  relay2 state */
    HPSB_INPUT_REG_RELAY3_STATE = 4,  /* reg4  relay3 state */
    HPSB_INPUT_REG_RELAY4_STATE = 5,  /* reg5  relay4 state */
    HPSB_INPUT_REG_ADC1_AVG     = 6,  /* reg6  ADC1 AVG */
    HPSB_INPUT_REG_ADC2_AVG     = 7,  /* reg7  ADC2 AVG */
    HPSB_INPUT_REG_ADC3_AVG     = 8,  /* reg8  ADC3 AVG */
    HPSB_INPUT_REG_ADC1_PKPK    = 9,  /* reg9  ADC1 PKPK */
    HPSB_INPUT_REG_ADC2_PKPK    = 10, /* reg10 ADC2 PKPK */
    HPSB_INPUT_REG_ADC3_PKPK    = 11, /* reg11 ADC3 PKPK */
    HPSB_INPUT_REG_CUR1_STATE   = 12, /* reg12 Current1 state */
    HPSB_INPUT_REG_CUR2_STATE   = 13, /* reg13 Current2 state */
    HPSB_INPUT_REG_CUR3_STATE   = 14, /* reg14 Current3 state */
    HPSB_INPUT_REG_RESERVED_15  = 15  /* reg15 reserve */
} HpsbInputRegIdx_t;

uint8_t IO_HPSB_ReadDiscrete(uint16_t idx);
void    IO_HPSB_WriteCoil(uint16_t idx, uint8_t value);
uint8_t IO_HPSB_ReadCoil(uint16_t idx);
void    IO_HPSB_ReadAllDiscrete(uint8_t *bits);
void    IO_HPSB_ReadAllCoils(uint8_t *bits);

/** Whether any of the 3 relay outputs is active (for LED2). Replace with actual output when protection/force-off is added. */
uint8_t HPSB_IsAnyRelayActive(void);

#ifdef __cplusplus
}
#endif

#endif /* IO_MAP_HPSB_H */
