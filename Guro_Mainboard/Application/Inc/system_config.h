/**
 * @file system_config.h
 * @brief EEPROM-backed system configuration (A/B dual block): magic, version, slave_id, baudrate.
 *        Load at boot; validate; on failure set defaults and save. Sequence counter for recovery.
 */
#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_CONFIG_MAGIC    0x4853u  /* "HS" */
#define SYSTEM_CONFIG_VERSION  1u
#define SYSTEM_CONFIG_DEFAULT_SLAVE_ID  9u
#define SYSTEM_CONFIG_DEFAULT_BAUDRATE  9600u

/* slave_id valid range (Modbus) */
#define SYSTEM_CONFIG_SLAVE_ID_MIN  1u
#define SYSTEM_CONFIG_SLAVE_ID_MAX  247u

/* EEPROM A/B dual block: 0x00~0x1F = CONFIG_A, 0x20~0x3F = CONFIG_B */
#define SYSTEM_CONFIG_BLOCK_A_BASE  0x00u
#define SYSTEM_CONFIG_BLOCK_B_BASE  0x20u
#define SYSTEM_CONFIG_BLOCK_SIZE    32u   /* bytes per block */
#define SYSTEM_CONFIG_PAYLOAD_BYTES 16u   /* bytes covered by CRC: magic..reserved */
#define SYSTEM_CONFIG_TOTAL_BYTES   18u   /* config without sequence: + crc */
#define SYSTEM_CONFIG_STORED_BYTES  20u   /* sequence(2) + config(18) per block */

/* Allowed baudrates (use SystemConfig_IsBaudrateAllowed). */
#define SYSTEM_CONFIG_BAUDRATE_9600    9600u
#define SYSTEM_CONFIG_BAUDRATE_19200  19200u
#define SYSTEM_CONFIG_BAUDRATE_38400  38400u
#define SYSTEM_CONFIG_BAUDRATE_57600  57600u
#define SYSTEM_CONFIG_BAUDRATE_115200 115200u

typedef struct __attribute__((packed)) {
	uint16_t magic;
	uint8_t  version;
	uint8_t  slave_id;
	uint32_t baudrate;
	uint8_t  reserved[8];
	uint16_t crc;
} system_config_t;

/** Set default values (magic, version=1, slave_id=1, baudrate=9600). */
void SystemConfig_SetDefaults(system_config_t *cfg);

/**
 * Load config from EEPROM (A/B both read; use valid block with higher sequence).
 * @return 0 on success, non-zero on failure (then caller should SetDefaults and Save).
 */
int SystemConfig_Load(system_config_t *cfg);

/**
 * Save cfg to EEPROM (writes to inactive block first, then switches). Skips write if unchanged.
 * @return 0 on success, non-zero on write failure.
 */
int SystemConfig_Save(const system_config_t *cfg);

/* Last SystemConfig_Save status code (for diagnostics; no UART logging). */
#define SYSCFG_SAVE_STATUS_OK                     0u
#define SYSCFG_SAVE_STATUS_ERR_NULL_CFG           1u
#define SYSCFG_SAVE_STATUS_ERR_VALIDATE           2u
#define SYSCFG_SAVE_STATUS_ERR_EEPROM_WRITE       3u
#define SYSCFG_SAVE_STATUS_ERR_EEPROM_READ        4u
#define SYSCFG_SAVE_STATUS_ERR_READBACK_VALIDATE  5u
#define SYSCFG_SAVE_STATUS_ERR_SEQ_MISMATCH       6u
uint16_t SystemConfig_GetLastSaveStatus(void);

/**
 * Validate magic, version, CRC, slave_id (1~247), baudrate (allowed list only).
 * @return 0 if valid, non-zero if invalid.
 */
int SystemConfig_Validate(const system_config_t *cfg);

/** Return 1 if baudrate is in allowed list (9600,19200,38400,57600,115200), else 0. */
int SystemConfig_IsBaudrateAllowed(uint32_t baudrate);

/** Compute CRC over config (excluding crc field). */
uint16_t SystemConfig_CalcCrc(const system_config_t *cfg);

/** Return pointer to the in-memory config. NULL if not yet loaded. */
const system_config_t *SystemConfig_Get(void);

/**
 * Effective UART1/PC Modbus unit ID (EEPROM + validation).
 * If FORCE_LIT9: 9. If EEPROM invalid (0xFF or out of 1..247): default 9.
 */
uint8_t SystemConfig_GetEffectiveMainboardSlaveId(void);

/**
 * Runtime communication slave ID used by current session.
 * Policy: updated at boot/load time, not changed by SystemConfig_Save().
 */
uint8_t SystemConfig_GetRuntimeCommSlaveId(void);

/** Current sequence number (from loaded or last saved block). 0 if not loaded. */
uint16_t SystemConfig_GetSequence(void);

/** Active block: 0 = A (0x00), 1 = B (0x20). */
uint8_t SystemConfig_GetActiveBlock(void);

/** Factory reset: clear A/B blocks, save defaults to A, update cache. */
int SystemConfig_FactoryReset(void);

/** Log current config to UART (test/debug). Weak; override to use real UART. */
void SystemConfig_LogCurrent(const system_config_t *cfg);

/** Log current config + active block, sequence, valid to UART. huart NULL = no-op. */
void SystemConfig_LogToUart(void *huart);

/** Weak: override to log "CFG unchanged, skip save" (when SYSTEM_CONFIG_LOG_SKIP_SAVE=1). */
void SystemConfig_LogSkipSave(void);

/** Weak: override to log "CFG factory reset done" (when SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET=1). */
void SystemConfig_LogFactoryResetDone(void);

/** SYSCFG debug log (when SYSCFG_MODBUS_DEBUG_LOG=1). FC03 read 3000~3002 시 id, baud_code 출력. */
void UpstreamSlave_LogSyscfgRead(uint16_t id, uint16_t baud_code);

/** SYSCFG debug log. FC06 write 3000/3001/3002 시 reg, value, result(1=OK 0=ERR) 출력. */
void UpstreamSlave_LogSyscfgWrite(uint16_t reg, uint16_t value, int result_ok);

/* ---------- Modbus system config registers (4x3000~3002 구현 완료) ----------
 * Holding registers; write 동작은 추후 구현.
 * 4x3000 : slave_id (1~247)
 * 4x3001 : baudrate code (0=9600, 1=19200, 2=38400, 3=57600, 4=115200)
 * 4x3002 : factory reset command (write 1 = execute factory reset)
 */
#define SYSCFG_MODBUS_SLAVE_ID_REG      3000u
#define SYSCFG_MODBUS_BAUDRATE_CODE_REG 3001u
#define SYSCFG_MODBUS_FACTORY_RESET_REG 3002u

/* PC UART1 config path: FC06 pending + FC05 coil7 save trigger */
#define MB_MAIN_SLAVE_PENDING_REG_PDU   2103u
#define MB_MAIN_BAUD_RATE_PENDING_REG_PDU 2104u
#define MB_MAIN_SYSTEM_MODE_REG_PDU     2105u
#define MB_MAIN_LOG_ENABLE_REG_PDU      2106u
#define MB_MAIN_SLAVE_SAVE_COIL_PDU     7u

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CONFIG_H */
