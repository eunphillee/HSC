/**
 * @file system_config.c
 * @brief EEPROM A/B dual-block config: load/save/validate, sequence, skip save when unchanged.
 */
#include "system_config.h"
#include "eeprom_24c02.h"
#include "modbus_rtu.h"
#include "main.h"
#include "app_config.h"
#include <string.h>
#include <stdio.h>

#ifndef SYSTEM_CONFIG_LOG_SKIP_SAVE
#define SYSTEM_CONFIG_LOG_SKIP_SAVE  0
#endif
#ifndef SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET
#define SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET  0
#endif

static system_config_t g_system_config;
static uint16_t g_sequence;
static uint8_t g_active_block;  /* 0 = A, 1 = B */
static int g_loaded;
static uint8_t g_runtime_comm_slave_id = SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
static uint16_t g_last_save_status = SYSCFG_SAVE_STATUS_OK;

static uint8_t normalize_slave_id(uint8_t id)
{
	if (id == 0xFFu) return SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	if (id < SYSTEM_CONFIG_SLAVE_ID_MIN || id > SYSTEM_CONFIG_SLAVE_ID_MAX)
		return SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	return id;
}

static int config_compare_payload(const system_config_t *a, const system_config_t *b)
{
	if (a->magic != b->magic) return 1;
	if (a->version != b->version) return 1;
	if (a->slave_id != b->slave_id) return 1;
	if (a->baudrate != b->baudrate) return 1;
	return memcmp(a->reserved, b->reserved, sizeof(a->reserved));
}

void SystemConfig_SetDefaults(system_config_t *cfg)
{
	if (!cfg) return;
	cfg->magic    = SYSTEM_CONFIG_MAGIC;
	cfg->version  = SYSTEM_CONFIG_VERSION;
	cfg->slave_id = SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	cfg->baudrate = SYSTEM_CONFIG_DEFAULT_BAUDRATE;
	memset(cfg->reserved, 0, sizeof(cfg->reserved));
	cfg->crc = 0;
	cfg->crc = SystemConfig_CalcCrc(cfg);
}

uint16_t SystemConfig_CalcCrc(const system_config_t *cfg)
{
	if (!cfg) return 0;
	return ModbusRTU_CRC16((const uint8_t *)cfg, SYSTEM_CONFIG_PAYLOAD_BYTES);
}

int SystemConfig_IsBaudrateAllowed(uint32_t baudrate)
{
	switch (baudrate) {
	case SYSTEM_CONFIG_BAUDRATE_9600:
	case SYSTEM_CONFIG_BAUDRATE_19200:
	case SYSTEM_CONFIG_BAUDRATE_38400:
	case SYSTEM_CONFIG_BAUDRATE_57600:
	case SYSTEM_CONFIG_BAUDRATE_115200:
		return 1;
	default:
		return 0;
	}
}

int SystemConfig_Validate(const system_config_t *cfg)
{
	if (!cfg) return -1;
	if (cfg->magic != SYSTEM_CONFIG_MAGIC) return -1;
	if (cfg->version != SYSTEM_CONFIG_VERSION) return -1;
	if (cfg->slave_id < SYSTEM_CONFIG_SLAVE_ID_MIN || cfg->slave_id > SYSTEM_CONFIG_SLAVE_ID_MAX)
		return -1;
	if (!SystemConfig_IsBaudrateAllowed(cfg->baudrate)) return -1;
	uint16_t computed = SystemConfig_CalcCrc(cfg);
	if (computed != cfg->crc) return -1;
	return 0;
}

/** Build 20-byte block: seq (LSB first) + config (18 bytes). */
static void build_stored_block(uint8_t *out, uint16_t seq, const system_config_t *cfg)
{
	system_config_t copy = *cfg;
	copy.crc = SystemConfig_CalcCrc(&copy);
	out[0] = (uint8_t)(seq & 0xFF);
	out[1] = (uint8_t)(seq >> 8);
	memcpy(out + 2, &copy, SYSTEM_CONFIG_TOTAL_BYTES);
}

/** Parse 20-byte block into seq and config. Returns 0 on parse ok. */
static int parse_stored_block(const uint8_t *raw, uint16_t *seq, system_config_t *cfg)
{
	if (!raw || !seq || !cfg) return -1;
	*seq = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
	memcpy(cfg, raw + 2, SYSTEM_CONFIG_TOTAL_BYTES);
	return 0;
}

static uint16_t get_block_base(uint8_t block_index)
{
	return block_index == 0 ? SYSTEM_CONFIG_BLOCK_A_BASE : SYSTEM_CONFIG_BLOCK_B_BASE;
}

int SystemConfig_Load(system_config_t *cfg)
{
	if (!cfg) return -1;

	uint8_t raw_a[SYSTEM_CONFIG_STORED_BYTES];
	uint8_t raw_b[SYSTEM_CONFIG_STORED_BYTES];
	if (EEPROM_Read(SYSTEM_CONFIG_BLOCK_A_BASE, raw_a, SYSTEM_CONFIG_STORED_BYTES) != 0)
		return -1;
	if (EEPROM_Read(SYSTEM_CONFIG_BLOCK_B_BASE, raw_b, SYSTEM_CONFIG_STORED_BYTES) != 0)
		return -1;

	uint16_t seq_a, seq_b;
	system_config_t cfg_a, cfg_b;
	parse_stored_block(raw_a, &seq_a, &cfg_a);
	parse_stored_block(raw_b, &seq_b, &cfg_b);

	int valid_a = (SystemConfig_Validate(&cfg_a) == 0);
	int valid_b = (SystemConfig_Validate(&cfg_b) == 0);

	if (valid_a && valid_b) {
		if (seq_a >= seq_b) {
			memcpy(cfg, &cfg_a, sizeof(system_config_t));
			g_sequence = seq_a;
			g_active_block = 0;
		} else {
			memcpy(cfg, &cfg_b, sizeof(system_config_t));
			g_sequence = seq_b;
			g_active_block = 1;
		}
	} else if (valid_a) {
		memcpy(cfg, &cfg_a, sizeof(system_config_t));
		g_sequence = seq_a;
		g_active_block = 0;
	} else if (valid_b) {
		memcpy(cfg, &cfg_b, sizeof(system_config_t));
		g_sequence = seq_b;
		g_active_block = 1;
	} else {
		SystemConfig_SetDefaults(cfg);
		g_sequence = 1;
		g_active_block = 0;
		memcpy(&g_system_config, cfg, sizeof(g_system_config));
		g_loaded = 1;
		g_runtime_comm_slave_id = normalize_slave_id(g_system_config.slave_id);
		/* Save defaults to block A */
		uint8_t buf[SYSTEM_CONFIG_STORED_BYTES];
		build_stored_block(buf, g_sequence, cfg);
		if (EEPROM_Write(SYSTEM_CONFIG_BLOCK_A_BASE, buf, SYSTEM_CONFIG_STORED_BYTES) != 0)
			return -1;
		return 0;
	}

	memcpy(&g_system_config, cfg, sizeof(g_system_config));
	g_loaded = 1;
	g_runtime_comm_slave_id = normalize_slave_id(g_system_config.slave_id);
	return 0;
}

int SystemConfig_Save(const system_config_t *cfg)
{
	g_last_save_status = SYSCFG_SAVE_STATUS_OK;
	if (!cfg) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_NULL_CFG;
		return -1;
	}
	if (SystemConfig_Validate(cfg) != 0) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_VALIDATE;
		return -1;
	}

	if (g_loaded && config_compare_payload(cfg, &g_system_config) == 0) {
#if SYSTEM_CONFIG_LOG_SKIP_SAVE
		SystemConfig_LogSkipSave();
#endif
		return 0;
	}

	uint8_t next_block = 1u - g_active_block;
	uint16_t next_seq = g_sequence + 1u;
	uint16_t base = get_block_base(next_block);

	uint8_t buf[SYSTEM_CONFIG_STORED_BYTES];
	build_stored_block(buf, next_seq, cfg);
	if (EEPROM_Write(base, buf, SYSTEM_CONFIG_STORED_BYTES) != 0) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_EEPROM_WRITE;
		return -1;
	}

	/* Read back and validate */
	uint8_t read_back[SYSTEM_CONFIG_STORED_BYTES];
	if (EEPROM_Read(base, read_back, SYSTEM_CONFIG_STORED_BYTES) != 0) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_EEPROM_READ;
		return -1;
	}
	uint16_t read_seq;
	system_config_t read_cfg;
	parse_stored_block(read_back, &read_seq, &read_cfg);
	if (SystemConfig_Validate(&read_cfg) != 0) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_READBACK_VALIDATE;
		return -1;
	}
	if (read_seq != next_seq) {
		g_last_save_status = SYSCFG_SAVE_STATUS_ERR_SEQ_MISMATCH;
		return -1;
	}

	g_active_block = next_block;
	g_sequence = next_seq;
	memcpy(&g_system_config, &read_cfg, sizeof(g_system_config));
	g_loaded = 1;
	g_last_save_status = SYSCFG_SAVE_STATUS_OK;
	return 0;
}

uint16_t SystemConfig_GetLastSaveStatus(void)
{
	return g_last_save_status;
}

const system_config_t *SystemConfig_Get(void)
{
	return g_loaded ? &g_system_config : NULL;
}

uint8_t SystemConfig_GetEffectiveMainboardSlaveId(void)
{
#if MAINBOARD_UART1_SLAVE_ID_FORCE_LIT9
	return 9u;
#else
	const system_config_t *cfg = SystemConfig_Get();
	if (!cfg)
		return SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	uint8_t id = cfg->slave_id;
	if (id == 0xFFu)
		return SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	if (id < SYSTEM_CONFIG_SLAVE_ID_MIN || id > SYSTEM_CONFIG_SLAVE_ID_MAX)
		return SYSTEM_CONFIG_DEFAULT_SLAVE_ID;
	return id;
#endif
}

uint8_t SystemConfig_GetRuntimeCommSlaveId(void)
{
#if MAINBOARD_UART1_SLAVE_ID_FORCE_LIT9
	return 9u;
#else
	return g_runtime_comm_slave_id;
#endif
}

uint16_t SystemConfig_GetSequence(void)
{
	return g_sequence;
}

uint8_t SystemConfig_GetActiveBlock(void)
{
	return g_active_block;
}

int SystemConfig_FactoryReset(void)
{
	uint8_t ff[SYSTEM_CONFIG_BLOCK_SIZE];
	memset(ff, 0xFF, sizeof(ff));
	if (EEPROM_Write(SYSTEM_CONFIG_BLOCK_A_BASE, ff, SYSTEM_CONFIG_BLOCK_SIZE) != 0)
		return -1;
	if (EEPROM_Write(SYSTEM_CONFIG_BLOCK_B_BASE, ff, SYSTEM_CONFIG_BLOCK_SIZE) != 0)
		return -1;

	system_config_t cfg;
	SystemConfig_SetDefaults(&cfg);
	g_sequence = 1;
	g_active_block = 0;
	uint8_t buf[SYSTEM_CONFIG_STORED_BYTES];
	build_stored_block(buf, g_sequence, &cfg);
	if (EEPROM_Write(SYSTEM_CONFIG_BLOCK_A_BASE, buf, SYSTEM_CONFIG_STORED_BYTES) != 0)
		return -1;
	memcpy(&g_system_config, &cfg, sizeof(g_system_config));
	g_loaded = 1;
	g_runtime_comm_slave_id = normalize_slave_id(g_system_config.slave_id);

#if SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET
	SystemConfig_LogFactoryResetDone();
#endif
	return 0;
}

__attribute__((weak)) void SystemConfig_LogSkipSave(void)
{
	/* Override to log "CFG unchanged, skip save" e.g. via UART. */
}

#if SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET
void SystemConfig_LogFactoryResetDone(void)
{
	extern UART_HandleTypeDef huart1;
	const char *msg = "CFG factory reset done\r\n";
	(void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)strlen(msg), 100);
}
#else
__attribute__((weak)) void SystemConfig_LogFactoryResetDone(void)
{
	/* Override to log "CFG factory reset done" e.g. via UART. */
}
#endif

#ifndef SYSCFG_MODBUS_DEBUG_LOG
#define SYSCFG_MODBUS_DEBUG_LOG  0
#endif
#if SYSCFG_MODBUS_DEBUG_LOG
void UpstreamSlave_LogSyscfgRead(uint16_t id, uint16_t baud_code)
{
	extern UART_HandleTypeDef huart1;
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "SYSCFG READ id=%u baud_code=%u\r\n", (unsigned)id, (unsigned)baud_code);
	if (n > 0)
		(void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
}
void UpstreamSlave_LogSyscfgWrite(uint16_t reg, uint16_t value, int result_ok)
{
	extern UART_HandleTypeDef huart1;
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "SYSCFG WRITE reg=%u value=%u result=%s\r\n",
	                 (unsigned)reg, (unsigned)value, result_ok ? "OK" : "ERR");
	if (n > 0)
		(void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100);
}
#else
void UpstreamSlave_LogSyscfgRead(uint16_t id, uint16_t baud_code) { (void)id; (void)baud_code; }
void UpstreamSlave_LogSyscfgWrite(uint16_t reg, uint16_t value, int result_ok) { (void)reg; (void)value; (void)result_ok; }
#endif

__attribute__((weak)) void SystemConfig_LogCurrent(const system_config_t *cfg)
{
	(void)cfg;
}

void SystemConfig_LogToUart(void *huart)
{
	if (!huart || !g_loaded) return;
	UART_HandleTypeDef *u = (UART_HandleTypeDef *)huart;
	const system_config_t *c = &g_system_config;
	char buf[80];
	char blk = (char)('A' + g_active_block);
	int n = snprintf(buf, sizeof(buf), "CFG[%c] seq=%u id=%u baud=%lu valid=1\r\n",
	                blk, (unsigned)g_sequence, (unsigned)c->slave_id, (unsigned long)c->baudrate);
	if (n > 0)
		(void)HAL_UART_Transmit(u, (uint8_t *)buf, (uint16_t)n, 100);
}
