/**
 * @file modbus_slave.h
 * @brief HPSB: Modbus RTU Slave - FC01/02/03/04/05/06/15/16.
 */
#ifndef MODBUS_SLAVE_HPSB_H
#define MODBUS_SLAVE_HPSB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ModbusSlave_Init(void);
void ModbusSlave_Poll(void);
/** 비블로킹 디버그 LED 처리: main loop에서 주기적으로 호출. 수신 경로에서 HAL_Delay 제거 후 LED 표시용. */
void ModbusSlave_ProcessDebugLEDs(void);

/** Direct Modbus 무응답 디버그: 디버거/watch에서 확인. rx/parse/reply 단계 구분용. */
extern volatile uint16_t HPSB_dbg_rx_len;
extern volatile uint8_t  HPSB_dbg_crc_ok;
extern volatile uint8_t  HPSB_dbg_slave_match;
extern volatile uint8_t  HPSB_dbg_reply_started;
extern volatile uint8_t  HPSB_dbg_reply_done;

/** TX 경로 단독 검증: Modbus 파싱 없이 고정 8바이트 테스트 프레임 송신. HPSB_TX_TEST_ENABLE=1 시 main에서 주기 호출. */
void ModbusSlave_SendTestFrame(void);

/** RS485 송신 경로 단독 검증: 고정 문자열 송신. DE HIGH → UART TX → TC → DE LOW. HPSB_RS485_TX_STRING_TEST=1 시 main에서 1초마다 "HPSB_OK\\r\\n" 호출. */
void ModbusSlave_SendTestString(const char *str, uint16_t len);

/* RS485 direction 핀 상태 추적: HPSB_PA8_TRACE=1 시 main.c 등에서 호출. weak HPSB_RS485_Log()로 출력. */
void ModbusSlave_PA8_Log(const char *msg);

/** 수신 raw 프레임/디버그 문자열 출력. weak: 기본 no-op.
 *  로그를 보려면 다른 .c에서 구현 (예: SWO/ITM, 또는 별도 UART로 전송).
 *  process_frame()에서 RX frame hex, rx_len, rx_crc, calc_crc, CRC OK/FAIL 출력. */
void HPSB_Debug_Log(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_HPSB_H */
