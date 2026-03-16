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

/** Start 100ms HIGH pulse on PC_ON_EN (PC1). Call Gateway_Action_Update() from main loop to clear. */
void Gateway_Action_StartPulsePC_ON_EN(void);
/** Start 100ms HIGH pulse on PC_RESET_EN (PC0). Call Gateway_Action_Update() from main loop to clear. */
void Gateway_Action_StartPulsePC_RESET_EN(void);

/** Write sub-board coil (HPSB=1, LPSB1=2, LPSB2=4, LPSB3=8). Called when PC sends FC05 to Mainboard. */
int Gateway_Action_WriteSubCoil(uint8_t slave_id, uint16_t coil_index, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_ACTIONS_H */
