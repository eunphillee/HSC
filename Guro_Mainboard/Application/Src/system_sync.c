#include "system_sync.h"
#include "aggregator.h"

void SystemSync_Init(void)
{
    /* 현재는 기존 Aggregator_Update 경로를 그대로 사용 */
}

void SystemSync_Update(aggregated_status_t *agg, uint32_t now_ms)
{
    (void)now_ms;
    if (!agg) return;
    Aggregator_Update(agg);
}
