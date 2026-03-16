# HPSB PA8 HIGH 유지 원인 및 수정 보고서

## 1. PA8 제어 위치 전수 검색 결과

| 검색어 | 위치 | 설명 |
|--------|------|------|
| **GPIO_PIN_8** | main.h | `RS485_DE_Pin` = GPIO_PIN_8 |
| **RS485_DE_Pin** | main.c | MX_GPIO_Init: WritePin(..., RS485_DE_Pin, RESET) 2회, PA8=LOW 설정 |
| **RS485_DE_Pin** | modbus_slave.c | MODBUS_DE_GPIO_PIN 매크로로 사용, set_de_tx/set_de_rx에서만 제어 |
| **set_de_tx** | modbus_slave.c | `send_response()` 내부에서만 호출. INVERTED=0이면 **PA8=HIGH** 설정 |
| **set_de_rx** | modbus_slave.c | Init·send_response 종료·process_frame 진입/복귀에서 호출. INVERTED=0이면 **PA8=LOW** 설정 |
| **RS485_DE_GPIO_Port** | main.h | GPIOA |

**PA8을 HIGH로 만드는 코드**
- **유일한 지점**: `modbus_slave.c`의 `set_de_tx()` (INVERTED=0일 때 `GPIO_PIN_SET`).
- `set_de_tx()`는 **오직 `send_response()` 안에서만** 호출됨.
- 따라서 **idle에서 PA8이 HIGH로 유지되는 경우**:
  1. **HPSB_RS485_DE_INVERTED=1** 로 빌드된 경우: `set_de_rx()`가 PA8=**HIGH**를 설정함 (수신 모드로 의도한 반전 로직). 이때 idle이면 코드상으로는 “RX 모드”지만 전기적으로는 DE/RE=HIGH → **실제로는 송신 모드**.
  2. **초기화 순서/덮어쓰기**: MX_GPIO_Init에서 PA8=LOW로 설정한 뒤, 다른 코드나 주변 회로가 PA8을 HIGH로 만드는 경우 (예: 풀업, AF 설정 등).
  3. **send_response() 후 복귀 실패**: 이론상 set_de_rx() 호출 전 예외/리셋이면 TX 상태로 남을 수 있으나, 정상 경로에서는 send_response() 끝에서 반드시 set_de_rx() 호출.

---

## 2. Idle에서 PA8이 HIGH가 되는 이유 (정리)

- **가장 유력**: **HPSB_RS485_DE_INVERTED=1** 로 빌드되어, `set_de_rx()`가 PA8=HIGH를 출력하고 있음.  
  → “수신 대기”로 의도한 상태가 전기적으로는 DE/RE=HIGH(송신 모드)가 됨.
- **대응**:  
  - **표준 MAX3485(DE/RE 직접 구동)** 이면 **HPSB_RS485_DE_INVERTED=0** 으로 빌드해야 함.  
  - `modbus_cfg.h`에서 `HPSB_RS485_DE_INVERTED`가 0인지 확인하고, 필요 시 0으로 고정.

---

## 3. PA8 상태 추적 로그 추가 위치

| 위치 | 로그 |
|------|------|
| MX_GPIO_Init 직후 | `[PA8] after gpio init = LOW` |
| ModbusSlave_Init 시작 | `[PA8] ModbusSlave_Init start` |
| ModbusSlave_Init 끝 | `[PA8] ModbusSlave_Init end (RX mode)` |
| set_de_rx 진입 | `[PA8] set_de_rx` |
| set_de_tx 진입 | `[PA8] set_de_tx` |
| send_response 시작 전 | `[PA8] send_response start` |
| send_response 끝난 직후 | `[PA8] after send_response back to RX` |
| process_frame 진입 | `[PA8] process_frame start ensure RX` |
| process_frame 끝 직후 | `[PA8] process_frame end` |
| poll frame timeout 시 | `[PA8] poll frame timeout` |

- `HPSB_PA8_TRACE=1` (modbus_cfg.h, 기본 1)일 때 위 로그가 weak `HPSB_RS485_Log()`로 출력됨.  
- 디버그 UART 등에서 `HPSB_RS485_Log()`를 구현하면 동일한 포맷으로 수신 가능.

---

## 4. PA8 강제 토글 진단 모드 (HPSB_RS485_PA8_TEST_MODE)

- **modbus_cfg.h**: `HPSB_RS485_PA8_TEST_MODE` (기본 0).
- **1로 설정 시**:
  - `ModbusSlave_Init()` / `ModbusSlave_Poll()` 호출 없음.
  - main에서 `MX_GPIO_Init()` 후 무한 루프로 **1초마다**  
    PA8 **LOW** → 1초 → PA8 **HIGH** → 1초 반복.
- **확인 방법**:  
  - 오실로스코프로 **MCU PA8 패드**, **MAX3485 pin2(/RE)**, **pin3(DE)** 를 동시에 측정.  
  - PA8과 pin2/pin3가 **같은 주기로 동일하게** LOW/HIGH 토글되면, 회로 연결 및 구동은 정상.  
  - PA8만 토글되고 pin2/pin3가 다르면, 중간 버퍼/인버터/풀업 등 회로 확인 필요.

---

## 5. PA8 핀 설정 점검

| 항목 | 결과 |
|------|------|
| **모드** | `MX_GPIO_Init()`에서 RS485_DE_Pin(PA8)을 **GPIO_MODE_OUTPUT_PP** 로 설정. |
| **Alternate** | PA8에 대한 AF 설정 없음. hal_msp.c의 GPIO_AF1_USART1은 USART1(PA9/PA10)용. |
| **다른 peripheral** | SystemClock_Config 등에서 PA8(MCO) 설정 없음. |
| **초기 출력** | WritePin(GPIOA, ..., RS485_DE_Pin, **GPIO_PIN_RESET**) 2회로 **LOW** 설정. |
| **순서** | RS485_DE는 별도 블록으로 초기화되며, 마지막에 USER CODE에서 한 번 더 **LOW** 강제. |

→ 코드 상 PA8은 **출력 push-pull, 초기값 LOW** 로만 설정됨. AF나 다른 주변장치에 의한 덮어쓰기는 없음.

---

## 6. 회로–실측 일치 확인 절차

1. **동일 노드 전압 비교**  
   - **MCU PA8 패드**, **MAX3485 pin2(/RE)**, **pin3(DE)** 를 오실로스코프로 동시 측정.  
   - 기대: 세 지점 전압이 **같은 시점에 동일한 레벨** (같이 LOW/같이 HIGH).
2. **다를 때**  
   - PA8과 pin2/pin3 사이에 **인버터, 풀업, 버퍼** 등이 있으면, schematic 기준으로:  
     - PA8 → 인버터 → /RE 등이면, PA8 LOW일 때 /RE가 HIGH가 될 수 있음.  
     - 이 경우 **HPSB_RS485_DE_INVERTED** 또는 하드웨어 수정으로 “PA8 LOW → DE/RE LOW”가 되도록 맞출 것.
3. **문서화**  
   - schematic에서 PA8 ↔ pin2, pin3 연결(직결/인버터/풀업)을 명시하고, 위 실측 결과(동일 여부)를 기록.

---

## 7. 실패 경로 복귀 (set_de_rx 호출)

- **process_frame()**  
  - **진입 시** 한 번 `set_de_rx()` 호출.  
  - **조기 return** (rx_len<4, slave id 불일치, CRC 오류): return 직전 `set_de_rx()` 호출.  
  - **switch 내 break** (파싱 실패, coil 주소 오류, unsupported FC 등): 해당 break 직전 또는 switch 종료 후 **공통으로** `set_de_rx()` 1회 호출.  
  - **switch 종료 직후** 한 번 더 `set_de_rx()` 호출.  
- **send_response()**  
  - **종료 직전** 항상 `set_de_rx()` 호출 (정상 응답 후 복귀).  
- 따라서 **정상 응답, invalid frame, CRC fail, address mismatch, unsupported FC, exception/early return** 모든 경로에서 **최소 한 번** `set_de_rx()`가 호출되도록 정리됨.

---

## 8. 수정 후 검증 (Idle LOW / 응답 시 HIGH / 완료 후 LOW)

1. **HPSB_RS485_DE_INVERTED=0** 인지 확인 후 빌드.
2. **HPSB_RS485_PA8_TEST_MODE=0** 으로 일반 Modbus 동작으로 실행.
3. **오실로스코프**  
   - **Idle**: PA8(및 MAX3485 pin2,3) **LOW** 유지.  
   - **Mainboard에서 FC05 등 수신 후 HPSB 응답 구간**: PA8 **짧게 HIGH** (송신 구간).  
   - **응답 직후**: PA8 **다시 LOW** (수신 복귀).
4. **로그**  
   - `HPSB_PA8_TRACE=1` 및 `HPSB_RS485_Log()` 구현 시,  
     `[PA8] set_de_rx` → (요청 수신) → `[PA8] set_de_tx` → `[PA8] after send_response back to RX` → `[PA8] set_de_rx` 순서로 출력되는지 확인.

---

## 9. 요약

| 항목 | 내용 |
|------|------|
| **PA8을 HIGH로 만드는 코드** | `set_de_tx()` 한 곳 (modbus_slave.c), `send_response()` 내부에서만 호출. |
| **Idle에서 HIGH 원인** | HPSB_RS485_DE_INVERTED=1 일 때 `set_de_rx()`가 PA8=HIGH로 설정하는 것이 가장 유력. |
| **PA8 토글 테스트** | HPSB_RS485_PA8_TEST_MODE=1 로 1초 주기 LOW/HIGH 토글 후, PA8과 MAX3485 pin2/3 동기 여부 측정. |
| **MCU PA8 vs MAX3485 pin2/3** | 오실로스코프로 세 지점 동시 측정해 전압이 동일한지 확인하고, 다르면 회로도 기준으로 중간 회로 설명. |
| **수정 후 검증** | INVERTED=0, 모든 경로 set_de_rx() 호출 추가 후, idle=LOW, 응답 시 잠깐 HIGH, 응답 후 LOW로 재측정. |
