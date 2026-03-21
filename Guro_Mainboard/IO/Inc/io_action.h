#ifndef IO_ACTION_H
#define IO_ACTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Coil 쓰기 액션 래퍼: 향후 인터록/펄스정책 확장용 */
int IoAction_WriteSubCoil(uint8_t slave_id, uint16_t coil_index, uint8_t value);
void IoAction_PulseDoor(uint8_t door_index, uint16_t pulse_ms);

#ifdef __cplusplus
}
#endif

#endif /* IO_ACTION_H */
