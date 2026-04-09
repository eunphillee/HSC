/**
 * @file app_config.h
 * @brief Build-time options: PC test over UART1 RS485 Slave vs normal (USART2 upstream + Master poll).
 *        PC=마스터, 메인보드=슬레이브 ID 9, UART1(PA9/PA10, DE=PB1) RS485 통신.
 *
 * --- PC 테스트 툴 Modbus RTU 요청/응답 호환 설정 (권장) ---
 *   #define ENABLE_PC_TEST_AA_STREAM  0   // 0 = Modbus 슬레이브 동작, FC05(RELAY/SSR) 등 요청 시 UART1로 정상 응답
 *   #define USE_PC_TEST_UART1_SLAVE   1   // 1 = UART1에서 Modbus Slave 동작 (PC 툴이 이 경로로 요청/응답)
 * 이 설정이면 HPSB RELAY1 EN 등 FC05 클릭 시 메인보드가 UART1로 Modbus 응답(0x05 또는 0x85) 정상 반환.
 *
 * 연결 확인만 할 때: ENABLE_PC_TEST_AA_STREAM=1 로 빌드 시 500ms마다 0xAA만 송신(Modbus 미동작).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 1 = PC test: Modbus Slave on USART1 only; Master poll and USART2 Modbus disabled.
 * 0 = Normal: USART2 upstream (Modbus+STX/ETX), USART1 Modbus Master poll. */
#ifndef USE_PC_TEST_UART1_SLAVE
#define USE_PC_TEST_UART1_SLAVE  1   /* 1 = PC 테스트 툴과 Modbus 요청/응답 (UART1) */
#endif

#if USE_PC_TEST_UART1_SLAVE
/* PC 테스트(UART1 Modbus Slave)에서도 UART2 하위보드(HPSB/LPSB) 폴링은 계속 필요하다.
 * - UART1: PC↔Mainboard Modbus Slave
 * - UART2: Mainboard↔Subboards Modbus Master poll (FC04)
 */
#define MODBUS_MASTER_POLL_ENABLE        1
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

/* USART2(RS485 하단) 통신 생존 테스트
 * 1초마다 "MB->SUB TEST\r\n" 송신 후, UART2 수신 이벤트가 발생하면 UART1으로 수신 바이트를 로그로 출력합니다.
 * 테스트 완료 후 0으로 되돌리세요. */
#ifndef UART2_RS485_SUB_TXRX_TEST_ENABLE
#define UART2_RS485_SUB_TXRX_TEST_ENABLE  0
#endif

/* UART2 수신 바이트 로그 출력: (ISR 호출) 부담이 있으므로 테스트 중에만 켜는 것을 권장합니다. */
#ifndef UART2_RS485_SUB_RX_LOG_ENABLE
#define UART2_RS485_SUB_RX_LOG_ENABLE  0
#endif

/* ASCII bridge test mode (UART2 sub-bus):
 * 1 = Modbus master/poll 대신 USART2에서 ASCII 라인 수신 후 USART1로 로그 브리지.
 * 목적: HPSB -> Mainboard -> PC 문자열 경로 확인 ("OKOK\r\n"). */
#ifndef MB_UART2_ASCII_BRIDGE_TEST
#define MB_UART2_ASCII_BRIDGE_TEST  0
#endif

/* USART1 PC 전송 문자열 테스트:
 * 1 = USART1(상위 RS485, DE=PB1)로 "MB_OK\r\n"를 1초마다 송신.
 * 목적: PC 툴이 UART1 수신을 못 받는지(케이블/포트/시리얼 스레드)부터 물리/소프트 레벨 확인. */
#ifndef MB_UART1_TX_OK_STREAM_TEST
#define MB_UART1_TX_OK_STREAM_TEST  0
#endif

/* 연결 확인용: 1 = 500ms마다 0xAA만 송신(Modbus 미동작). 0 = Modbus 슬레이브 동작(FC05 RELAY/SSR 등 UART1 응답 정상). */
#ifndef ENABLE_PC_TEST_AA_STREAM
#define ENABLE_PC_TEST_AA_STREAM  0   /* 0 = PC 테스트 툴 Modbus 요청/응답 사용. 1 = 0xAA 전용(Modbus 무응답). */
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

/* Gateway write (PC→Mainboard FC05 → UART2 to sub): full path log. 1=UART1 출력.
 * PC가 UART1로 연결된 상태에서는 1이면 [GW] 문자열이 Modbus 응답과 섞여 PC가 0 bytes로 인식할 수 있음 → 0 권장. */
#ifndef GATEWAY_WRITE_DEBUG_LOG
#define GATEWAY_WRITE_DEBUG_LOG  0   /* 0=PC 테스트 툴 사용 시 응답 정상 수신. 1=하위버스 디버그(별도 시리얼로 [GW] 확인) */
#endif

/* FC06 처리 경로 상세 로그: [GW] FC06 received / mapped / sending response. 1=UART1 출력.
 * PC 통신과 섞이므로 디버깅 시에만 1로 설정(별도 시리얼 권장). */
#ifndef FC06_DEBUG_LOG
#define FC06_DEBUG_LOG  0
#endif

/* FC05 coil 898/899 수신 시 상세 로그: raw coil, value, range, mapped slave/fc/sub_coil 또는 no mapping.
 * 1=UART1 출력. 898/899 실패 원인 확인용(별도 시리얼 권장). */
#ifndef FC05_COIL_DIAG_LOG
#define FC05_COIL_DIAG_LOG  0
#endif

/* FC05 gateway 전체 경로 단계 로그: recv/tx/rx/exception/normal/cleanup. 0x04 로컬 vs 서브보드 구분, USART2 RX hex.
 * 1=UART1 출력. FC05 실패 후 FC03까지 실패하는 원인 추적용(별도 시리얼 권장). */
#ifndef FC05_GW_STEP_LOG
#define FC05_GW_STEP_LOG  0
#endif

/* FC04 MAP_MAIN 디버그 로그:
 * 1 = [MB][FC04][MAIN] DI regs[2..9] + relay regs[11..14] 출력
 * 0 = 비활성(기본) */
#ifndef ENABLE_MB_FC04_MAIN_DEBUG
#define ENABLE_MB_FC04_MAIN_DEBUG  0
#endif

/* EEPROM 출력 상태(NVM) 진단 로그:
 *  a) NotifySubCoil 호출·저장 결과
 *  b) OutputStateNvm_Save 성공/실패 단계
 *  c) OutputStateNvm_Load 결과 (valid_a/b, seq, block, defaults)
 *  d) RestoreSubBoardsIfNeeded comm_ok 전환
 *  e) restore_sub_board coil 별 want/cur 비교
 * 주의: PC Modbus 통신 중 켜면 응답 프레임이 깨질 수 있습니다.
 *       별도 시리얼(SWO/RTT/UART2 브리지) 또는 PC 미연결 상태에서만 사용하세요. */
#ifndef OUTPUT_STATE_NVM_DEBUG_LOG
#define OUTPUT_STATE_NVM_DEBUG_LOG  0  /* 검증 후 0으로 되돌릴 것 (PC 통신 중 로그가 Modbus 응답과 섞임) */
#endif

#endif /* APP_CONFIG_H */
