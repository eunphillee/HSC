# HSC Communication Diagnostic Mode (LED)

통신 및 출력 제어 시각 검증용 진단 모드 설명.

## Design principle

- **Safest and simplest first** for debugging, not for final polished production behavior.
- **First priority**: visual verification of communication and output control (LED2/3/4 on boards).
- Keep logic minimal; avoid over-engineering until the communication path is confirmed.

## Topology

- **PC ↔ Mainboard**: 상단 RS485, Mainboard Slave ID = 9
- **Mainboard ↔ HPSB/LPSB**: 하단 RS485 (USART2 + MAX3485)
- 하단 Slave ID: HPSB=1, LPSB1=2, LPSB2=4, LPSB3=8

## LED 규칙 (HPSB / LPSB 공통)

| LED  | 의미           | 비고 |
|------|----------------|------|
| LED1 | 전원 표시      | 보드 전원 유효 시 항상 ON |
| LED2 | 채널 1 출력 상태 | HPSB: RELAY1, LPSB: SSR1 |
| LED3 | 채널 2 출력 상태 | HPSB: RELAY2, LPSB: SSR2 |
| LED4 | 채널 3 출력 상태 | HPSB: RELAY3, LPSB: SSR3 |

- **LED 극성**: LED1~4 모두 **LOW active** (LOW=점등, HIGH=소등).  
  구현: `GPIO_PIN_RESET` = ON, `GPIO_PIN_SET` = OFF.
- **출력 극성**: RELAY/SSR 제어는 **active-high** (GPIO_SET=ON, GPIO_RESET=OFF).  
  구현: `IO_HPSB_WriteCoil` / `IO_LPSB_WriteCoil`에서 value=1 → `GPIO_PIN_SET`.

## 매핑 구현 위치

### HPSB

- **LED2/3/4 ↔ RELAY1/2/3**: `Guro_HPSB/Application/Src/led_status.c`
  - `LED_DIAG_COMM_OUTPUT==1` 일 때:
    - LED2 ← `IO_HPSB_ReadCoil(HPSB_COIL_RLY01)` (RELAY1)
    - LED3 ← `IO_HPSB_ReadCoil(HPSB_COIL_RLY02)` (RELAY2)
    - LED4 ← `IO_HPSB_ReadCoil(HPSB_COIL_RLY03)` (RELAY3)
  - Coil → GPIO: `Guro_HPSB/IO/Src/io_map.c` (RLY_EN01/02/03, active-high)

### LPSB

- **LED2/3/4 ↔ SSR1/2/3**: `Guro_LPSB/Application/Src/led_status.c`
  - `LED_DIAG_COMM_OUTPUT==1` 일 때:
    - LED2 ← `IO_LPSB_ReadCoil(LPSB_COIL_SSR1)` (SSR1)
    - LED3 ← `IO_LPSB_ReadCoil(LPSB_COIL_SSR2)` (SSR2)
    - LED4 ← `IO_LPSB_ReadCoil(LPSB_COIL_SSR3)` (SSR3)
  - Coil → GPIO: `Guro_LPSB/IO/Src/io_map.c` (SSR1_EN/2/3, active-high)

### 컴파일 옵션

- `LED_DIAG_COMM_OUTPUT`:  
  - `1` (기본): LED2/3/4 = 채널 1/2/3 출력 상태 (통신 진단용)
  - `0`: 기존 동작 (LED2=any, LED3=전류/폴트, LED4=RS485)
- 정의 위치: 각 보드 `Application/Src/led_status.c` 상단 `#ifndef LED_DIAG_COMM_OUTPUT`

## 메인보드 게이트웨이 로그 (하단 버스 디버깅)

HPSB/LPSB에 명령이 가지 않을 때 하단 경로만 확인하려면:

- `Guro_Mainboard/Application/Inc/app_config.h`  
  `GATEWAY_WRITE_DEBUG_LOG` 를 **1**로 설정 후 빌드 (기본값 1).
- 로그는 **UART1**(PC 연결 포트)으로 출력됨.

전체 경로 로그 순서 (PC에서 HPSB RELAY1 EN 누를 때):

1. `[GW] upstream write received from PC: FC05 addr=898 val=1 (decoded virtual addr)` — 상단 수신
2. `[GW] resolved target board=HPSB slave_id=1 FC=05 coil=0 val=1` — 매핑 결과 (보드, slave, FC, coil)
3. `[GW] UART2 TX start FC05 slave=1 (HPSB) coil=0 val=1` — USART2 송신 시작
4. `[GW] USART2 TX done` — 송신 완료 (PB12 DE→TX 후 전송)
5. `[GW] USART2 RX received OK` / `USART2 RX timeout or invalid` — 서브보드 응답 수신 여부 (60ms 대기)
6. `[GW] final gateway result OK` / `final gateway result FAIL` — 최종 결과 (RX 성공 시에만 OK)

- **PB12 (DE/RE)**: TX 전 `MODBUS_DE_TX_SETTLE_MS`(1ms) 대기, TX 후 `DE_RX_GUARD_MS`(2ms) 대기 후 RX. `modbus_cfg.h` / `modbus_master.c`.
- **성공 정의**: 메인보드는 서브보드 FC05 **응답**을 받아야만 성공으로 처리하고, 실패 시 PC에는 예외(0x85) 응답.

구현: `Application/Src/modbus_master_log.c`, `Modbus/Src/modbus_master.c` (WriteCoil에서 RX 대기).

## PC 툴 진단 시퀀스

- **버튼**: "LED diagnostic sequence" (제어 그룹)
- **동작**:  
  HPSB RELAY1 ON → OFF → RELAY2 ON → OFF → RELAY3 ON → OFF →  
  LPSB1 SSR1 ON → OFF → SSR2 ON → OFF → SSR3 ON → OFF  
  각 단계마다 FC05를 메인보드(Unit 9)로 전송하고, 메인보드가 해당 HPSB/LPSB로 전달.
- **목적**: 보드의 LED2/3/4가 순서대로 켜졌다 꺼지는지 보면서,  
  PC 툴 → 메인보드 → 하단 RS485 → 서브보드 출력 적용까지 동작을 눈으로 확인.

## 기대 결과

- HPSB RELAY1 명령 성공 → HPSB **LED2** 점등
- HPSB RELAY2 명령 성공 → HPSB **LED3** 점등
- HPSB RELAY3 명령 성공 → HPSB **LED4** 점등
- LPSB SSR1 명령 성공 → 해당 LPSB **LED2** 점등
- LPSB SSR2 명령 성공 → 해당 LPSB **LED3** 점등
- LPSB SSR3 명령 성공 → 해당 LPSB **LED4** 점등

이를 통해 문제 구간을 다음처럼 나눌 수 있음:

- PC 툴
- 메인보드 게이트웨이 전달
- 하단 RS485 통신
- 서브보드 출력 적용

## 아키텍처 유지 사항

- PC는 항상 메인보드(Slave ID 9)와만 통신.
- 메인보드가 USART2로 HPSB/LPSB에 FC05 전달.
- 상/하단 버스 구조 및 Slave ID 체계는 변경하지 않음.
