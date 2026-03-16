/**
 * @file modbus_cfg.h
 * @brief Modbus configuration (HPSB = Slave, address 1).
 *        MAX3485: DE=LOW,/RE=LOW = receive; DE=HIGH,/RE=HIGH = transmit.
 *        RS485_DE_Pin(GPIOA, 현재 PA11)을 DE/RE 공통 제어용으로 사용, idle = LOW = 수신 대기.
 */
#ifndef MODBUS_CFG_HPSB_H
#define MODBUS_CFG_HPSB_H

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MASTER         0
#define MODBUS_SLAVE          1

#define MODBUS_SLAVE_ADDR     1
#define MODBUS_UART           huart1
#define MODBUS_DE_GPIO_PORT   RS485_DE_GPIO_Port
#define MODBUS_DE_GPIO_PIN    RS485_DE_Pin

/* 0=표준: RS485_DE 핀 LOW=수신(DE/RE LOW), HIGH=송신. 1=보드에 인버터: RS485_DE HIGH일 때 transceiver에 LOW 인가되어 수신. */
#ifndef HPSB_RS485_DE_INVERTED
#define HPSB_RS485_DE_INVERTED  0
#endif

/* [HPSB_RS485] 로그 출력: 1=활성화 시 HPSB_RS485_Log() 호출(weak 구현은 no-op, 디버그 UART 등으로 오버라이드 가능). */
#ifndef HPSB_RS485_DEBUG_LOG
#define HPSB_RS485_DEBUG_LOG  1
#endif

/* RS485 direction(PA11) 상태 추적 로그: 1=set_de_tx/set_de_rx/init/send_response 등에서 [RS485] 로그 출력. */
#ifndef HPSB_PA8_TRACE
#define HPSB_PA8_TRACE  1
#endif

/* RS485_DE 핀 강제 토글 진단: 1=Modbus 비활성화, main loop에서 1초마다 LOW↔HIGH 토글. 오실로스코프로 RS485_DE/MAX3485 pin2,3 동작 확인용. */
#ifndef HPSB_RS485_PA8_TEST_MODE
#define HPSB_RS485_PA8_TEST_MODE  0
#endif

/* TX 경로 단독 검증: 1=main에서 2초마다 ModbusSlave_SendTestFrame() 호출. 파싱 없이 고정 8바이트 송신. */
#ifndef HPSB_TX_TEST_ENABLE
#define HPSB_TX_TEST_ENABLE  0
#endif

/* RS485 송신 경로 단독 검증: 1=Modbus 배제, 1초마다 "HPSB_OK\r\n" 송신. HPSB→MAX3485→A/B→PC 경로 확인용. */
#ifndef HPSB_RS485_TX_STRING_TEST
#define HPSB_RS485_TX_STRING_TEST  0
#endif

/* PA9(MCU UART TX) 단독 검증: Modbus/RS485 전부 배제. 0=정상, 1=PA9 GPIO 500ms 토글, 2=PA9 UART 0x55 500ms마다 송신. */
#ifndef HPSB_PA9_TEST_MODE
#define HPSB_PA9_TEST_MODE  0
#endif

/* MAX3485 동작 확인: 1=Modbus 배제, 1초마다 0xAA 1바이트만 DE HIGH → TX → TC → DE LOW 로 PC 전송. */
#ifndef HPSB_MAX3485_TX_AA_TEST
#define HPSB_MAX3485_TX_AA_TEST  0
#endif

#define MODBUS_RTU_RX_BUF_SIZE    64
#define MODBUS_RTU_TX_BUF_SIZE    64
#define MODBUS_MAX_PDU_LEN        64

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CFG_HPSB_H */
