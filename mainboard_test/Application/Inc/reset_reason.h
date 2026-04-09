/**
 * @file reset_reason.h
 * @brief Capture MCU reset reason flags (RCC->CSR) early after boot.
 */
#ifndef RESET_REASON_H
#define RESET_REASON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void     ResetReason_CaptureAndClear(void);
uint32_t ResetReason_GetRccCsr(void);   /* raw RCC->CSR snapshot */

#ifdef __cplusplus
}
#endif

#endif /* RESET_REASON_H */

