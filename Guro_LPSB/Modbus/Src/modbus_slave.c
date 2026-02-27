/**
 * @file modbus_slave.c
 * @brief LPSB: Modbus Slave - receive, dispatch FC01-04/05/06/15/16, respond. LSB-first.
 */
#include "modbus_slave.h"
#include "modbus_rtu.h"
#include "modbus_cfg.h"
#include "modbus_table.h"
#include "io_map.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

static uint8_t rx_buf[64];
static uint16_t rx_len;
static uint32_t last_rx_tick;
#define FRAME_SILENCE_MS  5

static void set_de_tx(void) { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET); }
static void set_de_rx(void) { HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET); }

void ModbusSlave_Init(void)
{
    rx_len = 0;
    last_rx_tick = 0;
    set_de_rx();
}

static void send_response(uint8_t *pdu, size_t pdu_len)
{
    ModbusRTU_AppendCRC(pdu, pdu_len);
    set_de_tx();
    HAL_UART_Transmit(&MODBUS_UART, pdu, (uint16_t)(pdu_len + 2), 100);
    set_de_rx();
}

static void process_frame(void)
{
    if (rx_len < 4) return;
    if (rx_buf[0] != MODBUS_SLAVE_ADDR) return;
    if (ModbusRTU_CRC16Check(rx_buf, rx_len) != 0) return;

    uint8_t fc = rx_buf[1];
    uint8_t tx_pdu[64];
    size_t tx_len = 0;

    switch (fc) {
        case 0x01: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            if (start + num > COIL_COUNT) break;
            uint8_t coil_bits[COIL_COUNT];
            uint8_t coil_bytes[1];
            for (uint16_t i = 0; i < num; i++) coil_bits[i] = ModbusTable_GetCoil(start + i);
            ModbusRTU_PackCoilsLSB(coil_bits, num, coil_bytes);
            tx_len = ModbusRTU_BuildFC01Response(tx_pdu, MODBUS_SLAVE_ADDR, coil_bytes, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x02: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            ModbusTable_RefreshDiscrete();
            if (start + num > DISCRETE_COUNT) break;
            uint8_t disc_bits[DISCRETE_COUNT];
            uint8_t disc_bytes[1];
            for (uint16_t i = 0; i < num; i++) disc_bits[i] = ModbusTable_GetDiscrete(start + i);
            ModbusRTU_PackCoilsLSB(disc_bits, num, disc_bytes);
            tx_len = ModbusRTU_BuildFC02Response(tx_pdu, MODBUS_SLAVE_ADDR, disc_bytes, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x03: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            if (start + num > HOLDING_REG_COUNT) break;
            uint16_t regs[HOLDING_REG_COUNT];
            for (uint16_t i = 0; i < num; i++) regs[i] = ModbusTable_GetHoldingReg(start + i);
            tx_len = ModbusRTU_BuildFC03Response(tx_pdu, MODBUS_SLAVE_ADDR, regs, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x04: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            ModbusTable_RefreshInputRegs();
            if (start + num > INPUT_REG_COUNT) break;
            uint16_t regs[INPUT_REG_COUNT];
            for (uint16_t i = 0; i < num; i++) regs[i] = ModbusTable_GetInputReg(start + i);
            tx_len = ModbusRTU_BuildFC04Response(tx_pdu, MODBUS_SLAVE_ADDR, regs, num);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x05: {
            uint16_t coil_addr; uint8_t value;
            if (ModbusRTU_ParseFC05Request(rx_buf, rx_len, &coil_addr, &value) != 0) break;
            if (coil_addr >= COIL_COUNT) break;
            ModbusTable_SetCoil(coil_addr, value);
            tx_len = ModbusRTU_BuildFC05Response(tx_pdu, MODBUS_SLAVE_ADDR, coil_addr, value);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x06: {
            uint16_t reg_addr; uint16_t value;
            if (ModbusRTU_ParseFC06Request(rx_buf, rx_len, &reg_addr, &value) != 0) break;
            if (reg_addr >= HOLDING_REG_COUNT) break;
            ModbusTable_SetHoldingReg(reg_addr, value);
            tx_len = ModbusRTU_BuildFC06Response(tx_pdu, MODBUS_SLAVE_ADDR, reg_addr, value);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x0F: {
            uint16_t start_addr, num_coils;
            uint8_t coil_bytes[4];
            if (ModbusRTU_ParseFC15Request(rx_buf, rx_len, &start_addr, &num_coils, coil_bytes, sizeof(coil_bytes)) != 0) break;
            if (start_addr + num_coils > COIL_COUNT) break;
            ModbusTable_SetCoilBytesFrom(start_addr, coil_bytes, num_coils);
            tx_len = ModbusRTU_BuildFC15Response(tx_pdu, MODBUS_SLAVE_ADDR, start_addr, num_coils);
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x10: {
            uint16_t start_addr, num_regs;
            uint16_t regs[HOLDING_REG_COUNT];
            if (ModbusRTU_ParseFC16Request(rx_buf, rx_len, &start_addr, &num_regs, regs, HOLDING_REG_COUNT) != 0) break;
            if (start_addr + num_regs > HOLDING_REG_COUNT) break;
            ModbusTable_SetHoldingRegs(start_addr, regs, num_regs);
            tx_len = ModbusRTU_BuildFC16Response(tx_pdu, MODBUS_SLAVE_ADDR, start_addr, num_regs);
            send_response(tx_pdu, tx_len);
            break;
        }
        default:
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
