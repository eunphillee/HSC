/**
 * @file aggregated_status.c
 */
#include "aggregated_status.h"
#include <string.h>

void AggregatedStatus_Clear(aggregated_status_t *s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->env_temp_cx10 = -32768;
	s->env_rh_x10    = 0xFFFF;
}

void AggregatedStatus_UpdateTimestamp(aggregated_status_t *s, agg_tick_t now_ms)
{
	if (s) s->timestamp_ms = now_ms;
}
