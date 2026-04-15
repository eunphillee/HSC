#include "modbus_map.h"
#include "h2tech_address_map.h"

#define MB_RW_RO 0u
#define MB_RW_RW 1u

/* h2tech_address_map.c 의 DOC1X_DI_BASE / DOC1X_COIL_BASE 와 동일해야 함.
 * C 정적 초기화에는 함수 호출(doc1x_to_offset) 불가 → 상수식만 사용. */
#define MB_DOC1X_DI_BASE    821u
#define MB_DOC1X_COIL_BASE  892u
#define MB_OFF_DI(d)        ((uint16_t)((d) - MB_DOC1X_DI_BASE))
#define MB_OFF_COIL(d)      ((uint16_t)((d) - MB_DOC1X_COIL_BASE))

/* 1차 우선 범위: 자동문 입력/출력 주소군 */
static const mb_map_entry_t s_di_map[] = {
    {821,  MB_OFF_DI(821),  "자동문 ONOFF 상태1", MB_RW_RO},
    {822,  MB_OFF_DI(822),  "자동문 ONOFF 상태2", MB_RW_RO},
    {853,  MB_OFF_DI(853),  "자동문 열림 센서1", MB_RW_RO},
    {854,  MB_OFF_DI(854),  "자동문 열림 센서2", MB_RW_RO},
    {857,  MB_OFF_DI(857),  "외부 문열림 스위치1", MB_RW_RO},
    {858,  MB_OFF_DI(858),  "외부 문열림 스위치2", MB_RW_RO},
    {869,  MB_OFF_DI(869),  "정상/이상 상태1", MB_RW_RO},
    {870,  MB_OFF_DI(870),  "정상/이상 상태2", MB_RW_RO},
};

/* PC FC05 하위 릴레이만 사용 (1x0899~0910 / Modbus coil 898~909). VB·문열림 892~898 미사용. */
static const mb_map_entry_t s_coil_map[] = {
    {899, MB_OFF_COIL(899), "HPSB SSR#1 쓰기", MB_RW_RW},
    {900, MB_OFF_COIL(900), "HPSB SSR#2 쓰기", MB_RW_RW},
    {901, MB_OFF_COIL(901), "HPSB SSR#3 쓰기", MB_RW_RW},
    {902, MB_OFF_COIL(902), "LPSB1 SSR#1 쓰기", MB_RW_RW},
    {903, MB_OFF_COIL(903), "LPSB1 SSR#2 쓰기", MB_RW_RW},
    {904, MB_OFF_COIL(904), "LPSB1 SSR#3 쓰기", MB_RW_RW},
    {905, MB_OFF_COIL(905), "LPSB2 SSR#1 쓰기", MB_RW_RW},
    {906, MB_OFF_COIL(906), "LPSB2 SSR#2 쓰기", MB_RW_RW},
    {907, MB_OFF_COIL(907), "LPSB2 SSR#3 쓰기", MB_RW_RW},
    {908, MB_OFF_COIL(908), "LPSB3 SSR#1 쓰기", MB_RW_RW},
    {909, MB_OFF_COIL(909), "LPSB3 SSR#2 쓰기", MB_RW_RW},
    {910, MB_OFF_COIL(910), "LPSB3 SSR#3 쓰기", MB_RW_RW},
};

uint16_t ModbusMap_Doc1xToOffset(uint16_t doc_addr)
{
    return doc1x_to_offset(doc_addr);
}

uint16_t ModbusMap_Doc4xToOffset(uint16_t doc_addr)
{
    return doc4x_to_offset(doc_addr);
}

const mb_map_entry_t *ModbusMap_FindDiByOffset(uint16_t offset)
{
    for (unsigned i = 0; i < (sizeof(s_di_map) / sizeof(s_di_map[0])); i++) {
        if (s_di_map[i].modbus_offset == offset) return &s_di_map[i];
    }
    return 0;
}

const mb_map_entry_t *ModbusMap_FindCoilByOffset(uint16_t offset)
{
    for (unsigned i = 0; i < (sizeof(s_coil_map) / sizeof(s_coil_map[0])); i++) {
        if (s_coil_map[i].modbus_offset == offset) return &s_coil_map[i];
    }
    return 0;
}
