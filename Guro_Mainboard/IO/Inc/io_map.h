/**
 * @file io_map.h
 * @brief MAIN board: enum-based I/O and Modbus address definitions.
 *        No magic numbers in logic; all addresses from this mapping.
 */
#ifndef IO_MAP_MAIN_H
#define IO_MAP_MAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Slave IDs (Modbus 0x address) ========== */
typedef enum {
    SLAVE_ID_HPSB  = 1,
    SLAVE_ID_LPSB1 = 2,
    SLAVE_ID_LPSB2 = 3,
    SLAVE_ID_LPSB3 = 4,
    SLAVE_ID_COUNT = 4
} SlaveId_t;

#define SLAVE_ID_FIRST   SLAVE_ID_HPSB
#define SLAVE_ID_LAST    SLAVE_ID_LPSB3

/* ========== Poll types (what MAIN reads/writes) ========== */
typedef enum {
    POLL_READ_DISCRETE,   /* FC02: Discrete inputs (1x) */
    POLL_READ_COIL,       /* FC01: Coils (0x) status */
    POLL_READ_HOLDING,    /* FC03: Holding registers (4x) */
    POLL_READ_INPUT_REG, /* FC04: Input registers (3x) */
    POLL_WRITE_COIL,     /* FC05/15 */
    POLL_WRITE_HOLDING,  /* FC06/16 */
    POLL_TYPE_COUNT
} PollType_t;

/* ========== Start addresses and counts (from MODBUS_MAPPING.md) ========== */
#define MODBUS_COIL_START           0
#define MODBUS_COIL_COUNT           8
#define MODBUS_DISCRETE_START       0
#define MODBUS_DISCRETE_COUNT       8
#define MODBUS_HOLDING_START        0
#define MODBUS_HOLDING_COUNT        4
#define MODBUS_INPUT_REG_START      0
#define MODBUS_INPUT_REG_COUNT      2

/* ========== MAIN local Digital Inputs (GPIO) ========== */
typedef enum {
    MAIN_DI_01 = 0,
    MAIN_DI_02,
    MAIN_DI_03,
    MAIN_DI_04,
    MAIN_DI_05,
    MAIN_DI_06,
    MAIN_DI_07,
    MAIN_DI_08,
    MAIN_DI_COUNT
} MainDiChannel_t;

/* ========== MAIN local Digital Outputs (Relays) ========== */
typedef enum {
    MAIN_DO_RELAY1 = 0,
    MAIN_DO_RELAY2,
    MAIN_DO_RELAY3,
    MAIN_DO_RELAY4,
    MAIN_DO_COUNT
} MainDoChannel_t;

/* ========== Sub-board image indices (for application) ========== */
/* HPSB: Discrete[0..7], Coil[0..7], Holding[0..3], InputReg[0..1] */
/* LPSB: same layout */
#define SUB_DISCRETE_COUNT    MODBUS_DISCRETE_COUNT
#define SUB_COIL_COUNT        MODBUS_COIL_COUNT
#define SUB_HOLDING_COUNT     MODBUS_HOLDING_COUNT
#define SUB_INPUT_REG_COUNT   MODBUS_INPUT_REG_COUNT

/* Holding register indices (status / alarm) */
typedef enum {
    HOLDING_REG_STATUS  = 0,
    HOLDING_REG_ALARM  = 1,
    HOLDING_REG_RESERVED_2 = 2,
    HOLDING_REG_RESERVED_3 = 3
} HoldingRegIdx_t;

/* Coil indices per sub-board: 0=first relay/SSR, 1=second, 2=third, 3..7 reserved */
typedef enum {
    COIL_0 = 0,
    COIL_1,
    COIL_2,
    COIL_3,
    COIL_4,
    COIL_5,
    COIL_6,
    COIL_7
} CoilIdx_t;

/* ========== MAIN local DIO API (implemented in io_map.c) ========== */
uint8_t IO_Main_ReadDI(MainDiChannel_t ch);
uint8_t IO_Main_ReadDO(MainDoChannel_t ch);
void    IO_Main_WriteDO(MainDoChannel_t ch, uint8_t value);
void    IO_Main_ReadAllDI(uint8_t *bits);

#ifdef __cplusplus
}
#endif

#endif /* IO_MAP_MAIN_H */
