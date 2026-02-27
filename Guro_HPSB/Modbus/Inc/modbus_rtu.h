/**
 * @file modbus_rtu.h
 * @brief Modbus RTU frame layer: CRC, response build, request parse. LSB-first, 8 bits/byte.
 */
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t ModbusRTU_CRC16(const uint8_t *data, size_t len);
int      ModbusRTU_CRC16Check(const uint8_t *frame, size_t len);
void     ModbusRTU_AppendCRC(uint8_t *data, size_t len);

void ModbusRTU_PackCoilsLSB(const uint8_t *coil_bits, uint16_t num_bits, uint8_t *bytes);
void ModbusRTU_UnpackCoilsLSB(const uint8_t *bytes, uint16_t num_bits, uint8_t *coil_bits);

/* Slave: response builders (PDU without CRC). Return PDU length. */
size_t ModbusRTU_BuildFC01Response(uint8_t *pdu, uint8_t slave_addr, const uint8_t *coil_bytes, uint16_t num_coils);
size_t ModbusRTU_BuildFC02Response(uint8_t *pdu, uint8_t slave_addr, const uint8_t *discrete_bytes, uint16_t num_bits);
size_t ModbusRTU_BuildFC03Response(uint8_t *pdu, uint8_t slave_addr, const uint16_t *regs, uint16_t num_regs);
size_t ModbusRTU_BuildFC04Response(uint8_t *pdu, uint8_t slave_addr, const uint16_t *regs, uint16_t num_regs);
size_t ModbusRTU_BuildFC05Response(uint8_t *pdu, uint8_t slave_addr, uint16_t coil_addr, uint8_t value);
size_t ModbusRTU_BuildFC06Response(uint8_t *pdu, uint8_t slave_addr, uint16_t reg_addr, uint16_t value);
size_t ModbusRTU_BuildFC15Response(uint8_t *pdu, uint8_t slave_addr, uint16_t start_addr, uint16_t num_coils);
size_t ModbusRTU_BuildFC16Response(uint8_t *pdu, uint8_t slave_addr, uint16_t start_addr, uint16_t num_regs);

/* Slave: request parsers. Return 0 on success. */
int ModbusRTU_ParseFC05Request(const uint8_t *frame, size_t len, uint16_t *coil_addr, uint8_t *value);
int ModbusRTU_ParseFC06Request(const uint8_t *frame, size_t len, uint16_t *reg_addr, uint16_t *value);
int ModbusRTU_ParseFC15Request(const uint8_t *frame, size_t len, uint16_t *start_addr, uint16_t *num_coils, uint8_t *coil_bytes, size_t coil_byte_buf_size);
int ModbusRTU_ParseFC16Request(const uint8_t *frame, size_t len, uint16_t *start_addr, uint16_t *num_regs, uint16_t *regs, size_t reg_buf_count);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */
