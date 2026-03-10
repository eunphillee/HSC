/**
 * @file wwdg_service.h
 * @brief WWDG refresh service with timing guard (prevents early refresh reset).
 *
 * Design:
 * - WWDG must be kept enabled (vendor requirement).
 * - Refresh only when main loop is alive (call from main loop).
 * - Enforce min/max refresh window based on PCLK1 and WWDG init values.
 */
#ifndef WWDG_SERVICE_H
#define WWDG_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f2xx_hal.h"

void WwdgService_Init(WWDG_HandleTypeDef *hwwdg);
void WwdgService_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* WWDG_SERVICE_H */

