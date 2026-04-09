#include "io_action.h"
#include "gateway_actions.h"

int IoAction_WriteSubCoil(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    Gateway_WriteSubCoil_SetNextReason("IO_ACTION");
    return Gateway_Action_WriteSubCoil(slave_id, coil_index, value);
}

void IoAction_PulseDoor(uint8_t door_index, uint16_t pulse_ms)
{
    if (door_index == 1u) Gateway_Action_PulseMainDoor1(pulse_ms);
    else if (door_index == 2u) Gateway_Action_PulseMainDoor2(pulse_ms);
}
