#ifndef MODBUS_CRC_H
#define MODBUS_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus RTU CRC16 (poly 0xA001, init 0xFFFF) */
uint16_t ModbusCRC16_Calc(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CRC_H */
