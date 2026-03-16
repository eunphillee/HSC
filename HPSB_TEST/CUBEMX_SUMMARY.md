# HPSB_TEST — CubeMX 설정 요약

- **MCU**: STM32F030K6T6
- **목적**: HPSB 보드 하드웨어 독립 검증 (Modbus 미사용, RS485로 상태 문자열만 전송)

## 클럭 (MCU 보호: HSI만 사용)

- **SYSCLK**: HSI 8 MHz **직접** (PLL·HSE 미사용 — 크리스탈/부품 불량 시에도 안전)
- **HCLK / PCLK1**: 8 MHz
- **ADC**: HSI14 (HAL에서 자동 사용)
- **Flash latency**: 0  
- CubeMX에서 Code 재생성 시 `SystemClock_Config()`가 덮어쓰이지 않도록 하거나, 재생성 후 다시 HSI 전용으로 수정할 것.

## 핀맵 (GPIO / AF)

| 핀   | 기능        | 모드        | 비고                    |
|------|-------------|-------------|-------------------------|
| PA9  | USART1_TX   | AF Push-Pull| RS485 TX                |
| PA10 | USART1_RX   | AF Push-Pull| RS485 RX                |
| PA11 | GPIO_Output | Output PP   | RS485 DE (반이중 제어)  |
| PA0  | GPIO_Output | Output PP   | RLY_EN01                |
| PA1  | GPIO_Output | Output PP   | RLY_EN02                |
| PA2  | GPIO_Output | Output PP   | RLY_EN03                |
| PA3  | ADC1_IN3    | Analog      | TC_ADC01                |
| PA4  | ADC1_IN4    | Analog      | TC_ADC02                |
| PA5  | ADC1_IN5    | Analog      | TC_ADC03                |
| PB0  | GPIO_Input  | Input       | ID_BIT4                 |
| PB1  | GPIO_Input  | Input       | ID_BIT2                 |
| PB3  | GPIO_Input  | Input       | ID_BIT3                 |
| PB4  | GPIO_Input  | Input       | ID_BIT1                 |
| PB5  | GPIO_Output | Output PP   | LED01 (로우 액티브)      |
| PB6  | GPIO_Output | Output PP   | LED02                   |
| PB7  | GPIO_Output | Output PP   | LED03                   |
| PA15 | GPIO_Output | Output PP   | LED04                   |

## USART1 (RS485)

- **Baud rate**: 9600
- **Word length**: 8 bits
- **Parity**: None
- **Stop bits**: 1
- **Mode**: TX + RX
- **Flow control**: None
- **DE 제어**: PA11은 CubeMX에서 GPIO로 설정하고, 코드에서 송신 시에만 HIGH로 제어

## ADC1

- **Resolution**: 12 bit
- **Data align**: Right
- **Channels**: IN3 (PA3), IN4 (PA4), IN5 (PA5)
- **Trigger**: Software start
- **Sampling time**: 1.5 cycles (또는 동일 계열 권장값)

## HAL 모듈

- GPIO, RCC, ADC, UART, CORTEX, PWR, FLASH, EXTI (필요 시)
- DMA 사용 안 함 (폴링 전송/수신)

## 프로젝트 생성 시 (CubeIDE)

1. **New Project** → MCU Selector → **STM32F030K6Tx** 선택 후 프로젝트 이름 **HPSB_TEST**.
2. **CubeMX**에서 위 핀맵대로 PA9/PA10=USART1, PA3/4/5=ADC, PA11/PA0~2/PA15/PB0~7 설정.
3. **USART1**: 9600 8N1, Mode TX+RX.
4. **ADC1**: Channel 3, 4, 5 활성화, Software trigger.
5. **Clock**: HSI만 사용, SYSCLK = HSI.
6. **Generate Code** 후, 제공된 `main.c`, `main.h`, `stm32f0xx_hal_msp.c`로 교체.

또는 **Guro_HPSB 프로젝트를 복제**한 뒤, `Core/Src/main.c`, `Core/Inc/main.h`, `Core/Src/stm32f0xx_hal_msp.c`만 HPSB_TEST용으로 교체하고, Application/Modbus/IO는 빌드에서 제외해도 됩니다.
