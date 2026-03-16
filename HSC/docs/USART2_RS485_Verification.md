# Mainboard ↔ HPSB/LPSB USART2 RS485 하위 통신 검증

## 1. 개요

- **목표**: Mainboard가 USART2 Master로 HPSB(1), LPSB1(2), LPSB2(4), LPSB3(8)와 Modbus RTU 통신하도록 안정화.
- **제약**: PC ↔ Mainboard USART1 통신은 변경 없이 유지. 기존 메인보드 기능 유지, USART2 하위 통신만 수정.

## 2. 수정 요약

### 2.1 Mainboard

| 파일 | 변경 내용 |
|------|-----------|
| `Modbus/Src/modbus_master.c` | (1) TX **전에만** RX flush, TX 후 flush/지연 제거. (2) USART2 송신 공통화: `uart2_flush_rx()` + `uart2_tx()` (DE HIGH → settle → TX → TC 대기 → DE LOW). (3) `send_request()`, `WriteCoil()`, `WriteHoldingReg()` 모두 동일 경로 사용. (4) `parse_response()`에서 slave/FC/CRC 검사 및 실패 시 구체적 로그(CRC fail, slave mismatch, FC mismatch). |
| `Modbus/Inc/modbus_cfg.h` | `MODBUS_FC05_RX_DELAY_MS` = 0, 주석: TX 후 flush/지연 금지. |
| `Application/Src/modbus_master_log.c` | `LogSubPollTxOk`, `LogSubPollRxTimeout`, `LogSubPollRxLen` 추가. |
| `Modbus/Inc/modbus_master.h` | 위 로그 함수 선언 추가. |

### 2.2 HPSB

| 파일 | 변경 내용 |
|------|-----------|
| `Modbus/Src/modbus_slave.c` | `FRAME_SILENCE_MS` = 4, 주석: 9600 bps t3.5 ≈ 3.65 ms. (기존 `send_response`에 DE settle + TC wait 이미 있음) |

### 2.3 LPSB

| 파일 | 변경 내용 |
|------|-----------|
| `Modbus/Src/modbus_slave.c` | (1) `send_response()`: DE HIGH → 짧은 settle → TX → UART TC 대기 → DE LOW (HPSB와 동일). (2) `FRAME_SILENCE_MS` = 4. (3) ID_BIT1/2/3(PB0/PB1/PB3)으로 **런타임 slave 주소**: ID_BIT1만=2, ID_BIT2만=4, ID_BIT3만=8, 그 외=2. (4) `ModbusSlave_GetAddress()`로 디버그용 주소 확인. |
| `Modbus/Inc/modbus_cfg.h` | 주석: 실제 주소는 ID_BIT에서 설정. |
| `Modbus/Inc/modbus_slave.h` | `ModbusSlave_GetAddress()` 선언. |

### 2.4 BSP

- `bsp_rs485_sub.c`: 수정 없음. USART2 송신은 **modbus_master.c**의 `uart2_tx()`만 사용하도록 통일.

---

## 3. 검증 시나리오

### 3.1 사전 조건

- Mainboard: `ENABLE_PC_TEST_AA_STREAM=0`, `USE_PC_TEST_UART1_SLAVE=1`, `GATEWAY_WRITE_DEBUG_LOG=0` (PC 툴 사용 시).
- 폴 로그 확인 시에만: `MODBUS_MASTER_DEBUG_LOG=1` (UART1에 SUB 로그 출력 → PC 툴과 동시 사용 시 응답 깨질 수 있음, 별도 시리얼 모니터 권장).
- HPSB/LPSB 보드: 9600 bps, HPSB slave=1 고정, LPSB는 ID_BIT로 2/4/8 중 하나.

### 3.2 시나리오 1: Mainboard → HPSB(slave 1) FC05 coil 0 write

1. PC 테스트 툴에서 메인보드(슬레이브 9) 연결, **HPSB RELAY1 EN** 클릭.
2. 메인보드가 FC05 (slave=1, coil=0, value=0xFF)를 USART2로 전송.
3. HPSB가 8바이트 FC05 응답 수신 대기 (타임아웃 120 ms).
4. **성공 시**: PC 툴에 RX OK, HPSB LED2 등 하드웨어 반응.
5. **실패 시**: PC 툴에 exception 등; `GATEWAY_WRITE_DEBUG_LOG=1`이면 UART1에 `[GW] UART2 RX timeout or invalid` 등.

### 3.3 시나리오 2: Mainboard → LPSB(slave 2) FC05 coil 0 write

1. LPSB 보드에서 ID_BIT1만 HIGH (나머지 LOW) → slave 주소 2.
2. PC 툴에서 **LPSB1** 선택 후 **SSR1 EN** 클릭.
3. 메인보드가 FC05 (slave=2, coil=0) 전송 → LPSB가 응답.
4. **성공 시**: PC 툴 RX OK, LPSB SSR1 출력 반응.

### 3.4 시나리오 3: Mainboard에서 slave 4, 8 폴 시도

1. LPSB 보드 2대: 한 대는 ID_BIT2만 HIGH (slave 4), 다른 한 대는 ID_BIT3만 HIGH (slave 8).
2. 메인보드가 폴 테이블대로 slave 4, 8에 FC01/02/03/04 요청 전송.
3. **성공 시**: `MODBUS_MASTER_DEBUG_LOG=1`이면 `SUB slave=4 (LPSB2) OK`, `SUB slave=8 (LPSB3) OK` 등.
4. **실패 시**: `SUB slave=4 RX timeout` 또는 `SUB slave=4 fail: CRC fail` 등.

### 3.5 TX / RX / 성공·실패 로그

- **Gateway write (FC05)**  
  - `GATEWAY_WRITE_DEBUG_LOG=1` 시: `[GW] command parsed`, `[GW] UART2 TX start`, `[GW] USART2 TX done`, `[GW] USART2 DE=HIGH/LOW`, `[GW] UART2 RX OK` 또는 `timeout or invalid`, `[GW] final gateway result OK/FAIL`.

- **Poll (MODBUS_MASTER_DEBUG_LOG=1)**  
  - 요청 시작: `SUB POLL start slave=N (HPSB|LPSB1|LPSB2|LPSB3)`  
  - TX 성공: `SUB slave=N TX OK`  
  - RX 수신: `SUB slave=N RX len=M`  
  - RX 타임아웃: `SUB slave=N RX timeout`, `SUB slave=N fail: timeout`  
  - 실패 사유: `SUB slave=N fail: CRC fail` / `slave mismatch` / `FC mismatch` / `parse fail` / `exception`  
  - 성공: `SUB slave=N OK coils=... dis=... raw=[...]`

---

## 4. 실제 통신 성공 확인 절차

1. **빌드**
   - Mainboard, HPSB, LPSB 각각 빌드 후 해당 보드에 다운로드.
2. **연결**
   - PC —(USART1 RS485)— Mainboard —(USART2 RS485)— HPSB / LPSB1·2·3.
   - Baud 9600, DE/RE 배선(MAX3485) 확인.
3. **HPSB 단독**
   - PC 툴로 HPSB RELAY1 EN → 로그에서 RX OK, HPSB LED2 점등 확인.
4. **LPSB 주소**
   - 각 LPSB에서 ID_BIT 설정 후 전원 리셋, 필요 시 `ModbusSlave_GetAddress()`/LED로 2·4·8 확인.
5. **LPSB 제어**
   - PC 툴에서 LPSB1(2), LPSB2(4), LPSB3(8) 각각 SSR EN → RX OK 및 SSR 출력 확인.
6. **폴 로그**
   - `MODBUS_MASTER_DEBUG_LOG=1`로 빌드 후, **PC 툴과 다른** 시리얼 포트로 메인보드 UART1 로그 수신 시 SUB POLL start / TX OK / RX len / timeout·CRC·slave·FC fail / OK 메시지 확인.

---

## 5. 오실로스코프 없이 확인할 수 있는 로그 포인트

| 확인 항목 | 로그/동작 |
|-----------|-----------|
| 메인보드가 하위로 요청 전송 | `SUB POLL start`, `SUB slave=N TX OK` (폴), 또는 `[GW] UART2 TX start` (FC05). |
| 메인보드가 슬레이브 응답 수신 | `SUB slave=N RX len=M`, `SUB slave=N OK` (폴), 또는 `[GW] UART2 RX OK` (FC05). |
| 타임아웃 | `SUB slave=N RX timeout`, `[GW] UART2 RX timeout or invalid`. |
| CRC 오류 | `SUB slave=N fail: CRC fail`. |
| 잘못된 slave/FC | `SUB slave=N fail: slave mismatch`, `SUB slave=N fail: FC mismatch`. |
| PC 툴 기준 성공 | PC 툴 로그에 `[HPSB]`/`[LPSBx]` TX FC05, RX OK, Write \| Response: OK. |
| LPSB 현재 주소 | 부팅 시 `ModbusSlave_GetAddress()` 반환값을 LED/별도 UART로 출력해 2/4/8 확인. |

---

## 6. 타이밍 요약

- **Mainboard**: TX 직후 **flush/추가 지연 없이** 즉시 `HAL_UART_Receive()`로 8바이트(FC05) 또는 폴 응답 대기. 슬레이브가 t3.5(약 4 ms) 이후 응답을 시작하므로, TX 후 flush를 하지 않아야 첫 바이트를 놓치지 않음.
- **HPSB/LPSB**: `FRAME_SILENCE_MS=4` (9600 bps t3.5 ≈ 3.65 ms). 응답 시 DE settle + TX 후 UART TC 대기 후 DE LOW로 메인보드 수신과 충돌 최소화.
