/**
 * @file aggregator.h
 * @brief Fills aggregated_status from Modbus table, local IO, and env (SHTC3). Call periodically.
 */
#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include "aggregated_status.h"

#ifdef __cplusplus
extern "C" {
#endif

void Aggregator_Update(aggregated_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AGGREGATOR_H */
