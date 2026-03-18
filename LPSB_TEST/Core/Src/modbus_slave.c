/**
 ******************************************************************************
 * @file    modbus_slave.c
 * @brief   Modbus RTU Slave: FC03(Read Holding), FC01(Read Coils), FC05(Write Single Coil).
 *          주소맵: Holding 0~8 (SSR1,2,3, ADC1,2,3, SlaveID, Heartbeat, Version), Coil 0~2 (SSR1,2,3).
 ******************************************************************************
 */
#include "modbus_slave.h"
#include "rs485_drv.h"
#include "lpsb_app.h"
#include "adc_app.h"
#include "main.h"
#include <string.h>

static uint8_t  rx_buf[MODBUS_RX_BUF_SIZE];
static uint16_t rx_len;
static uint32_t last_rx_tick;

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

void Modbus_PushByte(uint8_t b)
{
  if (rx_len < MODBUS_RX_BUF_SIZE)
    rx_buf[rx_len++] = b;
  last_rx_tick = HAL_GetTick();
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

  if (fc == 0x03u) /* Read Holding Registers */
  {
    uint16_t start = (uint16_t)((uint16_t)rx_buf[2] << 8) | rx_buf[3];
    uint16_t qty   = (uint16_t)((uint16_t)rx_buf[4] << 8) | rx_buf[5];
    /* 기본 0~8(9개) + 디버깅 확장 9~11: ADC PKPK1~3 (총 12개) */
    if (qty > 125u || start > 11u || (start + qty) > 12u)
    {
      send_exception(slave, fc, 0x02u);
      return;
    }
    uint16_t regs[12];
    regs[0] = (uint16_t)LPSB_SSR_Get(0);
    regs[1] = (uint16_t)LPSB_SSR_Get(1);
    regs[2] = (uint16_t)LPSB_SSR_Get(2);
    regs[3] = LPSB_ADC_GetStoredAvg(0);
    regs[4] = LPSB_ADC_GetStoredAvg(1);
    regs[5] = LPSB_ADC_GetStoredAvg(2);
    regs[6] = (uint16_t)LPSB_GetSlaveID();
    regs[7] = LPSB_GetHeartbeatCount();
    regs[8] = LPSB_FW_VERSION;
    /* 확장 레지스터: AC 전류 확인용 PKPK (미리 계산된 저장값) */
    regs[9]  = LPSB_ADC_GetStoredPkpk(0); /* ADC1 PKPK */
    regs[10] = LPSB_ADC_GetStoredPkpk(1); /* ADC2 PKPK */
    regs[11] = LPSB_ADC_GetStoredPkpk(2); /* ADC3 PKPK */
    uint8_t resp[MODBUS_RX_BUF_SIZE];
    resp[0] = slave;
    resp[1] = 0x03u;
    resp[2] = (uint8_t)(qty * 2u);
    for (uint16_t i = 0; i < qty; i++)
    {
      uint16_t v = regs[start + i];
      resp[3 + i * 2]     = (uint8_t)(v >> 8u);
      resp[3 + i * 2 + 1] = (uint8_t)(v & 0xFFu);
    }
    send_response(resp, (uint16_t)(3 + qty * 2));
  }
  else if (fc == 0x01u) /* Read Coils */
  {
    uint16_t start = (uint16_t)((uint16_t)rx_buf[2] << 8) | rx_buf[3];
    uint16_t qty   = (uint16_t)((uint16_t)rx_buf[4] << 8) | rx_buf[5];
    if (qty > 2000u || start > 2u || (start + qty) > 3u)
    {
      send_exception(slave, fc, 0x02u);
      return;
    }
    uint8_t resp[8];
    resp[0] = slave;
    resp[1] = 0x01u;
    resp[2] = (uint8_t)((qty + 7) / 8);
    resp[3] = (uint8_t)((LPSB_SSR_Get(0) ? 1u : 0u) | (LPSB_SSR_Get(1) ? 2u : 0u) | (LPSB_SSR_Get(2) ? 4u : 0u));
    send_response(resp, (uint16_t)(3 + resp[2]));
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
    send_response(rx_buf, 6);
  }
  else
    send_exception(slave, fc, 0x01u);
}

void Modbus_Poll(void)
{
  uint32_t now = HAL_GetTick();
  if (rx_len > 0 && (now - last_rx_tick) >= MODBUS_RTU_IDLE_MS)
  {
    process_frame();
    rx_len = 0;
  }
}
