/**
 * @file h2tech_address_map.h
 * @brief H2TECH address translation: logical address -> internal (source, type, addr, bit).
 *        Authority: H2TECH 스마트 쉘터 매핑 PDF.
 *        Do NOT use BASE subtraction; use explicit translation table.
 */
#ifndef H2TECH_ADDRESS_MAP_H
#define H2TECH_ADDRESS_MAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Modbus area (0x / 1x / 3x / 4x) ---------- */
typedef enum {
    H2TECH_AREA_COIL          = 0,  /* 0x - FC01/05/15 */
    H2TECH_AREA_DISCRETE      = 1,  /* 1x - FC02 */
    H2TECH_AREA_INPUT_REG     = 2,  /* 3x - FC04 */
    H2TECH_AREA_HOLDING       = 3   /* 4x - FC03/06/16 */
} h2tech_area_t;

/* ---------- Data source ---------- */
typedef enum {
    H2TECH_SOURCE_MAIN  = 0,
    H2TECH_SOURCE_HPSB  = 1,
    H2TECH_SOURCE_LPSB  = 2
} h2tech_source_t;

/* ---------- Internal data type (for aggregated image index) ---------- */
typedef enum {
    H2TECH_INTERNAL_COIL       = 0,
    H2TECH_INTERNAL_DISCRETE   = 1,
    H2TECH_INTERNAL_HOLDING    = 2,
    H2TECH_INTERNAL_INPUT_REG  = 3
} h2tech_internal_type_t;

/* ---------- Single translation entry ---------- */
typedef struct {
    uint16_t h2tech_addr;           /* Absolute logical address (e.g. 821, 869) */
    h2tech_area_t area;             /* 0x / 1x / 3x / 4x */
    uint8_t rw;                     /* 0 = read-only, 1 = read-write */
    h2tech_source_t source;         /* MAIN / HPSB / LPSB */
    uint8_t slave_id;              /* 0=main, 1=HPSB, 2=LPSB */
    h2tech_internal_type_t internal_type;
    uint16_t internal_addr;         /* 0-based internal address (byte for coil/discrete, reg for 3x/4x) */
    uint8_t bit_index;             /* 0..7 for bit in byte; 0xFF = whole register/byte */
} h2tech_map_entry_t;

/* ---------- Modbus exception codes (H2TECH protocol) ---------- */
#define H2TECH_EX_ILLEGAL_FUNCTION      0x01
#define H2TECH_EX_ILLEGAL_DATA_ADDRESS  0x02
#define H2TECH_EX_ILLEGAL_DATA_VALUE    0x03

/* ---------- Lookup API ---------- */
/**
 * Look up translation entry by H2TECH area and absolute address.
 * @param area   Modbus area (coil/discrete/input_reg/holding).
 * @param addr   H2TECH logical address (e.g. 821, 869).
 * @return Pointer to static entry, or NULL if not in table (-> return exception 0x02).
 */
const h2tech_map_entry_t *H2TechMap_Lookup(h2tech_area_t area, uint16_t addr);

/**
 * Check whether a contiguous range [addr, addr+count) is fully defined in the table.
 * @return 1 if all addresses present, 0 if any missing.
 */
int H2TechMap_IsRangeDefined(h2tech_area_t area, uint16_t addr, uint16_t count);

/**
 * Get total number of entries (for iteration / validation).
 */
uint16_t H2TechMap_EntryCount(void);

#ifdef __cplusplus
}
#endif

#endif /* H2TECH_ADDRESS_MAP_H */
