# HPSB RS485 송신 경로 단독 검증 (문자열 테스트)

## 목적

Modbus/CRC/slave id와 분리하여 **HPSB → MAX3485 → RS485 A/B → PC** 구간이 동작하는지 확인.

- 부팅 후 **1초마다** 고정 문자열 `"HPSB_OK\r\n"` (8바이트) 송신.
- **Modbus 폴링·파싱·응답 로직은 실행하지 않음** (`HPSB_RS485_TX_STRING_TEST=1`일 때).

---

## 사용 방법

1. `Guro_HPSB/Modbus/Inc/modbus_cfg.h`에서 다음을 **1**로 설정:
   ```c
   #define HPSB_RS485_TX_STRING_TEST  1
   ```
2. 빌드 후 HPSB 다운로드.
3. PC에서 터미널(또는 시리얼 모니터)을 **9600 8N1**로 HPSB가 연결된 COM 포트에 연결.
4. 약 1초마다 `HPSB_OK` + 줄바꿈이 출력되면 **송신 경로 정상**.

---

## 송신 순서 (펌웨어)

1. **DE HIGH** (PA11) — MAX3485 송신 모드
2. 짧은 딜레이 (DE 정착)
3. **UART TX** — `HAL_UART_Transmit(..., "HPSB_OK\r\n", 8, 100)`
4. **TC(Transfer Complete) 대기**
5. **DE LOW** (PA11) — 수신 모드 복귀

LED2: 송신 **직전** ON, 송신 **직후** OFF (1초 주기로 깜빡임).

---

## 수정 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `Modbus/Inc/modbus_cfg.h` | `HPSB_RS485_TX_STRING_TEST` 매크로 추가 (기본 0). |
| `Modbus/Inc/modbus_slave.h` | `ModbusSlave_SendTestString(const char *str, uint16_t len)` 선언. |
| `Modbus/Src/modbus_slave.c` | `ModbusSlave_SendTestString()` 구현: DE HIGH → TX → TC → DE LOW. |
| `Core/Src/main.c` | `HPSB_RS485_TX_STRING_TEST` 시 1초마다 LED2 ON → `SendTestString("HPSB_OK\r\n", 8)` → LED2 OFF, Modbus 폴링/ProcessDebugLEDs 미호출. |

---

## 코드 설명

- **ModbusSlave_SendTestString(str, len)**  
  - `set_de_tx()` 로 PA11 HIGH.  
  - 약 500사이클 딜레이 후 `HAL_UART_Transmit(huart1, str, len, 100)`.  
  - `UART_FLAG_TC` set 될 때까지 대기 후 `set_de_rx()` 로 PA11 LOW.

- **main 루프 (`HPSB_RS485_TX_STRING_TEST=1`)**  
  - `HAL_GetTick()` 기준 1000ms마다:  
    - LED2 ON → `ModbusSlave_SendTestString("HPSB_OK\r\n", 8)` → LED2 OFF.  
  - 그 외에는 `__WFI()`.  
  - `ModbusSlave_Poll()`, `ModbusSlave_ProcessDebugLEDs()` 호출 안 함.

---

## 테스트 성공 시 검증되는 것

- **MCU PA9(UART TX)** 가 9600 8N1로 데이터 출력함.
- **PA11(DE)** 가 송신 구간에 HIGH, 그 외 LOW로 제어됨.
- **MAX3485** DE/DI 동작으로 RS485 A/B 라인에 차동 신호가 나감.
- **배선(A/B, GND)** 과 **PC 쪽 수신(포트/속도)** 이 맞으면 PC 터미널에 `HPSB_OK` 가 주기적으로 보임.

→ **HPSB → MAX3485 → RS485 A/B → PC** 구간이 정상이면, Modbus 무응답 원인은 수신/파싱/타이밍(또는 PC 측 타임아웃) 쪽으로 좁혀짐.

---

## 테스트 실패 시 의심 구간

| 현상 | 의심 구간 |
|------|-----------|
| PC에 아무것도 안 보임 | ① PA9(TX) 파형 없음 → UART 설정/클럭/핀 ② PA11이 HIGH로 안 감 → DE 제어/회로 ③ A/B에 신호 없음 → MAX3485·배선 ④ PC 포트/속도(9600 8N1)·COM 선택 오류 |
| LED2가 1초마다 깜빡이는데 PC에 안 보임 | 코드상 송신은 수행됨. → PA9·PA11 오실로 확인, A/B·터미네이션·PC 수신 경로 점검. |
| LED2가 안 깜빡임 | 1초 주기 코드 미동작 또는 LED2 핀/회로. `HPSB_RS485_TX_STRING_TEST`가 1인지, 해당 분기로 진입하는지 확인. |

테스트 후 **Modbus 디버깅으로 복귀**할 때는 `HPSB_RS485_TX_STRING_TEST`를 **0**으로 되돌리고 다시 빌드하면 됨.
