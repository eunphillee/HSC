/**
 * @file h2tech_address_map.h
 * @brief H2TECH address mapping: table-driven 1x discrete/coil for upstream Modbus Slave.
 *        H2TECH PDF "1xNNNN" = DEC logical address NNNN; Modbus start_addr = (NNNN - 1).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* H2TECH PDF uses "1xNNNN" notation. We treat NNNN as DEC logical address.
 * Modbus RTU frame uses 0-based starting address => start_addr = (NNNN - 1).
 * (Protocol example: 1x0197 -> 0x00C4) */
typedef enum {
    H2_AREA_1X = 0,
    H2_AREA_0X,
    H2_AREA_3X,
    H2_AREA_4X
} H2_Area_t;

typedef enum {
    H2_RW_READ  = 0,
    H2_RW_WRITE = 1
} H2_Rw_t;

typedef enum {
    H2_SRC_AGG_BIT = 0,
    H2_SRC_ACTION_PULSE,
    H2_SRC_CONST0
} H2_Source_t;

typedef enum {
    H2_ACT_NONE = 0,
    H2_ACT_PULSE_MAIN_DOOR1,
    H2_ACT_PULSE_MAIN_DOOR2,
    H2_ACT_TOGGLE_OUTPUT,
    H2_ACT_PULSE_OUTPUT
} H2_Action_t;

typedef struct {
    uint16_t h2_dec;
    H2_Area_t area;
    H2_Rw_t   rw;
    H2_Source_t src;
    uint16_t agg_bit_index;
    H2_Action_t action;
    const char* name;
} H2_MapEntry_t;

/* Aggregated status bit indices (unified bit-image for H2TECH 1x reads). */
typedef enum {
    AGG_BIT_ONOFF_1 = 0,
    AGG_BIT_ONOFF_2,
    AGG_BIT_ONOFF_3,
    AGG_BIT_ONOFF_4,
    AGG_BIT_ONOFF_5,
    AGG_BIT_ONOFF_6,
    AGG_BIT_ONOFF_7,
    AGG_BIT_ONOFF_8,
    AGG_BIT_ONOFF_9,
    AGG_BIT_ONOFF_10,
    AGG_BIT_ONOFF_11,
    AGG_BIT_ONOFF_12,
    AGG_BIT_ONOFF_13,
    AGG_BIT_ONOFF_14,
    AGG_BIT_ONOFF_15,
    AGG_BIT_ONOFF_16,

    AGG_BIT_DOOR_MAG_1,
    AGG_BIT_DOOR_MAG_2,
    AGG_BIT_DOOR_MAG_3,
    AGG_BIT_DOOR_MAG_4,
    AGG_BIT_DOOR_BTN_1,
    AGG_BIT_DOOR_BTN_2,
    AGG_BIT_DOOR_BTN_3,
    AGG_BIT_DOOR_BTN_4,

    AGG_BIT_ALM_1,
    AGG_BIT_ALM_2,
    AGG_BIT_ALM_3,
    AGG_BIT_ALM_4,
    AGG_BIT_ALM_5,
    AGG_BIT_ALM_6,
    AGG_BIT_ALM_7,
    AGG_BIT_ALM_8,
    AGG_BIT_ALM_9,
    AGG_BIT_ALM_10,
    AGG_BIT_ALM_11,
    AGG_BIT_ALM_12,

    AGG_BIT_CMD_ONOFF_1,
    AGG_BIT_CMD_ONOFF_2,
    AGG_BIT_CMD_ONOFF_3,
    AGG_BIT_CMD_ONOFF_4,
    AGG_BIT_CMD_ONOFF_5,
    AGG_BIT_CMD_ONOFF_6,
    AGG_BIT_CMD_ONOFF_7,

    AGG_BIT_COUNT
} AggBitIndex_t;

const H2_MapEntry_t* H2Map_FindByDec(H2_Area_t area, uint16_t h2_dec);
bool H2Map_ReadAggBit(uint16_t agg_bit_index);
void H2Map_WriteAggBit(uint16_t agg_bit_index, bool v);
bool H2Map_ApplyWrite(const H2_MapEntry_t* e, bool value, uint16_t pulse_ms);

/* Modbus start_addr -> H2TECH dec: h2_dec = start_addr + 1 */
static inline uint16_t H2Map_ModbusAddrToH2Dec(uint16_t start_addr) {
    return (uint16_t)(start_addr + 1);
}

#ifdef __cplusplus
}
#endif
