/**
 * @file h2tech_address_map.c
 * @brief H2TECH translation table: absolute logical address -> internal (source, type, addr, bit).
 *        Ranges from H2TECH PDF: 1x0821~0836, 1x0853~0860, 1x0869~0880, 1x0892~0900.
 *        LSB-first bit packing. Explicit table-driven mapping per address.
 */
#include "h2tech_address_map.h"

/* ---------- Translation table ---------- */
#define H2TECH_MAP_MAX_ENTRIES  128

static h2tech_map_entry_t s_map[H2TECH_MAP_MAX_ENTRIES];
static uint16_t s_map_count = 0;

static void add_entry(uint16_t addr, h2tech_area_t area, uint8_t rw,
                     h2tech_source_t src, uint8_t slave_id,
                     h2tech_internal_type_t itype, uint16_t iaddr, uint8_t bit_idx)
{
    if (s_map_count >= H2TECH_MAP_MAX_ENTRIES) return;
    h2tech_map_entry_t *e = &s_map[s_map_count++];
    e->h2tech_addr    = addr;
    e->area           = area;
    e->rw             = rw;
    e->source         = src;
    e->slave_id       = slave_id;
    e->internal_type  = itype;
    e->internal_addr  = iaddr;
    e->bit_index      = bit_idx;
}

/* ---------- Build table: 1x discrete status ranges ---------- */
static void build_discrete_1x_0821_0836(void)
{
    /* SECTION 1: DIGITAL OUTPUT STATUS (READ ONLY)
     * 1x0821 ~ 1x0836 (16 bits): logical ON/OFF1..16, read-only.
     * Internal: MAIN discrete image, bytes 0..1, LSB-first. */
    for (uint16_t i = 0; i < 16; i++) {
        uint16_t addr = 821u + i;
        uint16_t byte_idx = i / 8u;
        uint8_t bit_idx  = (uint8_t)(i % 8u);
        add_entry(addr, H2TECH_AREA_DISCRETE, 0,           /* READ ONLY */
                  H2TECH_SOURCE_MAIN, 0,                  /* MAIN aggregated image */
                  H2TECH_INTERNAL_DISCRETE, byte_idx, bit_idx);
    }
}

static void build_discrete_1x_0853_0860(void)
{
    /* SECTION 2: DOOR OPEN SENSORS (READ ONLY)
     * 1x0853 ~ 1x0860 (8 bits): 자동문 센서/외부 스위치 1..4.
     * Internal: MAIN discrete image, byte 2, bits 0..7. */
    for (uint16_t i = 0; i < 8; i++) {
        add_entry(853u + i, H2TECH_AREA_DISCRETE, 0,
                  H2TECH_SOURCE_MAIN, 0,
                  H2TECH_INTERNAL_DISCRETE, 2, (uint8_t)i);
    }
}

static void build_discrete_1x_0869_0880(void)
{
    /* SECTION 3: ALARM STATUS (READ ONLY)
     * 1x0869 ~ 1x0880 (12 bits): Alarm1..12.
     * Internal: MAIN discrete image, bytes 3..4.
     * 869..876 -> byte 3 bits0..7, 877..880 -> byte 4 bits0..3. */
    for (uint16_t i = 0; i < 12; i++) {
        uint16_t addr = 869u + i;
        uint16_t byte_idx = i / 8u;
        uint8_t bit_idx  = (uint8_t)(i % 8u);
        add_entry(addr, H2TECH_AREA_DISCRETE, 0,
                  H2TECH_SOURCE_MAIN, 0,
                  H2TECH_INTERNAL_DISCRETE, (uint16_t)(3u + byte_idx), bit_idx);
    }
}

/* ---------- Build table: virtual button / control (write) ---------- */
static void build_control_1x_0892_0900(void)
{
    /* SECTION 4: VIRTUAL BUTTON / CONTROL (WRITE)
     * 1x0892 ~ 1x0900 (9 bits): ON/OFF8..12 + 자동문 문열림 제어1..4.
     * H2TECH 문서에서는 1x 영역으로 표기되지만, 쓰기 기능(FC05/15)을 위해
     * 게이트웨이 내부에서는 COIL 영역으로 취급한다.
     * Internal: MAIN coil image, bytes 0..1.
     * 892..899 -> coil byte0 bits0..7, 900 -> coil byte1 bit0. */
    for (uint16_t i = 0; i < 9; i++) {
        uint16_t addr = 892u + i;
        uint16_t byte_idx = i / 8u;
        uint8_t bit_idx  = (uint8_t)(i % 8u);
        add_entry(addr, H2TECH_AREA_COIL, 1,              /* WRITE-ABLE logical control */
                  H2TECH_SOURCE_MAIN, 0,
                  H2TECH_INTERNAL_COIL, (uint16_t)byte_idx, bit_idx);
    }
}

static void build_table(void)
{
    s_map_count = 0;
    build_discrete_1x_0821_0836();
    build_discrete_1x_0853_0860();
    build_discrete_1x_0869_0880();
    build_control_1x_0892_0900();
    /* Add 0x/3x/4x register ranges here when defined in H2TECH PDF. */
}

/* ---------- Public API ---------- */
const h2tech_map_entry_t *H2TechMap_Lookup(h2tech_area_t area, uint16_t addr)
{
    if (s_map_count == 0) build_table();

    for (uint16_t i = 0; i < s_map_count; i++) {
        if (s_map[i].area == area && s_map[i].h2tech_addr == addr)
            return &s_map[i];
    }
    return NULL;
}

int H2TechMap_IsRangeDefined(h2tech_area_t area, uint16_t addr, uint16_t count)
{
    for (uint16_t k = 0; k < count; k++) {
        if (H2TechMap_Lookup(area, addr + k) == NULL)
            return 0;
    }
    return 1;
}

uint16_t H2TechMap_EntryCount(void)
{
    if (s_map_count == 0) build_table();
    return s_map_count;
}
