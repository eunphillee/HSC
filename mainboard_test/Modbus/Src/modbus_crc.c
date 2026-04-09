#include "modbus_crc.h"

uint16_t ModbusCRC16_Calc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0; b < 8u; b++) {
            if (crc & 0x0001u) crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else crc >>= 1;
        }
    }
    return crc;
}
