# HPSB RS485 방향 제어(DE/RE) 수정 보고서

## 1. 측정 결과 해석

- **Mainboard USART2**: A/B 신호·PB12(DE/RE) 토글 정상.
- **HPSB MAX3485** (RELAY1 EN 버튼 눌렀을 때):
  - pin2 (/RE) = HIGH  
  - pin3 (DE) = HIGH  
  - TX, RX = LOW  
- **MAX3485**: /RE=HIGH → receiver disabled, DE=HIGH → driver enabled → **송신 모드**.
- **결론**: HPSB는 슬레이브이므로 idle 시 **수신 모드**(DE=LOW, /RE=LOW)여야 하나, 현재는 **송신 모드**로 고정되어 Mainboard 요청을 수신하지 못하는 상태로 판단됨.

---

## 2. HPSB MAX3485 방향 제어 핀

| 항목 | 내용 |
|------|------|
| **GPIO** | **PA8** (단일 핀) |
| **CubeMX/코드 상 이름** | `RS485_DE_Pin`, `RS485_DE_GPIO_Port` (main.h: `GPIO_PIN_8`, `GPIOA`) |
| **연결** | PA8 한 핀으로 MAX3485의 DE와 /RE를 함께 제어하는 구성으로 가정 (회로도 확인 필요). |
| **Active level** | 표준(INVERTED=0): **LOW = 수신**, **HIGH = 송신**. |

---

## 3. Idle 기본 상태

| 모드 | 요구 전기적 상태 | PA8 (INVERTED=0) | PA8 (INVERTED=1) |
|------|------------------|-------------------|-------------------|
| **수신 대기** | DE=LOW, /RE=LOW | PA8 = **LOW** | PA8 = **HIGH** (인버터/풀업 보드 시) |
| **응답 송신** | DE=HIGH, /RE=HIGH | PA8 = **HIGH** | PA8 = **LOW** |

- **기본값**: `HPSB_RS485_DE_INVERTED = 0` → idle 시 PA8 **LOW**로 수신 모드 유지.
- **측정 시 idle에 DE/RE=HIGH**이면:  
  - 회로도에서 PA8–DE/RE 연결 확인.  
  - 보드가 풀업/인버터 구성이면 `HPSB_RS485_DE_INVERTED=1` 로 빌드 후 재측정.

---

## 4. TX/RX 전환 로직

1. **부팅 직후**  
   - `MX_GPIO_Init()` 끝에서 `HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET)` 로 PA8 **LOW** 강제.  
   - `ModbusSlave_Init()` 에서 `set_de_rx()` 호출 → 역시 수신 모드(LOW 또는 INVERTED 시 HIGH) 유지.

2. **평상시 (idle)**  
   - `set_de_rx()` 유지 → 수신 모드.

3. **응답 전송 시** (`send_response()`)  
   - `set_de_tx()` → PA8 HIGH (또는 INVERTED 시 LOW)  
   - 짧은 DE 정착 딜레이  
   - `HAL_UART_Transmit()`  
   - `UART_FLAG_TC` 대기  
   - `set_de_rx()` → 즉시 다시 수신 모드로 복귀.

---

## 5. 수정 전/후 파형 기대값 (PA8 기준, INVERTED=0)

| 구간 | 수정 전 (문제) | 수정 후 (기대) |
|------|----------------|----------------|
| **Idle** | PA8 HIGH (또는 불명확) → DE/RE HIGH → 송신 모드 | PA8 **LOW** → DE/RE LOW → **수신 모드** |
| **Mainboard 요청 수신 중** | 수신 불가 (드라이버 활성) | PA8 계속 **LOW** → 수신 가능 |
| **HPSB 응답 송신** | - | PA8 **잠깐 HIGH** |
| **송신 완료 후** | - | PA8 **다시 LOW** |

---

## 6. GPIO 초기화 점검

- **위치**: `Guro_HPSB/Core/Src/main.c`  
  - `MX_GPIO_Init()` 내 출력 초기값:  
    `HAL_GPIO_WritePin(GPIOA, RLY_EN01_Pin|RLY_EN02_Pin|RLY_EN03_Pin|**RS485_DE_Pin**, GPIO_PIN_RESET)`  
    → PA8 **LOW** 설정.  
  - RS485_DE 핀을 `OUTPUT_PP`로 초기화한 직후, **USER CODE BEGIN MX_GPIO_Init_2** 에서  
    `HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET)` 한 번 더 호출하여 idle = 수신 모드 강제.

---

## 7. 로그 (HPSB_RS485_DEBUG_LOG=1 시)

- **설정**: `Guro_HPSB/Modbus/Inc/modbus_cfg.h` 에서 `HPSB_RS485_DEBUG_LOG` 를 1로 두면 `HPSB_RS485_Log()` 가 호출됨.  
  기본 구현은 weak no-op. 디버그 UART 등으로 오버라이드 가능.

| 로그 | 의미 |
|------|------|
| `[HPSB_RS485] set RX mode (init)` | ModbusSlave_Init에서 수신 모드 설정 |
| `[HPSB_RS485] set TX mode` | 응답 보내기 직전 송신 모드 |
| `[HPSB_RS485] TX start len=N` | UART 송신 시작 |
| `[HPSB_RS485] TX complete` | UART TC 대기 완료 |
| `[HPSB_RS485] back to RX mode` | 송신 후 다시 수신 모드 |
| `[HPSB_RS485] frame byte received` | 첫 바이트 수신 시 |
| `[HPSB_RS485] frame complete len=N` | 프레임 끝(무음 구간 경과) |
| `[HPSB_RS485] slave id matched` | 수신 slave id 일치 |
| `[HPSB_RS485] fc05 addr=X value=Y` | FC05 요청 주소/값 |
| `[HPSB_RS485] before response` | 정상 응답 전송 직전 |
| `[HPSB_RS485] no response ...` / `exception response ...` | CRC/주소/파싱 오류 등으로 응답 없음 또는 예외 응답 |

---

## 8. A/B 측정 해석

- RS485는 **A–B 차동**으로 판단하는 것이 맞음. 단일선–GND 측정은 참고용.
- 다만 현재 증상은 **DE/RE가 HIGH로 유지되어 수신이 불가한 것**이 더 결정적이므로, 우선 방향 제어(idle = 수신) 수정을 적용한 뒤, 필요 시 A/B 레벨을 재측정하는 것을 권장.

---

## 9. 검증 방법

1. **PA8(DE/RE) 파형**  
   - Idle: **LOW** 유지.  
   - Mainboard에서 FC05 등 요청 보낼 때: 구간 내내 **LOW**.  
   - HPSB가 응답 보낼 때만 **짧게 HIGH** 후 다시 **LOW**.

2. **동작 확인**  
   - PC에서 HPSB RELAY1 EN 클릭 → Mainboard가 HPSB로 FC05 전송.  
   - HPSB가 요청 수신 후 FC05 정상 응답 → PC에서 RX OK, 릴레이/LED 반응 확인.

3. **로그**  
   - `HPSB_RS485_DEBUG_LOG=1` 및 `HPSB_RS485_Log()` 구현 시  
     `frame byte received` → `slave id matched` → `fc05 addr=0 value=1` → `before response` → `set TX mode` → `back to RX mode` 순서로 출력되는지 확인.
