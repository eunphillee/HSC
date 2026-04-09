/**
 * @file wwdg_service.c
 * @brief WWDG refresh with guard window.
 *
 * Reference (HAL driver note):
 * WWDG clock (Hz) = PCLK1 / (4096 * PrescalerDiv)
 * min time (ms)   = 1000 * (Counter - Window) / WWDG_clock
 * max time (ms)   = 1000 * (Counter - 0x40) / WWDG_clock
 */
#include "wwdg_service.h"

static WWDG_HandleTypeDef *g_hwwdg;
static uint32_t g_last_refresh_tick;
static uint32_t g_min_ms;
static uint32_t g_max_ms;

static uint32_t prescaler_div(uint32_t p)
{
	switch (p)
	{
		case WWDG_PRESCALER_1: return 1u;
		case WWDG_PRESCALER_2: return 2u;
		case WWDG_PRESCALER_4: return 4u;
		case WWDG_PRESCALER_8: return 8u;
		default: return 1u;
	}
}

void WwdgService_Init(WWDG_HandleTypeDef *hwwdg)
{
	g_hwwdg = hwwdg;
	g_last_refresh_tick = HAL_GetTick();

	uint32_t pclk1 = HAL_RCC_GetPCLK1Freq(); /* expected 42MHz */
	uint32_t div = prescaler_div(hwwdg->Init.Prescaler);
	uint32_t wwdg_clk = (pclk1 / 4096u) / div;
	if (wwdg_clk == 0) wwdg_clk = 1;

	/* Derive min/max refresh time in ms from current init values */
	int32_t counter = (int32_t)(hwwdg->Init.Counter & 0x7Fu);
	int32_t window  = (int32_t)(hwwdg->Init.Window & 0x7Fu);
	if (window > counter) window = counter;

	int32_t min_ticks = counter - window;
	int32_t max_ticks = counter - 0x40;
	if (min_ticks < 0) min_ticks = 0;
	if (max_ticks < 0) max_ticks = 0;

	g_min_ms = (uint32_t)((1000u * (uint32_t)min_ticks) / wwdg_clk);
	g_max_ms = (uint32_t)((1000u * (uint32_t)max_ticks) / wwdg_clk);

	/* Safety margin: avoid refreshing at exact boundary */
	if (g_max_ms > 2u) g_max_ms -= 1u;

	/* For current config (Prescaler=8, Counter=127, Window=120, PCLK1=42MHz):
	 * - WWDG clock ≈ 1281.7 Hz  (42MHz / (4096 * 8))
	 * - min_refresh_ms ≈ 5.5 ms ((127-120)/f)
	 * - max_refresh_ms ≈ 49  ms ((127-64)/f)
	 * -> Refresh must occur only within [g_min_ms, g_max_ms]. */
}

void WwdgService_Process(void)
{
	if (g_hwwdg == NULL)
	{
		return;
	}

	uint32_t now = HAL_GetTick();
	uint32_t elapsed = now - g_last_refresh_tick;

	/* Too early -> do nothing (prevents early refresh reset). */
	if (elapsed < g_min_ms) return;

	/* Too late -> do nothing; WWDG will reset (desired if loop is stuck). */
	if (elapsed > g_max_ms) return;

	(void)HAL_WWDG_Refresh(g_hwwdg);
	g_last_refresh_tick = now;
}

