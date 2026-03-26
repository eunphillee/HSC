/**
 ******************************************************************************
 * @file    modbus_slave.c
 * @brief   Modbus RTU Slave (Unified Rule v1.2):
 *          - Read  : FC04(Input Register) only
 *          - Write : FC05(Single Coil) only
 *          - FC02/FC03/FC06/FC15/FC16 → EX_ILLEGAL_FUNCTION
 *          - Addr  : 0-index
 *
 *          LPSB FC04 map (reg 0~13):
 *          reg0  alive/status (1=정상)
 *          reg1  error code
 *          reg2  SSR1 state
 *          reg3  SSR2 state
 *          reg4  SSR3 state
 *          reg5  ADC1 AVG
 *          reg6  ADC2 AVG
 *          reg7  ADC3 AVG
 *          reg8  ADC1 PKPK
 *          reg9  ADC2 PKPK
 *          reg10 ADC3 PKPK
 *          reg11 Current1 state
 *          reg12 Current2 state
 *          reg13 Current3 state
 *
 *          FC05 coil 0~2 = SSR1~3 유지.
 ******************************************************************************
 */
#include "modbus_slave.h"
#include "rs485_drv.h"
#include "lpsb_app.h"
#include "adc_app.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

static uint8_t  rx_buf[MODBUS_RX_BUF_SIZE];
static uint16_t rx_len;
static uint32_t last_rx_tick;
static uint8_t  s_rx_byte;

/* Weak debug log (SWO/RTT 등으로 오버라이드 가능). 기본 no-op. */
__attribute__((weak)) void LPSB_Debug_Log(const char *msg) { (void)msg; }

static uint16_t crc16(const uint8_t *buf, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  for (uint16_t i = 0; i < len; i++)
  {
    crc ^= (uint16_t)buf[i];
    for (int j = 0; j < 8; j++)
    {
      if (crc & 1u) crc = (uint16_t)((crc >> 1) ^ 0xA001u);
      else crc >>= 1;
    }
  }
  return crc;
}

static void uart_clear_errors(void)
{
  __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_ORE);
  __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_FE);
  __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_NE);
}

void Modbus_Init(void)
{
  rx_len = 0;
  last_rx_tick = 0;
  RS485_SetRxMode();
  uart_clear_errors();
  (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

static void send_response(const uint8_t *data, uint16_t len)
{
  if (data == NULL || len == 0) return;
  uint16_t crc = crc16(data, len);
  uint8_t buf[MODBUS_RX_BUF_SIZE + 2];
  if (len + 2u > sizeof(buf)) return;
  memcpy(buf, data, len);
  buf[len]     = (uint8_t)(crc & 0xFFu);
  buf[len + 1] = (uint8_t)(crc >> 8u);
  RS485_Send(buf, len + 2);
}

static void send_exception(uint8_t slave, uint8_t fc, uint8_t ex)
{
  uint8_t frame[3];
  frame[0] = slave;
  frame[1] = (uint8_t)(fc + 0x80u);
  frame[2] = ex;
  send_response(frame, 3);
}

static void process_frame(void)
{
  if (rx_len < 8) return;
  uint8_t slave = rx_buf[0];
  uint8_t fc   = rx_buf[1];
  if (slave != LPSB_GetSlaveID())
    return;
  uint16_t crc_calc = crc16(rx_buf, rx_len - 2);
  uint16_t crc_recv = (uint16_t)((uint16_t)rx_buf[rx_len - 1] << 8) | rx_buf[rx_len - 2];
  if (crc_calc != crc_recv)
    return;

  LPSB_LED_RxBlink();

  if (fc == 0x04u) /* Read Input Registers (Unified map) */
  {
    uint16_t start = (uint16_t)((uint16_t)rx_buf[2] << 8) | rx_buf[3];
    uint16_t qty   = (uint16_t)((uint16_t)rx_buf[4] << 8) | rx_buf[5];
    if (qty == 0u || qty > 125u || start > 13u || (start + qty) > 14u)
    {
      send_exception(slave, fc, 0x02u);
      return;
    }
    uint16_t regs[14];
    const uint16_t pkpk1 = LPSB_ADC_GetStoredPkpk(0);
    const uint16_t pkpk2 = LPSB_ADC_GetStoredPkpk(1);
    const uint16_t pkpk3 = LPSB_ADC_GetStoredPkpk(2);
    /* Current state rule (tool/현장 규칙): pkpk >= 30 → ON(1), pkpk < 30 → OFF(0) */
    const uint16_t cur1 = (pkpk1 >= 30u) ? 1u : 0u;
    const uint16_t cur2 = (pkpk2 >= 30u) ? 1u : 0u;
    const uint16_t cur3 = (pkpk3 >= 30u) ? 1u : 0u;

    regs[0]  = 1u; /* alive/status */
    regs[1]  = 0u; /* error code (reserved) */
    regs[2]  = (uint16_t)(LPSB_SSR_Get(0) ? 1u : 0u);
    regs[3]  = (uint16_t)(LPSB_SSR_Get(1) ? 1u : 0u);
    regs[4]  = (uint16_t)(LPSB_SSR_Get(2) ? 1u : 0u);
    regs[5]  = LPSB_ADC_GetStoredAvg(0);
    regs[6]  = LPSB_ADC_GetStoredAvg(1);
    regs[7]  = LPSB_ADC_GetStoredAvg(2);
    regs[8]  = pkpk1;
    regs[9]  = pkpk2;
    regs[10] = pkpk3;
    regs[11] = cur1;
    regs[12] = cur2;
    regs[13] = cur3;

    uint8_t resp[MODBUS_RX_BUF_SIZE];
    resp[0] = slave;
    resp[1] = 0x04u;
    resp[2] = (uint8_t)(qty * 2u);
    for (uint16_t i = 0; i < qty; i++)
    {
      uint16_t v = regs[start + i];
      resp[3 + i * 2]     = (uint8_t)(v >> 8u);
      resp[3 + i * 2 + 1] = (uint8_t)(v & 0xFFu);
    }
    LPSB_Debug_Log("[LPSB] FC04 map ready\r\n");
    send_response(resp, (uint16_t)(3 + qty * 2));
  }
  else if (fc == 0x05u) /* Write Single Coil */
  {
    uint16_t addr = (uint16_t)((uint16_t)rx_buf[2] << 8) | rx_buf[3];
    uint16_t val  = (uint16_t)((uint16_t)rx_buf[4] << 8) | rx_buf[5];
    if (addr > 2u)
    {
      send_exception(slave, fc, 0x02u);
      return;
    }
    uint8_t on = (val == 0xFF00u) ? 1u : 0u;
    LPSB_SSR_Set((uint8_t)addr, on);
    LPSB_Debug_Log("[LPSB] FC05 coil write\r\n");
    send_response(rx_buf, 6);
  }
  else
    send_exception(slave, fc, 0x01u);
}

void Modbus_Poll(void)
{
  /* UART 에러 플래그 감지 시 RX IT 재시작 */
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) ||
      __HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE)  ||
      __HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE)) {
    uart_clear_errors();
    rx_len = 0;
    last_rx_tick = 0;
    RS485_SetRxMode();
    (void)HAL_UART_AbortReceive_IT(&huart1);
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
  }
  uint32_t now = HAL_GetTick();
  if (rx_len > 0 && (now - last_rx_tick) >= MODBUS_RTU_IDLE_MS)
  {
    /* 프레임 처리 중 다음 요청 바이트 혼입 방지 */
    (void)HAL_UART_AbortReceive_IT(&huart1);
    process_frame();
    rx_len = 0;
    uart_clear_errors();
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart1) return;
  last_rx_tick = HAL_GetTick();
  if (rx_len < MODBUS_RX_BUF_SIZE)
    rx_buf[rx_len++] = s_rx_byte;
  RS485_NotifyRxActivity();
  (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}
