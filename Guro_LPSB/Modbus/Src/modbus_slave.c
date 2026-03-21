/**
 * @file modbus_slave.c
 * @brief LPSB: Modbus Slave - receive, dispatch FC01-04/05/06/15/16, respond. LSB-first.
 */
#include "modbus_slave.h"
#include "modbus_rtu.h"
#include "modbus_cfg.h"
#include "modbus_table.h"
#include "io_map.h"
#include "led_status.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define EX_ILLEGAL_FUNCTION      0x01u
#define EX_ILLEGAL_DATA_ADDRESS  0x02u
#define EX_ILLEGAL_DATA_VALUE    0x03u
#define EX_SLAVE_DEVICE_FAILURE  0x04u

extern UART_HandleTypeDef huart1;

__attribute__((weak)) void LPSB_Debug_Log(const char *msg) { (void)msg; }

static uint8_t rx_buf[64];
static uint16_t rx_len;
static uint32_t last_rx_tick;
/* Runtime slave address from ID_BIT1/2/3 (PB0/PB1/PB3): one-hot → LPSB1=2, LPSB2=4, LPSB3=8 */
static uint8_t s_slave_addr;
/* Modbus RTU t3.5 at 9600 bps ≈ 3.65 ms; use 4 ms for frame end (mainboard 응답 대기와 충돌 방지) */
#define FRAME_SILENCE_MS  4

static void set_de_tx(void) { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET); }
static void set_de_rx(void) { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET); }

/** Read slave address from ID_BIT1/2/3: ID_BIT1 only=2, ID_BIT2 only=4, ID_BIT3 only=8; else 2. */
static uint8_t read_slave_addr_from_id_bits(void)
{
    uint8_t b1 = IO_LPSB_ReadDiscrete(LPSB_DISCRETE_ID_BIT1);
    uint8_t b2 = IO_LPSB_ReadDiscrete(LPSB_DISCRETE_ID_BIT2);
    uint8_t b3 = IO_LPSB_ReadDiscrete(LPSB_DISCRETE_ID_BIT3);
    if (b1 && !b2 && !b3) return 2;   /* LPSB1 */
    if (!b1 && b2 && !b3) return 4;   /* LPSB2 */
    if (!b1 && !b2 && b3) return 8;   /* LPSB3 */
    return 2;   /* default LPSB1 */
}

void ModbusSlave_Init(void)
{
    rx_len = 0;
    last_rx_tick = 0;
    s_slave_addr = read_slave_addr_from_id_bits();
    set_de_rx();
}

uint8_t ModbusSlave_GetAddress(void)
{
    return s_slave_addr;
}

static void send_response(uint8_t *pdu, size_t pdu_len)
{
    char l[64];
    int ln = snprintf(l, sizeof(l), "[LPSB-SLAVE] tx resp len=%u\r\n", (unsigned)(pdu_len + 2u));
    if (ln > 0) LPSB_Debug_Log(l);
    ModbusRTU_AppendCRC(pdu, pdu_len);
    set_de_tx();
    for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }  /* DE settle before TX */
    HAL_UART_Transmit(&MODBUS_UART, pdu, (uint16_t)(pdu_len + 2), 100);
    while (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_TC) == RESET) { }  /* wait last byte out */
    set_de_rx();
    LED_Status_OnRS485Activity();
}

static void log_hex(const char *prefix, const uint8_t *buf, uint16_t len)
{
    char line[192];
    int n = snprintf(line, sizeof(line), "%s len=%u data=", prefix, (unsigned)len);
    if (n < 0) return;
    for (uint16_t i = 0; i < len && i < 32u && n < (int)(sizeof(line) - 4); i++) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%02X ", buf[i]);
    }
    n += snprintf(line + n, sizeof(line) - (size_t)n, "\r\n");
    if (n > 0) LPSB_Debug_Log(line);
}

static void send_exception(uint8_t req_fc, uint8_t ex_code, const char *reason)
{
    uint8_t pdu[8];
    char dbg[96];
    pdu[0] = s_slave_addr;
    pdu[1] = (uint8_t)(req_fc | 0x80u);
    pdu[2] = ex_code;
    if (reason == NULL) reason = "?";
    (void)snprintf(dbg, sizeof(dbg), "[LPSB-SLAVE] exception 0x%02X reason=%s\r\n", (unsigned)ex_code, reason);
    LPSB_Debug_Log(dbg);
    send_response(pdu, 3u);
}

static void process_frame(void)
{
    if (rx_len < 4) return;
    if (rx_buf[0] != s_slave_addr) return;
    if (ModbusRTU_CRC16Check(rx_buf, rx_len) != 0) return;
    log_hex("[LPSB-SLAVE] rx raw", rx_buf, rx_len);

    LED_Status_OnRS485Activity();  /* valid RX */

    uint8_t fc = rx_buf[1];
    {
        uint16_t addr = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
        uint16_t cv = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
        char dbg[96];
        (void)snprintf(dbg, sizeof(dbg), "[LPSB-SLAVE] parsed fc=%02X addr=%u val/count=%u\r\n",
                       (unsigned)fc, (unsigned)addr, (unsigned)cv);
        LPSB_Debug_Log(dbg);
    }
    uint8_t tx_pdu[64];
    size_t tx_len = 0;

    switch (fc) {
        case 0x01: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            if (start + num > COIL_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc01 addr out of range"); break; }
            uint8_t coil_bits[COIL_COUNT];
            uint8_t coil_bytes[1];
            for (uint16_t i = 0; i < num; i++) coil_bits[i] = ModbusTable_GetCoil(start + i);
            ModbusRTU_PackCoilsLSB(coil_bits, num, coil_bytes);
            tx_len = ModbusRTU_BuildFC01Response(tx_pdu, s_slave_addr, coil_bytes, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x02: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            ModbusTable_RefreshDiscrete();
            if (start + num > DISCRETE_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc02 addr out of range"); break; }
            uint8_t disc_bits[DISCRETE_COUNT];
            uint8_t disc_bytes[1];
            for (uint16_t i = 0; i < num; i++) disc_bits[i] = ModbusTable_GetDiscrete(start + i);
            ModbusRTU_PackCoilsLSB(disc_bits, num, disc_bytes);
            tx_len = ModbusRTU_BuildFC02Response(tx_pdu, s_slave_addr, disc_bytes, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x03: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            if (start + num > HOLDING_REG_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc03 addr out of range"); break; }
            uint16_t regs[HOLDING_REG_COUNT];
            for (uint16_t i = 0; i < num; i++) regs[i] = ModbusTable_GetHoldingReg(start + i);
            tx_len = ModbusRTU_BuildFC03Response(tx_pdu, s_slave_addr, regs, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x04: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            ModbusTable_RefreshInputRegs();
            if (start + num > INPUT_REG_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc04 addr out of range"); break; }
            uint16_t regs[INPUT_REG_COUNT];
            for (uint16_t i = 0; i < num; i++) regs[i] = ModbusTable_GetInputReg(start + i);
            tx_len = ModbusRTU_BuildFC04Response(tx_pdu, s_slave_addr, regs, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x05: {
            uint16_t coil_addr; uint8_t value;
            if (ModbusRTU_ParseFC05Request(rx_buf, rx_len, &coil_addr, &value) != 0) { send_exception(fc, EX_ILLEGAL_DATA_VALUE, "fc05 parse fail"); break; }
            if (coil_addr >= COIL_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc05 coil out of range"); break; }
            ModbusTable_SetCoil(coil_addr, value);
            tx_len = ModbusRTU_BuildFC05Response(tx_pdu, s_slave_addr, coil_addr, value);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x06: {
            uint16_t reg_addr; uint16_t value;
            if (ModbusRTU_ParseFC06Request(rx_buf, rx_len, &reg_addr, &value) != 0) { send_exception(fc, EX_ILLEGAL_DATA_VALUE, "fc06 parse fail"); break; }
            if (reg_addr >= HOLDING_REG_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc06 reg out of range"); break; }
            ModbusTable_SetHoldingReg(reg_addr, value);
            tx_len = ModbusRTU_BuildFC06Response(tx_pdu, s_slave_addr, reg_addr, value);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x0F: {
            uint16_t start_addr, num_coils;
            uint8_t coil_bytes[4];
            if (ModbusRTU_ParseFC15Request(rx_buf, rx_len, &start_addr, &num_coils, coil_bytes, sizeof(coil_bytes)) != 0) { send_exception(fc, EX_ILLEGAL_DATA_VALUE, "fc15 parse fail"); break; }
            if (start_addr + num_coils > COIL_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc15 addr out of range"); break; }
            ModbusTable_SetCoilBytesFrom(start_addr, coil_bytes, num_coils);
            tx_len = ModbusRTU_BuildFC15Response(tx_pdu, s_slave_addr, start_addr, num_coils);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x10: {
            uint16_t start_addr, num_regs;
            uint16_t regs[HOLDING_REG_COUNT];
            if (ModbusRTU_ParseFC16Request(rx_buf, rx_len, &start_addr, &num_regs, regs, HOLDING_REG_COUNT) != 0) { send_exception(fc, EX_ILLEGAL_DATA_VALUE, "fc16 parse fail"); break; }
            if (start_addr + num_regs > HOLDING_REG_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc16 addr out of range"); break; }
            ModbusTable_SetHoldingRegs(start_addr, regs, num_regs);
            tx_len = ModbusRTU_BuildFC16Response(tx_pdu, s_slave_addr, start_addr, num_regs);
            send_response(tx_pdu, tx_len);
            break;
        }
        default:
            send_exception(fc, EX_ILLEGAL_FUNCTION, "unsupported function");
            break;
    }
}

void ModbusSlave_Poll(void)
{
    uint8_t b;
    while (HAL_UART_Receive(&MODBUS_UART, &b, 1, 0) == HAL_OK) {
        last_rx_tick = HAL_GetTick();
        if (rx_len < sizeof(rx_buf))
            rx_buf[rx_len++] = b;
    }
    if (rx_len > 0 && (HAL_GetTick() - last_rx_tick) >= FRAME_SILENCE_MS) {
        process_frame();
        rx_len = 0;
    }
}
