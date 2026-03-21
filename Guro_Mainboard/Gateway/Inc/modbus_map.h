#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MB_MAP_DI = 0,   /* FC02 */
    MB_MAP_COIL,     /* FC01/05/15 */
    MB_MAP_IR,       /* FC04 */
    MB_MAP_HR        /* FC03/06/16 */
} mb_map_type_t;

typedef struct {
    uint16_t doc_addr;
    uint16_t modbus_offset;
    const char *name;
    uint8_t rw;
} mb_map_entry_t;

/* 문서 표기 주소 -> 실제 Modbus PDU 주소(0-based) 변환 */
uint16_t ModbusMap_Doc1xToOffset(uint16_t doc_addr);
uint16_t ModbusMap_Doc4xToOffset(uint16_t doc_addr);

/* 최소 조회 API (확장용) */
const mb_map_entry_t *ModbusMap_FindDiByOffset(uint16_t offset);
const mb_map_entry_t *ModbusMap_FindCoilByOffset(uint16_t offset);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MAP_H */
