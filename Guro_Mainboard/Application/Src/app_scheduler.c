/**
 * @file app_scheduler.c
 * @brief 1ms-base task flags. Call AppScheduler_Update() from main loop (uses HAL_GetTick()).
 */
#include "app_scheduler.h"
#include "main.h"

static uint32_t tick_count;
static uint32_t next_run[TASK_COUNT];
static uint8_t  task_due[TASK_COUNT];

static const uint32_t period_ms[TASK_COUNT] = {
	10,   /* UPSTREAM_POLL */
	10,   /* DOWNSTREAM_MODBUS */
	100,  /* AGGREGATE_UPDATE */
	500   /* UPSTREAM_SEND_STATUS */
};

void AppScheduler_Init(void)
{
	tick_count = HAL_GetTick();
	for (int i = 0; i < TASK_COUNT; i++) {
		next_run[i] = tick_count;
		task_due[i] = 0;
	}
}

void AppScheduler_Update(void)
{
	uint32_t now = HAL_GetTick();
	if (now < tick_count) return;
	tick_count = now;

	for (int i = 0; i < TASK_COUNT; i++) {
		if (now >= next_run[i]) {
			task_due[i] = 1;
			next_run[i] = now + period_ms[i];
		}
	}
}

uint8_t AppScheduler_IsDue(app_task_id_t id)
{
	if (id >= TASK_COUNT) return 0;
	if (!task_due[id]) return 0;
	task_due[id] = 0;
	return 1;
}
