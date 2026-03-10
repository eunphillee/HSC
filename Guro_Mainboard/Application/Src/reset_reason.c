/**
 * @file reset_reason.c
 * @brief Reset reason capture. Reads RCC->CSR flags and clears them.
 */
#include "reset_reason.h"
#include "stm32f2xx_hal.h"

static uint32_t g_rcc_csr;

void ResetReason_CaptureAndClear(void)
{
	/* RCC->CSR reset flags persist across reset until cleared by RMVF. */
	g_rcc_csr = RCC->CSR;
	__HAL_RCC_CLEAR_RESET_FLAGS();
}

uint32_t ResetReason_GetRccCsr(void)
{
	return g_rcc_csr;
}

