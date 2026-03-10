/**
 * @file app_config.h
 * @brief Build-time options: PC test over UART1 RS485 Slave vs normal (USART2 upstream + Master poll).
 *        PC=마스터, 메인보드=슬레이브 ID 9, UART1(PA9/PA10, DE=PB1) RS485 통신.
 *
 * 연결 살아있는지 확인(단순): 메인보드가 500ms마다 0xAA 전송 → PC 툴이 수신해서 현시.
 * 이 모드를 쓰려면 ENABLE_PC_TEST_AA_STREAM=1 로 빌드. (Modbus Read DI/Relay 쓰려면 0으로.)
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 1 = PC test: Modbus Slave on USART1 only; Master poll and USART2 Modbus disabled.
 * 0 = Normal: USART2 upstream (Modbus+STX/ETX), USART1 Modbus Master poll. */
#ifndef USE_PC_TEST_UART1_SLAVE
#define USE_PC_TEST_UART1_SLAVE  1
#endif

#if USE_PC_TEST_UART1_SLAVE
#define MODBUS_MASTER_POLL_ENABLE        0
#define UPSTREAM_PC_MODBUS_SLAVE_ENABLE  0
#else
#define MODBUS_MASTER_POLL_ENABLE        1
#define UPSTREAM_PC_MODBUS_SLAVE_ENABLE  1
#endif

/* RS485 DE/RE (PB1) polarity for UART1 slave: 1=Mode A (TX=SET, RX=RESET), 0=Mode B (TX=RESET, RX=SET).
 * If UART1 slave shows 0 received / no response, set to 0 and rebuild. */
#ifndef RS485_DE_ACTIVE_HIGH
#define RS485_DE_ACTIVE_HIGH  1
#endif

/* Force PB1(RS485_DE_RE) at boot and during operation: RX only or TX only (for receive test).
 * FORCE_RS485_RX=1: PB1 kept in RX (set_de_tx no-op) → Modbus 응답을 보낼 수 없음, PC에서 0 received.
 * Modbus 슬레이브(Read DI/Relay 응답)를 쓰려면 반드시 FORCE_RS485_RX=0. */
#ifndef FORCE_RS485_RX
#define FORCE_RS485_RX  0   /* 0=정상 송수신. 1이면 응답 전송 안 됨(0 received). */
#endif
#ifndef FORCE_RS485_TX
#define FORCE_RS485_TX  0
#endif

/* UART1 Modbus Slave debug log: 1 = RX frame HEX(up to 16B) + CRC OK/FAIL + FC + addr, and periodic invalid_len/crc counts */
#ifndef UPSTREAM_DEBUG_LOG
#define UPSTREAM_DEBUG_LOG  0
#endif

/* 테스트용 보드→PC 0xAA 주기 송신: 1 = 500ms마다 송신(수신 확인용), 0 = OFF(Modbus 응답과 섞이지 않도록 기본 OFF) */
#ifndef BOARD_TX_0XAA_ENABLE
#define BOARD_TX_0XAA_ENABLE  0
#endif

/* 연결 확인용: 메인보드 → PC 500ms마다 0xAA 전송, PC는 수신 현시. 1=이 모드(0xAA만 송신), 0=Modbus 슬레이브 동작(Read DI/Relay 가능). */
#ifndef ENABLE_PC_TEST_AA_STREAM
#define ENABLE_PC_TEST_AA_STREAM  0   /* 0=Modbus 테스트(Read DI/Relay/PC Status). 연결 확인만 할 때만 1로. */
#endif

/* 부팅 후 EEPROM 설정 로드/저장 결과를 UART1로 한 줄 로그 출력. 1=테스트 시 활성화 (CFG[A/B] seq= id= baud= valid=1) */
#ifndef SYSTEM_CONFIG_BOOT_LOG
#define SYSTEM_CONFIG_BOOT_LOG  0
#endif

/* Save 시 값이 동일하면 EEPROM 쓰기 생략. 1=생략 시 "CFG unchanged, skip save" 로그 출력 옵션 사용 */
#ifndef SYSTEM_CONFIG_LOG_SKIP_SAVE
#define SYSTEM_CONFIG_LOG_SKIP_SAVE  0
#endif

/* Factory reset 수행 후 "CFG factory reset done" 부팅 로그 출력. 1=출력 */
#ifndef SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET
#define SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET  0
#endif

/* 4x3000~3002 FC03/FC06 접근 시 디버그 로그: SYSCFG READ id= baud_code= / SYSCFG WRITE reg= value= result= */
#ifndef SYSCFG_MODBUS_DEBUG_LOG
#define SYSCFG_MODBUS_DEBUG_LOG  0
#endif

/* UART2 하위 폴링(HPSB/LPSB): poll start, OK(포트/전류 요약), timeout/CRC fail 로그. 1=UART1 출력 */
#ifndef MODBUS_MASTER_DEBUG_LOG
#define MODBUS_MASTER_DEBUG_LOG  0
#endif

/* Gateway write (PC→Mainboard FC05 898..909 → UART2 to sub): upstream command, target board/slave_id/channel, FC05 on UART2, subboard response. 1=UART1 출력 (communication diagnostic) */
#ifndef GATEWAY_WRITE_DEBUG_LOG
#define GATEWAY_WRITE_DEBUG_LOG  0   /* set to 1 for LED diagnostic / comm verification */
#endif

#endif /* APP_CONFIG_H */
