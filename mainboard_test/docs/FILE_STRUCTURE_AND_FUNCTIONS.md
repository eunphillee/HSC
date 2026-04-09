# Mainboard 파일 구조 초안 및 핵심 함수 목록

**기준**: [MAINBOARD_HW_REFERENCE.md](MAINBOARD_HW_REFERENCE.md), [MAINBOARD_PINMAP_AND_DESIGN.md](MAINBOARD_PINMAP_AND_DESIGN.md).

---

## 1. 디렉터리/파일 구조 초안

```
Guro_Mainboard/
├── Core/
│   ├── Inc/main.h              # HAL/Cube 핀 정의 (PA9/10, PA2/3, PB1/12 등)
│   ├── Src/main.c              # 초기화 순서 + 메인 루프만 (비대해지지 않게)
│   ├── Src/stm32f2xx_hal_msp.c
│   └── Src/stm32f2xx_it.c
├── BSP/
│   ├── Inc/
│   │   ├── bsp_gpio.h          # GPIO/DI/DO/PC I/O 정의
│   │   ├── bsp_led.h           # LED Low active 제어
│   │   ├── bsp_rs485_pc.h      # PC용 RS485 (USART1 + PB1)
│   │   └── bsp_rs485_sub.h     # Subboard용 RS485 (USART2 + PB12)
│   └── Src/
│       ├── bsp_gpio.c
│       ├── bsp_led.c
│       ├── bsp_rs485_pc.c
│       └── bsp_rs485_sub.c
├── Drivers/
│   └── Sensors/
│       ├── shtc3.h
│       ├── shtc3.c             # I2C1 전용 SHTC3
│       ├── at24c02.h
│       └── at24c02.c           # I2C3 전용 AT24C02C
├── Application/
│   ├── Inc/upstream_slave_uart1.h   # 상위 Slave (PC, ID 09)
│   └── Src/upstream_slave_uart1.c   # USART1 + PB1
├── Modbus/
│   ├── Inc/
│   │   ├── modbus_rtu.h
│   │   └── modbus_master.h         # 하위 Master
│   └── Src/
│       ├── modbus_rtu.c
│       └── modbus_master.c         # HPSB/LPSB 01/02/04/08 폴링
└── docs/
    ├── MAINBOARD_HW_REFERENCE.md
    ├── MAINBOARD_PINMAP_AND_DESIGN.md
    └── FILE_STRUCTURE_AND_FUNCTIONS.md
```

---

## 2. 파일별 핵심 함수 목록

### 2.1 bsp_gpio.h / bsp_gpio.c

| 함수 | 설명 |
|------|------|
| `void BSP_GPIO_Init(void)` | DI(PE4~PE11), DO(RELAY PE0~PE3), PC I/O(PC0~PC2) 초기화 |
| `uint8_t BSP_ReadDI(uint8_t ch)` | ch 0~7 → DI_01~DI_08 읽기 (1/0) |
| `void BSP_ReadAllDI(uint8_t *bits)` | 8ch 한 번에 bits[8]에 저장 |
| `void BSP_WriteRelay(uint8_t ch, uint8_t on)` | ch 0~3 → RELAY1~4, on=1 구동 |
| `void BSP_WritePC_ON_EN(uint8_t level)` | PC_ON_EN (PC1) 출력 |
| `void BSP_WritePC_RESET_EN(uint8_t level)` | PC_RESET_EN (PC0) 출력 |
| `uint8_t BSP_ReadPC_LED_IN(void)` | PC_LED_IN (PC2) 정논리 읽기 (1=High 의미) |

### 2.2 bsp_led.h / bsp_led.c (Low active)

| 함수 | 설명 |
|------|------|
| `void BSP_LED_Init(void)` | PB8~PB11 Output, 초기 **HIGH (LED OFF)** |
| `void BSP_LED_On(uint8_t led_id)` | led_id 0~3 → PB8~PB11 **LOW** (LED ON) |
| `void BSP_LED_Off(uint8_t led_id)` | led_id 0~3 → PB8~PB11 **HIGH** (LED OFF) |
| `void BSP_LED_Toggle(uint8_t led_id)` | 토글 |

- **상수**: `BSP_LED_01`~`BSP_LED_04` (0~3). 주석에 "Low active" 명시.

### 2.3 bsp_rs485_pc.h / bsp_rs485_pc.c

| 함수 | 설명 |
|------|------|
| `void BSP_RS485_PC_Init(void)` | USART1 초기화, **PB1 LOW (RX)** 설정 |
| `void BSP_RS485_PC_SetDE_TX(void)` | 송신 전: PB1 High (TX) |
| `void BSP_RS485_PC_SetDE_RX(void)` | 송신 후: PB1 Low (RX) |
| `HAL_StatusTypeDef BSP_RS485_PC_Transmit(uint8_t *buf, uint16_t len)` | SetDE_TX → UART 전송 → SetDE_RX (필요 시 2ms 가드) |
| `UART_HandleTypeDef* BSP_RS485_PC_GetUartHandle(void)` | Slave/수신 쪽에서 사용 |

### 2.4 bsp_rs485_sub.h / bsp_rs485_sub.c

| 함수 | 설명 |
|------|------|
| `void BSP_RS485_Sub_Init(void)` | USART2 초기화, **PB12 LOW (RX)** 설정 |
| `void BSP_RS485_Sub_SetDE_TX(void)` | 송신 전: PB12 High (TX) |
| `void BSP_RS485_Sub_SetDE_RX(void)` | 송신 후: PB12 Low (RX) |
| `HAL_StatusTypeDef BSP_RS485_Sub_Transmit(uint8_t *buf, uint16_t len)` | SetDE_TX → 전송 → SetDE_RX |
| `UART_HandleTypeDef* BSP_RS485_Sub_GetUartHandle(void)` | Master 송수신에서 사용 |

### 2.5 shtc3.h / shtc3.c (I2C1 전용)

| 함수 | 설명 |
|------|------|
| `void SHTC3_Init(I2C_HandleTypeDef *hi2c)` | hi2c = I2C1 핸들, 초기화 |
| `int8_t SHTC3_Measure(float *temp_c, float *rh_pct)` | 온도(°C), 습도(%) 반환, 실패 시 -1 |

### 2.6 at24c02.h / at24c02.c (I2C3 전용)

| 함수 | 설명 |
|------|------|
| `void AT24C02_Init(I2C_HandleTypeDef *hi2c)` | hi2c = I2C3 핸들 |
| `HAL_StatusTypeDef AT24C02_Read(uint16_t addr, uint8_t *buf, uint16_t len)` | addr 0~255, 1바이트 주소 |
| `HAL_StatusTypeDef AT24C02_Write(uint16_t addr, uint8_t *buf, uint16_t len)` | 페이지 쓰기 주의 |

### 2.7 upstream_slave_uart1 (상위 Slave, ID 09) — Application/Src/upstream_slave_uart1.c

| 함수 | 설명 |
|------|------|
| `void UpstreamSlaveUart1_Init(void)` | 수신 버퍼, 상태 초기화, USART1 수신 시작, DE=RX |
| `void UpstreamSlaveUart1_Poll(const aggregated_status_t *agg)` | 수신 프레임 처리, FC02/03/05/06/15 응답, RS485 PC(USART1+PB1)로 전송 |

### 2.8 modbus_master (하위 Master) — Modbus/Src/modbus_master.c

| 함수 | 설명 |
|------|------|
| `void ModbusMaster_Init(void)` | 폴링 상태, USART2 초기화 |
| `void ModbusMaster_Poll(void)` | HPSB(01), LPSB(02/04/08) 순차 폴링, USART2+PB12 사용 |

### 2.9 app_main / main.c

| 항목 | 설명 |
|------|------|
| main.c 초기화 | BSP/GPIO/LED, UpstreamSlaveUart1_Init, ModbusMaster_Init 등 호출 |
| 메인 루프 | UpstreamSlaveUart1_Poll, ModbusMaster_Poll, 스케줄러·태스크 |

---

**정리**: Mainboard에는 `modbus_slave.c` 파일명은 없음. 상위 Slave = `Application/Src/upstream_slave_uart1.c`, 하위 Master = `Modbus/Src/modbus_master.c`.
