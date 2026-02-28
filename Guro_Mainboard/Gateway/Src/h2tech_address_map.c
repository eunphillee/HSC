/**
 * @file h2tech_address_map.c
 * @brief H2TECH table-driven mapping: g_agg_bits image and g_map entries.
 *        Concrete mapping: 0821~0836, 0853~0860, 0869~0880, 0885~0891, 0892~0898.
 *        0899/0900 not in table -> exception 0x02.
 */
#include <stddef.h>
#include "h2tech_address_map.h"

static volatile uint8_t g_agg_bits[(AGG_BIT_COUNT + 7) / 8] = {0};

static inline void bit_set(volatile uint8_t* buf, uint16_t idx, bool v) {
    uint16_t byte = idx >> 3;
    uint8_t  mask = (uint8_t)(1u << (idx & 7u));
    if (v) buf[byte] |= mask;
    else   buf[byte] &= (uint8_t)~mask;
}

static inline bool bit_get(const volatile uint8_t* buf, uint16_t idx) {
    uint16_t byte = idx >> 3;
    uint8_t  mask = (uint8_t)(1u << (idx & 7u));
    return (buf[byte] & mask) != 0;
}

bool H2Map_ReadAggBit(uint16_t agg_bit_index) {
    if (agg_bit_index >= AGG_BIT_COUNT) return false;
    return bit_get(g_agg_bits, agg_bit_index);
}

void H2Map_WriteAggBit(uint16_t agg_bit_index, bool v) {
    if (agg_bit_index >= AGG_BIT_COUNT) return;
    bit_set(g_agg_bits, agg_bit_index, v);
}

#define H2E(_area, _dec, _rw, _src, _agg_bit, _act, _label) \
    { .h2_dec=(_dec), .area=(_area), .rw=(_rw), .src=(_src), .agg_bit_index=(_agg_bit), .action=(_act), .name=(_label) }

static const H2_MapEntry_t g_map[] = {
    /* 1x0821~0836 : ON/OFF 1~16 status (READ) */
    H2E(H2_AREA_1X, 821, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_1,  H2_ACT_NONE, "ONOFF_1_DOOR1"),
    H2E(H2_AREA_1X, 822, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_2,  H2_ACT_NONE, "ONOFF_2_DOOR2"),
    H2E(H2_AREA_1X, 823, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_3,  H2_ACT_NONE, "ONOFF_3_HPSB_CH1"),
    H2E(H2_AREA_1X, 824, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_4,  H2_ACT_NONE, "ONOFF_4_HPSB_CH2"),
    H2E(H2_AREA_1X, 825, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_5,  H2_ACT_NONE, "ONOFF_5_HPSB_CH3"),
    H2E(H2_AREA_1X, 826, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_6,  H2_ACT_NONE, "ONOFF_6_LPSB1_CH1"),
    H2E(H2_AREA_1X, 827, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_7,  H2_ACT_NONE, "ONOFF_7_LPSB1_CH2"),
    H2E(H2_AREA_1X, 828, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_8,  H2_ACT_NONE, "ONOFF_8_LPSB1_CH3"),
    H2E(H2_AREA_1X, 829, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_9,  H2_ACT_NONE, "ONOFF_9_LPSB2_CH1"),
    H2E(H2_AREA_1X, 830, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_10, H2_ACT_NONE, "ONOFF_10_LPSB2_CH2"),
    H2E(H2_AREA_1X, 831, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_11, H2_ACT_NONE, "ONOFF_11_LPSB2_CH3"),
    H2E(H2_AREA_1X, 832, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_12, H2_ACT_NONE, "ONOFF_12_LPSB3_CH1"),
    H2E(H2_AREA_1X, 833, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_13, H2_ACT_NONE, "ONOFF_13_LPSB3_CH2"),
    H2E(H2_AREA_1X, 834, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ONOFF_14, H2_ACT_NONE, "ONOFF_14_LPSB3_CH3"),
    H2E(H2_AREA_1X, 835, H2_RW_READ, H2_SRC_CONST0, 0,               H2_ACT_NONE, "ONOFF_15_RESERVED"),
    H2E(H2_AREA_1X, 836, H2_RW_READ, H2_SRC_CONST0, 0,               H2_ACT_NONE, "ONOFF_16_RESERVED"),

    /* 1x0853~0860 : Door sensors (READ) */
    H2E(H2_AREA_1X, 853, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_DOOR_MAG_1, H2_ACT_NONE, "DOOR_MAG_1"),
    H2E(H2_AREA_1X, 854, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_DOOR_MAG_2, H2_ACT_NONE, "DOOR_MAG_2"),
    H2E(H2_AREA_1X, 855, H2_RW_READ, H2_SRC_CONST0,  0,                 H2_ACT_NONE, "DOOR_MAG_3_UNUSED"),
    H2E(H2_AREA_1X, 856, H2_RW_READ, H2_SRC_CONST0,  0,                 H2_ACT_NONE, "DOOR_MAG_4_UNUSED"),
    H2E(H2_AREA_1X, 857, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_DOOR_BTN_1, H2_ACT_NONE, "DOOR_BTN_1"),
    H2E(H2_AREA_1X, 858, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_DOOR_BTN_2, H2_ACT_NONE, "DOOR_BTN_2"),
    H2E(H2_AREA_1X, 859, H2_RW_READ, H2_SRC_CONST0,  0,                 H2_ACT_NONE, "DOOR_BTN_3_UNUSED"),
    H2E(H2_AREA_1X, 860, H2_RW_READ, H2_SRC_CONST0,  0,                 H2_ACT_NONE, "DOOR_BTN_4_UNUSED"),

    /* 1x0869~0880 : Alarm 1~12 (READ) */
    H2E(H2_AREA_1X, 869, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_1,  H2_ACT_NONE, "ALM_1_HPSB_COMM"),
    H2E(H2_AREA_1X, 870, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_2,  H2_ACT_NONE, "ALM_2_LPSB_ANY_COMM"),
    H2E(H2_AREA_1X, 871, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_3,  H2_ACT_NONE, "ALM_3_SHTC3_FAIL"),
    H2E(H2_AREA_1X, 872, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_4,  H2_ACT_NONE, "ALM_4_DOOR_SENSOR_FAULT"),
    H2E(H2_AREA_1X, 873, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_5,  H2_ACT_NONE, "ALM_5_HPSB_OC1"),
    H2E(H2_AREA_1X, 874, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_6,  H2_ACT_NONE, "ALM_6_HPSB_OC2"),
    H2E(H2_AREA_1X, 875, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_7,  H2_ACT_NONE, "ALM_7_HPSB_OC3"),
    H2E(H2_AREA_1X, 876, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_8,  H2_ACT_NONE, "ALM_8_LPSB1_OC_ANY"),
    H2E(H2_AREA_1X, 877, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_9,  H2_ACT_NONE, "ALM_9_LPSB2_OC_ANY"),
    H2E(H2_AREA_1X, 878, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_10, H2_ACT_NONE, "ALM_10_LPSB3_OC_ANY"),
    H2E(H2_AREA_1X, 879, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_11, H2_ACT_NONE, "ALM_11_PC_LINK_FAIL"),
    H2E(H2_AREA_1X, 880, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_ALM_12, H2_ACT_NONE, "ALM_12_DOWNSTREAM_WRITE_FAIL"),

    /* 1x0885~0891 : ON/OFF 1~7 command/extra (READ) */
    H2E(H2_AREA_1X, 885, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_1, H2_ACT_NONE, "CMD_ONOFF_1"),
    H2E(H2_AREA_1X, 886, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_2, H2_ACT_NONE, "CMD_ONOFF_2"),
    H2E(H2_AREA_1X, 887, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_3, H2_ACT_NONE, "CMD_ONOFF_3"),
    H2E(H2_AREA_1X, 888, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_4, H2_ACT_NONE, "CMD_ONOFF_4"),
    H2E(H2_AREA_1X, 889, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_5, H2_ACT_NONE, "CMD_ONOFF_5"),
    H2E(H2_AREA_1X, 890, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_6, H2_ACT_NONE, "CMD_ONOFF_6"),
    H2E(H2_AREA_1X, 891, H2_RW_READ, H2_SRC_AGG_BIT, AGG_BIT_CMD_ONOFF_7, H2_ACT_NONE, "CMD_ONOFF_7"),

    /* 1x0892~0898 : Virtual buttons / Door open control (WRITE). 0899/0900 not in table -> 0x02 */
    H2E(H2_AREA_1X, 892, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_OUTPUT, "VB_ONOFF_8"),
    H2E(H2_AREA_1X, 893, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_OUTPUT, "VB_ONOFF_9"),
    H2E(H2_AREA_1X, 894, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_OUTPUT, "VB_ONOFF_10"),
    H2E(H2_AREA_1X, 895, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_OUTPUT, "VB_ONOFF_11"),
    H2E(H2_AREA_1X, 896, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_OUTPUT, "VB_ONOFF_12"),
    H2E(H2_AREA_1X, 897, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_MAIN_DOOR1, "DOOR_OPEN_CTRL_1"),
    H2E(H2_AREA_1X, 898, H2_RW_WRITE, H2_SRC_ACTION_PULSE, 0, H2_ACT_PULSE_MAIN_DOOR2, "DOOR_OPEN_CTRL_2"),
};

const H2_MapEntry_t* H2Map_FindByDec(H2_Area_t area, uint16_t h2_dec) {
    for (unsigned i = 0; i < (sizeof(g_map) / sizeof(g_map[0])); i++) {
        if (g_map[i].area == area && g_map[i].h2_dec == h2_dec) {
            return &g_map[i];
        }
    }
    return NULL;
}

__attribute__((weak)) void Gateway_Action_PulseMainDoor1(uint16_t pulse_ms) { (void)pulse_ms; }
__attribute__((weak)) void Gateway_Action_PulseMainDoor2(uint16_t pulse_ms) { (void)pulse_ms; }
__attribute__((weak)) void Gateway_Action_PulseOutputByOnOffIndex(uint8_t onoff_index_1based, uint16_t pulse_ms) {
    (void)onoff_index_1based; (void)pulse_ms;
}

bool H2Map_ApplyWrite(const H2_MapEntry_t* e, bool value, uint16_t pulse_ms) {
    if (!e) return false;
    if (e->rw != H2_RW_WRITE) return false;
    if (!value) return true;

    switch (e->action) {
    case H2_ACT_PULSE_MAIN_DOOR1:
        Gateway_Action_PulseMainDoor1(pulse_ms);
        return true;
    case H2_ACT_PULSE_MAIN_DOOR2:
        Gateway_Action_PulseMainDoor2(pulse_ms);
        return true;
    case H2_ACT_PULSE_OUTPUT:
        if (e->h2_dec >= 892 && e->h2_dec <= 896) {
            uint8_t onoff_index = (uint8_t)(e->h2_dec - 884);
            Gateway_Action_PulseOutputByOnOffIndex(onoff_index, pulse_ms);
            return true;
        }
        return false;
    default:
        return false;
    }
}
