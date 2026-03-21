#ifndef SYSTEM_SYNC_H
#define SYSTEM_SYNC_H

#include <stdint.h>
#include "aggregated_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 하위 보드(HPSB/LPSB) 상태 취합 동기화 래퍼 */
void SystemSync_Init(void);
void SystemSync_Update(aggregated_status_t *agg, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_SYNC_H */
