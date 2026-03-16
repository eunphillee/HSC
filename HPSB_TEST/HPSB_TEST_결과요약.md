# HPSB_TEST — 하드웨어 검증 프로젝트 결과 요약

HPSB 보드의 **RS485 송신 / 릴레이 3채널 / 전류센서 ADC** 를 Modbus 없이 단순 검증하는 테스트 프로젝트입니다.

---

## 1. CubeMX 설정 요약

| 항목 | 설정 |
|------|------|
| **MCU** | STM32F030K6T6 (LQFP32) |
| **전원** | 3.3V |
| **클럭** | HSI 8MHz (PLL/HSE 미사용, 코드에서 고정) |

### 핀맵

| 핀 | 기능 | 모드 |
|----|------|------|
| PA9  | USART1_TX → MAX3485 DI | AF Push-Pull |
| PA10 | USART1_RX ← MAX3485 RO | AF Push-Pull |
| PA11 | RS485_DE → MAX3485 DE/RE | GPIO Output |
| PA0  | RLY_EN01 | GPIO Output |
| PA1  | RLY_EN02 | GPIO Output |
| PA2  | RLY_EN03 | GPIO Output |
| PA3  | TC_ADC01 (전류센서) | ADC1_IN3 |
| PA4  | TC_ADC02 | ADC1_IN4 |
| PA5  | TC_ADC03 | ADC1_IN5 |
| PB0, PB1, PB3, PB4 | ID_BIT1~4 | GPIO Input |
| PB5, PB6, PB7 | LED01, LED02, LED03 | GPIO Output |
| PA15 | LED04 | GPIO Output |

### USART1

- Baud: **9600**
- Word: **8 bit**, Parity: **None**, Stop: **1 bit**
- Mode: TX + RX

### ADC1

- Resolution: **12bit**
- Channels: **IN3, IN4, IN5** (PA3, PA4, PA5)
- Trigger: Software
- Sampling: 1.5 cycles

---

## 2. main.c 구조 (전체 코드는 `Core/Src/main.c` 참조)

- **초기화 순서**: `HAL_Init()` → `SystemClock_Config()` → `MX_GPIO_Init_AllInputSafe()` (전류 급상승 방지) → `MX_GPIO_Init()` → `MX_ADC_Init()` → `MX_USART1_UART_Init()` → RS485 DE=LOW, 시퀀스/phase 초기화.
- **메인 루프**: 1초마다 `s_relay_phase` 0→1→2→3→0 순환 → `Relay_UpdateFromPhase()` → `ADC_ReadThreeChannels()` → LED01 토글, LED02/03/04는 릴레이 phase에 따라 표시 → `snprintf`로 한 줄 문자열 생성 → `RS485_Send()`.
- **Modbus 미사용**: 프로젝트에 Modbus/Application/IO 소스는 빌드에서 제외.

---

## 3. RS485_Send 함수

```c
/**
 * @brief RS485 반이중 송신: DE HIGH → UART TX → TC 대기 → DE LOW
 */
static void RS485_Send(const uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return;
  /* 1. PA11 (DE) HIGH → MAX3485 송신 모드 */
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
  /* 2. 짧은 delay */
  for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }
  /* 3. HAL_UART_Transmit */
  HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 100);
  /* 4. Transmission Complete (TC) wait */
  while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET) { }
  /* 5. PA11 (DE) LOW → 수신 모드 복귀 */
  HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
}
```

---

## 4. ADC 읽기 함수 (전류센서 3채널)

```c
/**
 * @brief ADC 3채널(PA3=TC_ADC01, PA4=TC_ADC02, PA5=TC_ADC03) 순차 읽기.
 *        MX_ADC_Init에서 CH3, CH4, CH5가 시퀀스로 등록된 상태에서
 *        한 번 Start 후 EOC마다 GetValue로 A1, A2, A3에 해당하는 값 반환.
 */
static void ADC_ReadThreeChannels(uint16_t *adc_val)
{
  if (adc_val == NULL) return;
  HAL_ADC_Start(&hadc);
  HAL_ADC_PollForConversion(&hadc, 10);
  adc_val[0] = (uint16_t)HAL_ADC_GetValue(&hadc);  /* A1: PA3 */
  HAL_ADC_PollForConversion(&hadc, 10);
  adc_val[1] = (uint16_t)HAL_ADC_GetValue(&hadc);  /* A2: PA4 */
  HAL_ADC_PollForConversion(&hadc, 10);
  adc_val[2] = (uint16_t)HAL_ADC_GetValue(&hadc);  /* A3: PA5 */
}
```

---

## 5. 릴레이 상태 머신 코드

```c
/* phase 0: R1 ON,  phase 1: R2 ON,  phase 2: R3 ON,  phase 3: 전부 OFF */
static void Relay_UpdateFromPhase(void)
{
  HAL_GPIO_WritePin(RLY_EN01_GPIO_Port, RLY_EN01_Pin, (s_relay_phase == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RLY_EN02_GPIO_Port, RLY_EN02_Pin, (s_relay_phase == 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RLY_EN03_GPIO_Port, RLY_EN03_Pin, (s_relay_phase == 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
```

- 메인 루프에서 1초마다 `s_relay_phase = (s_relay_phase + 1) % 4` 로 0→1→2→3→0 반복 후 `Relay_UpdateFromPhase()` 호출.

| phase | RLY_EN01 | RLY_EN02 | RLY_EN03 |
|-------|----------|----------|----------|
| 0     | ON       | OFF      | OFF      |
| 1     | OFF      | ON       | OFF      |
| 2     | OFF      | OFF      | ON       |
| 3     | OFF      | OFF      | OFF      |

---

## 6. PC에서 보일 예상 출력 예시

시리얼 터미널(9600 8N1, RS485 변환기 연결)에서 1초마다 한 줄씩 수신되는 형식:

```
HPSB_TEST,SEQ=1,R1=1,R2=0,R3=0,A1=1234,A2=1201,A3=1198
HPSB_TEST,SEQ=2,R1=0,R2=1,R3=0,A1=1220,A2=1198,A3=1200
HPSB_TEST,SEQ=3,R1=0,R2=0,R3=1,A1=1215,A2=1205,A3=1189
HPSB_TEST,SEQ=4,R1=0,R2=0,R3=0,A1=1210,A2=1200,A3=1190
HPSB_TEST,SEQ=5,R1=1,R2=0,R3=0,A1=1234,A2=1201,A3=1198
...
```

- **SEQ**: 전송 시퀀스 번호 (1부터 증가).
- **R1, R2, R3**: 해당 구간 릴레이 1/2/3 ON(1) 또는 OFF(0).
- **A1, A2, A3**: 전류센서 ADC 3채널 12bit 값 (0~4095, PA3/PA4/PA5).

---

## 프로젝트 사용 방법

- **기존 Guro_HPSB 복제** 후 프로젝트 이름을 **HPSB_TEST**로 변경하고, `Core/Inc/main.h`, `Core/Src/main.c`, `Core/Src/stm32f0xx_hal_msp.c` 를 이 HPSB_TEST 버전으로 교체. Application / Modbus / IO 소스는 빌드에서 제외.
- **전체 하드웨어 검증**이 기본 동작 (`HPSB_TEST_MINIMAL_SAFE_MODE 0`). 전원+LED+스위치만 연결해 둔 보드에서 전류/발열을 보고 싶으면 `main.c`에서 `HPSB_TEST_MINIMAL_SAFE_MODE` 를 `1`로 변경 후 빌드.
