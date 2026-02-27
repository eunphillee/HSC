/**
 * @file aggregated_status.h
 * @brief Data model for aggregated status: env, main DI/DO, sub-board relay + sensing, timestamps, error flags.
 */
#ifndef AGGREGATED_STATUS_H
#define AGGREGATED_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t agg_tick_t;

#define AGG_ERR_COMM_HPSB    (1u << 0)
#define AGG_ERR_COMM_LPSB    (1u << 1)
#define AGG_ERR_SHTC3       (1u << 2)
#define AGG_ERR_UPSTREAM_RX (1u << 3)
#define AGG_FLAG_FAULT      (1u << 4)

typedef struct {
	agg_tick_t timestamp_ms;
	int16_t   env_temp_cx10;
	uint16_t  env_rh_x10;
	uint8_t   main_di;
	uint8_t   main_do;
	uint8_t   hpsb_coils;
	uint8_t   hpsb_discrete;
	uint16_t  hpsb_status_reg;
	uint16_t  hpsb_alarm_reg;
	uint8_t   lpsb_coils;
	uint8_t   lpsb_discrete;
	uint16_t  lpsb_status_reg;
	uint16_t  lpsb_alarm_reg;
	uint16_t  hpsb_sense_raw;
	uint16_t  lpsb_sense_raw;
	uint16_t  error_flags;
} aggregated_status_t;

void AggregatedStatus_Clear(aggregated_status_t *s);
void AggregatedStatus_UpdateTimestamp(aggregated_status_t *s, agg_tick_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* AGGREGATED_STATUS_H */
