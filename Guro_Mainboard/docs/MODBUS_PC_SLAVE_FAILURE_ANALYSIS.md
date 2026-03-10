# Modbus PC↔메인보드 통신 실패 원인 분석

## 개요

- **확인된 사실**: PC_TEST_AA_STREAM(500ms마다 0xAA 송신) 테스트에서  
  **Mainboard(UART1 + MAX3485) → USB-RS485 동글 → PC 프로그램** 경로가 정상 동작함.  
  즉 **물리계층 / DE 제어 / UART1 설정은 정상**이다.

- **이전 증상**: Modbus 요청(FC03 Read DI 등) 시  
  - **0 received** (수신 바이트 0개)  
  - **Unable to decode response** (응답 프레임 해석 실패)

아래는 위 증상에 대한 **기술적 원인 분석**과 **재발 방지 권장 설계**이다.

---

## 1) WWDG 미갱신으로 인한 주기 리셋 문제

### 동작 요약

- **main.c** 에서 `USE_PC_TEST_UART1_SLAVE=1` 이면 **WWDG 초기화를 하지 않음** (`#if !USE_PC_TEST_UART1_SLAVE` 에서만 `MX_WWDG_Init()` 호출).
- **이전 빌드**에서 WWDG를 항상 초기화하고, 루프에서 `HAL_WWDG_Refresh()` 를 **UART1 슬레이브 경로에서는 호출하지 않았을 가능성**이 있다.

### WWDG 설정 (main.c)

- `Prescaler = WWDG_PRESCALER_1`, `Window = 64`, `Counter = 64`
- STM32F2 WWDG 클록: PCLK1/4096, Prescaler 1 → 카운터 감쇠 주기 약 **수 ms~십수 ms** 구간.
- **갱신을 안 하면** 이 윈도우 내에 리셋 발생 → **약 12ms 단위로 MCU 리셋 반복** 가능.

### 연쇄 효과

| 항목 | 설명 |
|------|------|
| **HAL_GetTick()** | 리셋 시 0으로 초기화. 갱신 직전까지 tick이 거의 안 올라가면, 리셋 주기가 짧을수록 tick 기반 로직 전부 지연/비정상. |
| **LED 타이머** | `LED_Status_Tick_1ms()` 는 `HAL_GetTick()` 에 의존. tick이 안 올라가면 LED 타이머도 줄지 않음. |
| **Modbus 응답** | `HAL_UART_Transmit()` 로 응답 전송 중에 리셋이 나면 **전송이 끊김** → PC는 **0바이트 수신** 또는 **일부만 수신**. |
| **0 received** | 보드가 리셋으로 응답을 못 보내거나, 보내다 끊기면 시리얼 버퍼에 아무것도 안 들어옴 → **0 received** 발생. |

### 결론 (1)

- **실제 원인일 가능성: 높음.**  
  UART1 슬레이브 전용 빌드에서 WWDG가 켜져 있는데 갱신이 빠져 있으면, **주기 리셋 → 응답 미완성/미송신** → **0 received** 로 이어짐.
- 현재 코드는 `USE_PC_TEST_UART1_SLAVE=1` 일 때 WWDG 초기화 자체를 하지 않아, 이 경로에서는 WWDG 리셋이 발생하지 않음.

---

## 2) 0xAA 주기 송신이 Modbus RTU 프레임에 섞였을 가능성

### RTU 특성

- Modbus RTU는 **바이트 스트림** 프로토콜. 프레임 경계는 **3.5 character time 이상의 무음**으로 구분.
- 같은 UART1 선로에서 **0xAA** 를 주기 송신하면, 그 바이트가 **응답 프레임 중간이나 직후**에 끼면, PC가 수신하는 스트림은  
  `[SlaveID][FC][...일부][0xAA][...나머지][CRC]` 형태가 됨.

### 영향

- **중간에 0xAA** 가 끼면:  
  - 길이/구조 불일치, CRC 실패 → **Unable to decode response**.
- **프레임 직후**에 0xAA가 붙으면:  
  - 다음 요청/버퍼 처리 시 잘못된 바이트로 인해 decode 실패 가능.

### 코드상 분리

- **ENABLE_PC_TEST_AA_STREAM=1** 인 빌드:  
  - **0xAA만** 보내는 `PcTestAA_Tick()` 만 동작하고, **Modbus 슬레이브는 초기화/폴링되지 않음** (`UpstreamSlaveUart1_Init` 미호출).  
  - 따라서 이 모드에서는 **0xAA와 Modbus 응답이 동시에 나갈 수 없음**.
- **ENABLE_PC_TEST_AA_STREAM=0**, **BOARD_TX_0XAA_ENABLE=1** 인 빌드:  
  - `upstream_slave_uart1.c` 에서 Modbus 응답 직후 `last_tx_resp_tick` 갱신, **TX_RESP_GUARD_MS(150ms)** 동안 0xAA 송신 금지.  
  - 이전에 이 가드가 없었거나, 0xAA 주기가 짧으면 **프레임 간섭** 가능.

### 결론 (2)

- **실제 원인일 가능성: 중간.**  
  - ENABLE_PC_TEST_AA_STREAM=1 이면 Modbus와 0xAA가 동시에 나가지 않아 해당 모드에서는 이 원인 아님.  
  - Modbus 슬레이브 + 주기 0xAA 를 같이 쓰는 구성을 **과거에** 썼다면, 가드 없이 0xAA가 응답 중간/직후에 나가 **Unable to decode response** 를 유발했을 수 있음.
- 재발 방지: **테스트 모드 분리**(연결 확인=0xAA 전용 / Modbus=0xAA 없음 또는 가드 150ms 유지).

---

## 3) DE/RE 제어 타이밍 문제 가능성

### 현재 구현

- **pc_test_aa_stream.c**:  
  `set_de_tx()` → `HAL_UART_Transmit(..., 1, 50)` → `set_de_rx()` → `HAL_Delay(TX_GUARD_MS)` (2ms).
- **upstream_slave_uart1.c**:  
  `set_de_tx()` → `HAL_UART_Transmit(..., tx_len, 100)` → `set_de_rx()` → `HAL_Delay(TX_GUARD_MS)` (2ms).

### 분석

- `HAL_UART_Transmit()` 은 **마지막 바이트가 FIFO에 들어갈 때** 반환.  
  실제 비트가 라인에 나가는 것은 그 이후이므로, **직후 즉시 DE=RX** 로 바꾸면 마지막 바이트가 잘리거나 왜곡될 수 있음.
- **TX_GUARD_MS = 2ms** 는 9600 baud 기준으로 마지막 비트가 완전히 나가기 위한 여유로 충분한 편.
- 과거에 **가드가 0이거나 매우 짧았을 경우** → TX 완료 전에 RX 모드 전환 → **응답 일부 손실/왜곡** → 0 received 또는 decode 실패 가능.

### 결론 (3)

- **실제 원인일 가능성: 낮음~중간.**  
  현재처럼 2ms 가드가 있으면 DE 타이밍만으로는 설명하기 어렵고, **과거에 가드가 없던 시점**이 있다면 보조 원인으로 고려할 수 있음.
- 재발 방지: **TX 완료 후 DE=RX 전환 + 최소 1~2ms 지연** 유지.

---

## 4) FC03 count=1 / count=2 길이 처리 문제

### 슬레이브(메인보드) 측

- **ModbusRTU_GetExpectedRequestLength()** (modbus_rtu.c): FC03 요청은 **항상 8바이트** (SlaveID, FC, Addr 2B, Count 2B, CRC 2B).  
  count=1 이든 count=2 이든 **요청 길이는 8** 로 동일.
- 슬레이브는 `frame_len >= 8` 이고 CRC만 맞으면 처리하며, `count` 는 frame[4]|frame[5] 로 파싱.  
  → **요청 쪽 count=1/2 는 슬레이브에서 길이 불일치 원인이 아님.**

### PC(클라이언트) 측

- Read DI 시 **FC03, address=2100, count=2** (2레지스터) 전송 → 응답은 **byte_count=4** (4바이트 데이터) + CRC.
- **ModbusRTU_ParseFC03Response()** 는 `frame[2] != num_regs*2` 이면 실패.  
  즉, **수신한 응답 길이/구조가 기대와 다르면** (잘렸거나, 중간에 0xAA가 끼었거나) → **Unable to decode response** 에 해당.

### 결론 (4)

- **실제 원인일 가능성: 낮음(직접 원인 아님).**  
  - count=1/2 에 따른 **요청** 길이 차이는 없음.  
  - **응답** 길이 불일치는 **리셋으로 인한 절단**, **0xAA 끼침**, **DE 타이밍** 등 **다른 원인의 결과**로 보는 것이 타당.
- 즉, **주소/길이 문제** 는 “프레임이 깨진 결과”에 가깝고, 근본 원인은 1·2·3 중 하나일 가능성이 큼.

---

## 5) USART2 / USART1 혼용으로 인한 경로 혼란 가능성

### 구조

- **정상 PC 테스트 경로**: PC ↔ **UART1**(PA9/PA10, DE=PB1) ↔ MAX3485 ↔ RS485 A/B ↔ 동글 ↔ PC.
- **일반 업스트림**: **USART2** 기반 Upstream PC 프로토콜(Modbus+STX/ETX 등).

### 가능한 혼란

- PC를 **USART2** 에 연결했는데, 코드/설정은 **UART1 슬레이브** 기준일 때 → 요청은 UART1으로 가지 않고, 응답도 UART1에서만 나감 → **0 received**.
- 또는 툴/문서에서 포트를 USART2로 안내했을 때 같은 현상.

### 결론 (5)

- **실제 원인일 가능성: 중간.**  
  물리/DE/UART1이 정상이라고 확인된 지금은, **연결 포트(USART1 vs USART2)** 를 잘못 쓰지 않았다면 이 가능성은 낮음.  
  다만 **이전에 포트/보드 연결을 잘못했을 가능성**은 배제할 수 없음.

---

## 최종 결론: 실제 원인 구분

| 구분 | 설명 | 판단 |
|------|------|------|
| **물리 문제** | 배선, A/B, GND, 동글, Baud | **아님.** 0xAA 500ms 수신 정상으로 물리/DE/UART1 정상 확인됨. |
| **워치독 리셋** | WWDG 갱신 누락 → 약 12ms 주기 리셋 → 응답 미완성/미송신 | **가장 유력.** UART1 슬레이브 빌드에서 WWDG가 켜져 있었고 갱신이 없었을 경우 0 received 와 직접 연관됨. |
| **프레임 간섭** | 0xAA가 Modbus 응답 스트림 중간/직후에 섞임 | **가능.** Modbus와 0xAA를 동시에 쓰던 과거 구성에서 가드 없으면 Unable to decode response 유발 가능. |
| **주소/길이 문제** | FC03 count=1/2 요청 길이 처리 오류 | **직접 원인 아님.** 요청은 항상 8바이트; 응답 길이/해석 실패는 리셋·간섭 등에 따른 결과로 보는 것이 맞음. |

- **0 received** → 주로 **워치독 리셋**(응답 미전송/전송 중단) 또는 **경로 혼란**(USART2 연결 등).  
- **Unable to decode response** → **프레임 간섭**(0xAA 끼침) 또는 **리셋으로 인한 절단**으로 인한 길이/CRC 오류.

---

## 재발 방지 권장 설계

1. **테스트 모드 분리**
   - **연결 확인 전용**: `ENABLE_PC_TEST_AA_STREAM=1` → 0xAA만 500ms 주기 송신, Modbus 슬레이브 비동작.  
   - **Modbus 사용**: `ENABLE_PC_TEST_AA_STREAM=0`, `USE_PC_TEST_UART1_SLAVE=1` → Modbus만 동작, 주기 0xAA 없음(또는 아래 2번 적용 후 선택적 사용).

2. **WWDG 조건부 사용**
   - **UART1 슬레이브 사용 시** WWDG 초기화/갱신을 하지 않도록 유지 (`#if !USE_PC_TEST_UART1_SLAVE` 로 `MX_WWDG_Init()` 및 루프 내 `HAL_WWDG_Refresh()` 제어).  
   - 향후 UART1 슬레이브와 WWDG를 같이 쓰려면, **main 루프에서 반드시 주기적으로 `HAL_WWDG_Refresh()` 호출** (12ms 미만 주기 권장).

3. **0xAA 주기 송신과 Modbus 병행 시**
   - **BOARD_TX_0XAA_ENABLE=1** 일 때만 유휴 구간에 0xAA 송신.
   - Modbus **응답 송신 직후** `last_tx_resp_tick` 갱신 및 **TX_RESP_GUARD_MS(150ms)** 동안 0xAA 송신 금지 유지.
   - 가능하면 “연결 확인”은 0xAA 전용 빌드로만 수행하고, Modbus 테스트 시에는 0xAA 비활성화.

4. **DE/RE 타이밍**
   - TX 후 **반드시** `set_de_rx()` 전에 **최소 1~2ms 지연** 유지 (현재 2ms 유지 권장).

5. **문서/연결 안내**
   - PC–메인보드 통신 시 **반드시 UART1(RS485)** 에 연결함을 명시.  
   - USART2는 업스트림(다른 장치)용임을 구분해 기재.

---

*문서 버전: 1.0 | 메인보드 UART1 + MAX3485, PC 툴 Modbus Slave ID 9 기준.*
