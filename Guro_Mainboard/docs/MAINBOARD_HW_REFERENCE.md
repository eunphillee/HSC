# Mainboard 공식 하드웨어 기준 (Hardware Reference)

**이 문서는 프로젝트의 공식 Mainboard 하드웨어 기준이다.  
펌웨어/문서/코드/테스트 로직은 이 정의를 절대 우선으로 따른다.**

---

## 0. 프로젝트 개요

| 구성 | 수량 | 비고 |
|------|------|------|
| PC | 1대 | 상위 |
| Mainboard | 1대 | 중앙 제어·게이트웨이 |
| HPSB | 1대 | Subboard |
| LPSB | 3대 | Subboard |

- **Mainboard**: PC와 상위 통신, HPSB/LPSB와 하위 통신하는 게이트웨이.

---

## 1. 시스템 통신 구조

### 1.1 상위 통신 (PC ↔ Mainboard)

| 항목 | 내용 |
|------|------|
| 방식 | RS485 Modbus RTU |
| Mainboard 역할 | **Slave** |
| Mainboard Slave ID | **09 고정** |
| UART | **USART1** |
| TX | PA9 |
| RX | PA10 |
| RS485 DE/RE | **PB1** |

→ PC는 Mainboard와 RS485 Modbus RTU로 통신하며, Mainboard는 **Slave ID 09**로 응답한다.

### 1.2 하위 통신 (Mainboard ↔ Subboard)

| 항목 | 내용 |
|------|------|
| 방식 | RS485 Modbus RTU Multi-drop |
| Mainboard 역할 | **Master** |
| UART | **USART2** |
| TX | PA2 |
| RX | PA3 |
| RS485 DE/RE | **PB12** |

| Subboard | Slave ID |
|----------|-----------|
| HPSB  | 01 |
| LPSB1 | 02 |
| LPSB2 | 04 |
| LPSB3 | 08 |

→ Mainboard는 상위 Slave, 하위 Master를 동시에 수행한다.

---

## 2. I2C 구성

| 장치 | 부품 | I2C | SCL | SDA | 비고 |
|------|------|-----|-----|-----|------|
| 온습도 센서 | SHTC3 | **I2C1** | PB6 | PB7 | 4.7kΩ Pull-up, 3.3V |
| EEPROM | AT24C02C | **I2C3** | PA8 | PC9 | Pull-up, 별도 버스 |

- **SHTC3**: I2C1 전용 (PB6/PB7).
- **AT24C02C**: I2C3 전용 (PA8/PC9). 센서와 **같은 버스 아님**.

---

## 3. 전원 구조

- 외부 입력: **24V** (보호 회로, Fuse, 역극성/보호 다이오드, TVS 포함).
- **24V → LM2596 → 5V**
- **5V → LM1117-3.3 → 3.3V**
- 로직 전압: **3.3V**. 릴레이/드라이버는 5V·24V 도메인과 분리.

---

## 4. MCU 및 클럭

| 항목 | 내용 |
|------|------|
| MCU | STM32F205VCTx |
| HSE | 8MHz 외부 크리스탈 |
| LSE | 32.768kHz 외부 크리스탈 |
| 시스템 클럭 | 84MHz |
| 디버그 | SWD |
| NRST | 포함 |
| BOOT0 | 기본 Low |

---

## 5. RS485 하드웨어

- **채널 #1 (PC용)**: MAX3485, USART1, PA9(TX)/PA10(RX), **DE/RE=PB1**. Slave ID 09.
- **채널 #2 (Subboard용)**: MAX3485, USART2, PA2(TX)/PA3(RX), **DE/RE=PB12**. Master.
- Bias 저항, 120Ω 종단저항 점퍼, TVS, A/B/GND.
- **Modbus RTU CRC: Low byte first.**
- **PB1, PB12**: 기본·초기 상태는 **RX 모드(LOW)** 로 설계/초기화.

---

## 6. 디지털 입력 (DI)

- 24V 입력, 옵토커플러 절연.
- MCU는 절연된 Low voltage 신호만 읽음 (GPIO 입력으로 읽음).

---

## 7. 디지털 출력 (DO)

- 릴레이 4채널: **RELAY1_EN ~ RELAY4_EN**.
- 드라이버: TBD62003 계열 → 릴레이 구동 → 외부 커넥터.
- “GPIO 직접 구동”이 아니라 “드라이버를 통한 릴레이 활성화”로 이해.

---

## 8. PC 보조 I/O

- **PC_RESET_EN**, **PC_ON_EN**, **PC_LED_IN**
- **PC_LED_IN**: SN74AHC1G14 인버터 포함이지만, **회로 전체 경로 기준 최종 의미는 정논리**.
  - High 의미 입력 → High로 해석.
  - Low 의미 입력 → Low로 해석.

---

## 9. 상태 LED (Low active)

| 핀 | 이름 | 동작 |
|----|------|------|
| PB8  | LED01 | **Low active**: LOW=ON, HIGH=OFF |
| PB9  | LED02 | 동일 |
| PB10 | LED03 | 동일 |
| PB11 | LED04 | 동일 |

- **led_on(LED01)** → PB8 **LOW**
- **led_off(LED01)** → PB8 **HIGH**
- 초기 상태: LED OFF → HIGH 출력.

---

## 10. Cursor 작업 시 절대 규칙

1. 위 핀맵·Peripheral 구성을 임의로 바꾸지 말 것.
2. SHTC3와 EEPROM의 I2C 버스를 혼동하지 말 것.
3. **USART1 = PC 통신 전용.**
4. **USART2 = Subboard 통신 전용.**
5. Mainboard 상위 ID는 **09 고정**.
6. 하위 Subboard ID는 **01 / 02 / 04 / 08** 체계 유지.
7. RS485 방향 제어 **PB1, PB12** 초기 상태를 신중히 다룰 것.
8. 문서/코드/주석/테스트 로직은 이 정의와 일치하게 작성할 것.
