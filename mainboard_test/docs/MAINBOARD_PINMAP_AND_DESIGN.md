# Mainboard 핀맵·모듈·초기화·테스트 정리

**기준**: [MAINBOARD_HW_REFERENCE.md](MAINBOARD_HW_REFERENCE.md) 공식 하드웨어 정의.

---

## 1. Mainboard 전체 핀맵 표

| 핀명 | 기능명 | Peripheral | 방향 | 초기상태 | 비고 |
|------|--------|------------|------|----------|------|
| PA2 | USART2_TX | USART2 | AF | - | Subboard RS485 TX |
| PA3 | USART2_RX | USART2 | AF | - | Subboard RS485 RX |
| PA8 | EEP_I2C_SCL | I2C3 | AF | - | EEPROM (AT24C02C) SCL |
| PA9 | USART1_TX | USART1 | AF | - | PC RS485 TX |
| PA10 | USART1_RX | USART1 | AF | - | PC RS485 RX |
| PB1 | RS485_DE_PC | GPIO | Output | **LOW (RX)** | PC용 DE/RE, 초기 RX 모드 |
| PB6 | SEN1_I2C_SCL | I2C1 | AF | - | SHTC3 SCL |
| PB7 | SEN1_I2C_SDA | I2C1 | AF | - | SHTC3 SDA |
| PB8 | LED01 | GPIO | Output | **HIGH (OFF)** | Low active |
| PB9 | LED02 | GPIO | Output | **HIGH (OFF)** | Low active |
| PB10 | LED03 | GPIO | Output | **HIGH (OFF)** | Low active |
| PB11 | LED04 | GPIO | Output | **HIGH (OFF)** | Low active |
| PB12 | RS485_DE_SUB | GPIO | Output | **LOW (RX)** | Subboard용 DE/RE, 초기 RX 모드 |
| PC0 | PC_RESET_EN | GPIO | Output | LOW | PC 보조 |
| PC1 | PC_ON_EN | GPIO | Output | LOW | PC 보조 |
| PC2 | PC_LED_IN | GPIO | Input | - | PC 보조, 정논리 |
| PC8 | - | - | - | - | (I2C3 SCL은 PA8) |
| PC9 | EEP_I2C_SDA | I2C3 | AF | - | EEPROM SDA |
| PE0 | RELAY1_EN | GPIO | Output | LOW | TBD62003 구동 |
| PE1 | RELAY2_EN | GPIO | Output | LOW | TBD62003 구동 |
| PE2 | RELAY3_EN | GPIO | Output | LOW | TBD62003 구동 |
| PE3 | RELAY4_EN | GPIO | Output | LOW | TBD62003 구동 |
| PE4 | DI_01 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE5 | DI_02 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE6 | DI_03 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE7 | DI_04 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE8 | DI_05 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE9 | DI_06 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE10 | DI_07 | GPIO | Input | - | 24V 옵토 절연 입력 |
| PE11 | DI_08 | GPIO | Input | - | 24V 옵토 절연 입력 |

---

## 2. 펌웨어 모듈 분리안

| 계층 | 모듈 | 역할 |
|------|------|------|
| BSP | **bsp_gpio** | GPIO 초기화, DI/DO/PC I/O 핀 정의 |
| BSP | **bsp_led** | LED01~04 제어 (Low active: on=LOW, off=HIGH) |
| BSP | **bsp_uart** | USART1/USART2 HAL 초기화 (PC/Subboard 구분) |
| BSP | **rs485_pc** | PC용 RS485: USART1 + PB1 DE/RE 제어 (TX 전 DE High, 송신 후 RX 복귀) |
| BSP | **rs485_sub** | Subboard용 RS485: USART2 + PB12 DE/RE 제어 |
| 드라이버 | **shtc3** | I2C1 전용, SHTC3 온습도 읽기 |
| 드라이버 | **at24c02** | I2C3 전용, EEPROM 읽기/쓰기 |
| 프로토콜 | **upstream_slave_uart1** | 상위 Modbus RTU Slave (Slave ID 09), `Application/Src/upstream_slave_uart1.c` |
| 프로토콜 | **modbus_master** | 하위 Modbus RTU Master, `Modbus/Src/modbus_master.c` (HPSB/LPSB 01/02/04/08 폴링) |
| 앱 | **app_main** | 초기화 순서 제어, 메인 루프, 스케줄러/태스크 호출 |

---

## 3. 부팅 직후 초기화 순서

| 순서 | 항목 | 설명 |
|------|------|------|
| 1 | HAL_Init() | HAL 초기화 |
| 2 | SystemClock_Config() | HSE 8MHz → 84MHz |
| 3 | GPIO 초기화 | LED(High=OFF), DE(Low=RX), RELAY(Low), PC I/O, DI 입력 |
| 4 | I2C1 초기화 | SHTC3용 (PB6/PB7) |
| 5 | I2C3 초기화 | EEPROM용 (PA8/PC9) |
| 6 | USART1 초기화 | PC 통신, 9600 8N1 등 |
| 7 | USART2 초기화 | Subboard 통신 |
| 8 | RS485 PC/Sub DE | PB1, PB12 **LOW(RX)** 로 고정 확인 |
| 9 | EEPROM 읽기 | at24c02 초기화·필요 시 설정 로드 |
| 10 | SHTC3 초기화 | shtc3 초기화·첫 측정 가능 확인 |
| 11 | Upstream Slave 초기화 | `UpstreamSlaveUart1_Init()` (PC용 수신 대기) |
| 12 | Modbus Master 초기화 | `ModbusMaster_Init()` (Subboard 폴링용) |
| 13 | 메인 루프 시작 | 주기 태스크·폴링·상태 집계 |

---

## 4. 납품 전 테스트 체크리스트

| 번호 | 항목 | 내용 | Pass/Fail |
|------|------|------|-----------|
| 1 | **LED** | LED01~04 각각 on/off, Low active 동작 확인 | |
| 2 | **EEPROM** | AT24C02C (I2C3) read/write, 주소/데이터 검증 | |
| 3 | **SHTC3** | I2C1로 온도·습도 읽기, 값 범위 확인 | |
| 4 | **PC RS485** | USART1+PB1, Slave ID 09, FC03/06 요청에 응답 | |
| 5 | **Subboard 폴링** | USART2+PB12, HPSB(01)/LPSB(02/04/08) 폴링 정상 | |
| 6 | **Relay** | RELAY1~4_EN 구동, TBD62003·릴레이 동작 확인 | |
| 7 | **DI** | DI_01~08, 24V 입력 시 옵토 경유 상태 읽기 확인 | |
| 8 | **PC 보조** | PC_ON_EN, PC_RESET_EN 출력, PC_LED_IN 입력(정논리) | |

---

*문서 버전: 1.0 | MAINBOARD_HW_REFERENCE.md 기준.*
