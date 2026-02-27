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

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_ACTIONS_H */
