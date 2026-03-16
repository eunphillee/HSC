# PA9(MCU UART TX) 단독 검증 — 분석 및 사용법

## 목적

Modbus/RS485/DE를 보지 않고, **MCU PA9에서 실제로 신호가 나오는지**만 검증.

- PA9에 파형이 나와야 “HPSB가 송신한다”고 말할 수 있음.
- PA9에 아무것도 안 나오면 그 이하(MAX3485, A/B, PC)는 의미 없음.

---

## 1) PA9 GPIO 토글 테스트 (핀/클럭 검증)

### 설정

`Modbus/Inc/modbus_cfg.h` 또는 해당 정의 위치에서:

```c
#define HPSB_PA9_TEST_MODE  1
```

### 동작

- **Modbus/RS485 코드 전부 미실행.**
- `MX_GPIO_Init()` 만 호출. `MX_ADC_Init()`, `MX_USART1_UART_Init()` **호출 안 함** → PA9는 Cube/UART에서 설정되지 않음.
- 그 다음 **PA9만** GPIO 출력(PP)으로 직접 설정:
  - `GPIOA, GPIO_PIN_9`, `GPIO_MODE_OUTPUT_PP`, `GPIO_SPEED_FREQ_LOW`
- `while(1)`: **500ms마다** `HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_9)` + `HAL_Delay(500)`.

### 오실로에서 볼 것

- **PA9**: 500ms 주기 **사각파** (0V ↔ VDD).
- 주기가 500ms로 보이면 **PA9 핀·클럭·GPIO**는 정상.

### 해석

- **파형 있음** → PA9 핀은 살아 있음. 다음 단계로 UART 송신 테스트(MODE 2).
- **파형 없음** → PA9 회로(연결/단선), MCU 클럭, 또는 해당 보드의 PA9 핀 할당(다른 용도로 쓰는지) 점검.

---

## 2) PA9 UART 0x55 반복 송신 테스트

### 설정

```c
#define HPSB_PA9_TEST_MODE  2
```

### 동작

- `MX_GPIO_Init()`, `MX_ADC_Init()`, **`MX_USART1_UART_Init()`** 호출 → **PA9는 `HAL_UART_MspInit()` 안에서 USART1_TX AF로 설정됨.**
- Modbus/DE/문자열 테스트 등 **전부 미실행.**
- `while(1)`: **500ms마다** `HAL_UART_Transmit(&huart1, &0x55, 1, 100)` + `HAL_Delay(500)`.

### 오실로에서 볼 것 (PA9)

- **9600 8N1** 기준, **0x55** 한 바이트:
  - 0x55 = 0101 0101 (LSB first) → 시작비트(0) + 8비트 + 정지비트(1).
  - 한 바이트 길이 ≈ 1/9600 × 10 ≈ **1.04 ms**.
- **500ms마다** 위 1ms 구간 파형이 한 번씩 반복되면 **PA9에서 UART 송신이 나가는 것**.

### 파형 기준 (9600 8N1, 0x55)

- Low 구간(시작비트) → High 1칸 → Low 1칸 → … (0x55 비트 패턴).
- 주기: **500ms** (다음 0x55까지).
- **이렇게 보이면** “MCU PA9에서 UART TX가 실제로 동작한다”고 볼 수 있음.

---

## 3) MX_USART1_UART_Init / MX_GPIO_Init 에서 PA9 설정 (코드 기준)

### PA9가 USART1_TX로 설정되는 곳

- **`Core/Src/stm32f0xx_hal_msp.c`** 의 **`HAL_UART_MspInit(UART_HandleTypeDef* huart)`**:
  - `huart->Instance == USART1` 일 때
  - `GPIO_InitStruct.Pin = RS485_TX_Pin|RS485_RX_Pin` → **PA9, PA10**
  - `GPIO_MODE_AF_PP`, `GPIO_AF1_USART1`
  - `HAL_GPIO_Init(GPIOA, &GPIO_InitStruct)`
- 이 함수는 **`HAL_UART_Init(&huart1)`** 안에서만 호출됨.
- **`MX_USART1_UART_Init()`** 가 **`HAL_UART_Init(&huart1)`** 를 호출하므로, **PA9는 `MX_USART1_UART_Init()` 호출 시에만** USART1_TX AF로 설정됨.

### MX_GPIO_Init() 과 PA9

- **`Core/Src/main.c`** 의 **`MX_GPIO_Init()`**:
  - 설정하는 핀: **RLY_EN01, RLY_EN02, RLY_EN03, RS485_DE_Pin, LED04_Pin** (GPIOA), **ID_BIT1~4** (GPIOB), **LED01~03** (GPIOB).
  - **PA9(RS485_TX_Pin)는 여기서 전혀 건드리지 않음.**

결론: **PA9는 MX_GPIO_Init()에서는 설정되지 않고, MX_USART1_UART_Init() → HAL_UART_MspInit() 에서만 USART1_TX AF로 설정됨.**

---

## 4) 다른 코드가 PA9를 다시 GPIO로 덮어쓰는지

- **검색 결과**: `main.c`, `modbus_slave.c`, `stm32f0xx_hal_msp.c` 등에서 **PA9(GPIO_PIN_9) 또는 RS485_TX_Pin을 `HAL_GPIO_Init`/`WritePin`으로 다시 설정하는 코드 없음.**
- **modbus_slave.c**: DE(PA11), LED만 제어. PA9는 UART로만 사용.
- **정리**: 다른 코드가 PA9를 GPIO로 덮어쓰지는 않음. PA9는 **USART1 초기화 시에만** AF로 설정되고, 이후에는 UART 하드웨어가 PA9를 사용.

---

## 5) 이전 코드에서 PA9가 조용했을 수 있는 이유 (가설)

- **실제로 송신 호출이 안 됐을 수 있음**
  - Modbus/문자열 테스트는 “요청 수신 → 응답” 또는 “1초/2초 주기”라서, **해당 조건이 만족되지 않으면** `HAL_UART_Transmit`이 한 번도 호출되지 않음.
  - 예: 문자열 테스트에서 `HPSB_RS485_TX_STRING_TEST`가 0이면 송신 루프 자체가 안 돌아감.
- **초기화/경로 분리**
  - PA9 테스트 모드 1·2는 **그런 조건 없이** 부팅 후 곧바로 500ms마다 PA9를 토글 또는 0x55 송신하므로, “PA9만 단독으로 동작하는지”를 명확히 볼 수 있음.
- **DE(PA11)와 무관**
  - 이전 코드는 RS485 송신 시 DE HIGH 등 제어가 선행됨. PA9 테스트 모드 2는 **DE 제어 없이** MCU PA9에서만 UART 신호를 내보냄. MAX3485는 보지 않음.

---

## 6) 수정 파일 요약

| 파일 | 내용 |
|------|------|
| `Modbus/Inc/modbus_cfg.h` | `HPSB_PA9_TEST_MODE` 추가 (0/1/2). |
| `Core/Src/main.c` | MODE 1: PA9 GPIO 500ms 토글 전용 루프. MODE 2: PA9 UART 0x55 500ms 송신 전용 루프. MODE 1일 때 ADC/USART1 init 생략. MODE 1/2일 때 Modbus init 및 메인 로직 미실행. |
| `HPSB_PA9_TEST_ANALYSIS.md` | 본 문서 (PA9 설정·덮어쓰기·파형 기준·이전 코드 가설). |

---

## 7) 사용 순서 제안

1. **`HPSB_PA9_TEST_MODE = 1`** 로 빌드·다운로드 → PA9에 **500ms 사각파** 나오는지 확인.  
   - 나오면: PA9 핀·클럭 정상.  
   - 안 나오면: 회로/핀/클럭 점검.

2. **`HPSB_PA9_TEST_MODE = 2`** 로 빌드·다운로드 → PA9에 **500ms마다 0x55 UART 파형(약 1ms)** 나오는지 확인.  
   - 나오면: MCU PA9 UART TX 동작 확인됨.  
   - 안 나오면: USART1 클럭/초기화/핀 매핑 점검.

3. 둘 다 확인된 뒤에 **`HPSB_PA9_TEST_MODE = 0`** 으로 되돌리고 Modbus/RS485 경로를 다시 검토.
