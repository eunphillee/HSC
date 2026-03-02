# PC 툴 통신 (연결 확인 / Modbus)

## 정리: 우리가 정한 통신 구조
- **PC = 마스터**, **메인보드 = 슬레이브 ID 9**
- **메인보드 UART1**(PA9/PA10, DE=PB1) **RS485**로 통신
- **연결 살아있는지 확인**: 메인보드가 **500ms마다 0xAA** 전송 → PC 툴이 수신해서 **현시**

## 연결 확인(단순) — 0xAA 500ms 수신 현시
- **메인보드**: `app_config.h` 에서 **ENABLE_PC_TEST_AA_STREAM=1** 로 빌드하면 500ms마다 0xAA만 송신 (기본값 1로 설정해 둠)
- **PC 툴**: Connect 후 시리얼 버퍼에서 raw 수신 → 로그에 `RX from board: 0xAA` 표시
- 이 모드에서는 보드가 Modbus 요청에 응답하지 않음 (0xAA만 보냄)

## Modbus (Read DI, Relay 제어) 사용할 때
- **ENABLE_PC_TEST_AA_STREAM=0**, **USE_PC_TEST_UART1_SLAVE=1** 로 빌드
- PC 툴 **Slave ID = 9** 로 설정 후 Connect, **Read DI** 등 사용
- `app_config.h` 확인:
  - **ENABLE_PC_TEST_AA_STREAM** = **0** (1이면 보드는 0xAA만 보내고 Modbus에 응답 안 함)
  - **USE_PC_TEST_UART1_SLAVE** = **1** (0이면 UART1 슬레이브 꺼짐)
- Connect 후 **Read DI** 버튼을 눌러야 응답이 옴 (보드는 요청 받을 때만 응답)
- 로그에 `TX FC03 addr=2100 cnt=2` → `RX OK` 나오면 정상

## No response / 동글 LED 안 깜빡일 때 (트러블슈팅)

**추가 확인**
- RS485 선: 보드 A↔동글 A, B↔B, GND 공통
- DE 극성: `RS485_DE_ACTIVE_HIGH` 를 0으로 바꿔서 재빌드 후 시험

---

## 증상 A: USB-RS485 동글 수신 LED가 전혀 안 깜빡임

### 확인 순서

1. **보드가 실제로 송신하는지**
   - **ENABLE_PC_TEST_AA_STREAM=1** 빌드: 보드는 500ms마다 0xAA를 보냄.  
     → 부팅 시 **LED2가 3번 빠르게 점멸**하면 이 모드가 올라간 것.  
     → 0xAA 송신 성공 시 **LED3가 약 20ms** 깜빡이고, 송신 실패 시 **LED4가 200ms** 켜짐.  
     → LED4만 계속 켜져 있으면 UART 송신 실패(BUSY/TIMEOUT 등).
   - **ENABLE_PC_TEST_AA_STREAM=0** 빌드: 보드는 **Modbus 요청을 받았을 때만** 응답 전송.  
     → PC 툴에서 **Read DI** 버튼을 눌렀을 때만 보드가 응답을 보냄.  
     → **Read DI를 눌러보고** 그때 동글 LED가 깜빡이면 회선/동글은 정상이고, 평소에는 보드가 보내는 게 없어서 LED가 안 깜빡이는 것.

2. **배선**
   - 보드 RS485 A ↔ 동글 A, B ↔ B, **GND 공통**.
   - A/B가 바뀌어 있으면 수신이 안 될 수 있음 (한번 바꿔서 시험).

3. **DE(Driver Enable) 극성**
   - 보드 송신 시 **PB1(DE)=H** 이어야 트랜시버가 라인을 구동함.  
   - `app_config.h` 의 **RS485_DE_ACTIVE_HIGH** 가 하드웨어와 맞지 않으면 송신이 안 나감.  
   - 현재 **1**인데 동글 LED가 절대 안 깜빡이면 **0**으로 바꿔서 재빌드 후 다시 시험.

4. **같은 포트/케이블**
   - 어제 됐을 때와 **같은 COM 포트**(예: `/dev/cu.usbserial-1120`), 같은 케이블 사용 중인지 확인.

---

## 증상 B: Modbus 응답 없음 (No response received)
- PC 툴 로그: `No response received, expected at least 2 bytes (0 received)`, `RX ERR`
- Read DI / Relay 체크해도 응답 없음

## 원인 1: 0xAA 전용 모드로 빌드됨
**ENABLE_PC_TEST_AA_STREAM=1** 이면 메인보드가 **Modbus 슬레이브를 동작하지 않고** 500ms마다 0xAA만 송신합니다.  
→ PC 툴의 FC03/FC06 요청에 **절대 응답하지 않습니다.**

**조치**
- `Application/Inc/app_config.h` 에서 **ENABLE_PC_TEST_AA_STREAM** 을 **0**으로 설정 후 **재빌드·다운로드**.

## 원인 2: UART1 슬레이브 비활성화
**USE_PC_TEST_UART1_SLAVE=0** 이면 UART1에서 Modbus 슬레이브가 동작하지 않습니다.

**조치**
- `app_config.h` 에서 **USE_PC_TEST_UART1_SLAVE** 를 **1**로 설정 후 재빌드.

## PC 툴과 정상 통신하려면 (Read DI, Relay 제어)
- **ENABLE_PC_TEST_AA_STREAM = 0**
- **USE_PC_TEST_UART1_SLAVE = 1**
- 보드와 PC를 UART1 RS485(PA9/PA10, DE=PB1)로 연결, 9600 8N1, Slave ID 9

## 0xAA 수신만 확인하고 싶을 때
- **ENABLE_PC_TEST_AA_STREAM=1** 빌드 사용 시: 보드는 0xAA만 보내고 Modbus에는 응답하지 않음. PC 툴 로그에 raw 수신으로 `RX from board: 0xAA` 표시됨(동일 포트 사용 시).
- Modbus(Read DI, Relay)와 0xAA를 함께 쓰려면: **ENABLE_PC_TEST_AA_STREAM=0**, **BOARD_TX_0XAA_ENABLE=1** 로 빌드하면 유휴 시 0xAA도 송신하고 Modbus에도 응답함.

## 메인보드가 "계속" 0xAA를 보내는 경우 (정리)
- **아니요, 기본 설정에서는 계속 보내지 않습니다.**  
  - **ENABLE_PC_TEST_AA_STREAM=0**, **BOARD_TX_0XAA_ENABLE=0** (기본값): 보드는 **PC가 Modbus 요청(Read DI 등)을 보낼 때만** 응답 프레임을 보냄. 평소에는 0xAA를 주기적으로 보내지 않음.
- **계속 0xAA를 받고 싶으면** 다음 둘 중 하나로 빌드하면 됨.  
  1. **ENABLE_PC_TEST_AA_STREAM=1** → 500ms마다 0xAA만 전송. 단, 이 모드에서는 **Modbus 요청에 응답하지 않음** (Read DI/Relay 동작 안 함).  
  2. **ENABLE_PC_TEST_AA_STREAM=0**, **BOARD_TX_0XAA_ENABLE=1** → Modbus 슬레이브 동작 + 유휴 시 500ms마다 0xAA 송신. Read DI/Relay도 사용 가능.

## 하드웨어
- RS485 A/B 접속, GND 공통. DE 극성 맞지 않으면 **RS485_DE_ACTIVE_HIGH** 를 0으로 바꿔 재빌드.
