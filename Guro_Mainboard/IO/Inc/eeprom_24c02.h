/**
 * @file eeprom_24c02.h
 * @brief IO layer for AT24C02C EEPROM (I2C3): page-aligned write, write-cycle wait/ACK polling.
 */
#ifndef EEPROM_24C02_H
#define EEPROM_24C02_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Read len bytes from EEPROM at addr into buf. */
int EEPROM_Read(uint16_t addr, uint8_t *buf, uint16_t len);

/** Write len bytes from buf to EEPROM at addr. Handles page boundary and write-cycle wait. */
int EEPROM_Write(uint16_t addr, const uint8_t *buf, uint16_t len);

/**
 * Self-test: write/read/compare patterns 0x55, 0xAA, 0x00, 0xFF in test region, then clear.
 * @return 0 on success, -1 on failure (생산/현장 점검용).
 */
int EEPROM_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_24C02_H */
