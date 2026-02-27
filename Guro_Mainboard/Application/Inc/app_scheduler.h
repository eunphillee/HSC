/**
 * @file app_scheduler.h
 * @brief Minimal 1ms-base task scheduler (no blocking). Use with SysTick or HAL_GetTick().
 */
#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	TASK_UPSTREAM_POLL = 0,
	TASK_DOWNSTREAM_MODBUS,
	TASK_AGGREGATE_UPDATE,
	TASK_UPSTREAM_SEND_STATUS,
	TASK_COUNT
} app_task_id_t;

void AppScheduler_Init(void);
void AppScheduler_Update(void);
uint8_t AppScheduler_IsDue(app_task_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */
