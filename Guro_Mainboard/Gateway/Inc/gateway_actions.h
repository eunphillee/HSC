/**
 * @file gateway_actions.h
 * @brief Non-blocking gateway actions: door pulse (300ms), output toggle.
 *        Call Gateway_Action_Update() from main loop (e.g. every 1ms).
 */
#ifndef GATEWAY_ACTIONS_H
#define GATEWAY_ACTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Gateway_Action_Update(void);

/** Returns 1 if any downstream WriteCoil failed since last clear; does not clear. Clear via ClearDownstreamWriteFailAlarm (e.g. on PC read of 1x0880). */
uint8_t Gateway_Action_PollDownstreamWriteFail(void);
/** Clear the downstream write-fail alarm (e.g. after PC read of 1x0880 or auto after N seconds). */
void Gateway_Action_ClearDownstreamWriteFailAlarm(void);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_ACTIONS_H */
