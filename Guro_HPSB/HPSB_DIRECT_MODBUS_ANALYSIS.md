# HPSB Direct Modbus 무응답 원인 분석

## 확정된 사실

- **PC Direct 모드**: HPSB로 정상 Modbus RTU FC05 프레임 전송
- **TX frame (hex)**: `01 05 00 00 00 00 CD CA`
  - slave=1, function=05, coil=0, value=OFF
  - CRC: 0xCDCA (LSB first)
- **결과**: 약 1초 후 RX ERR — *No response received, expected at least 2 bytes (0 received)*

---

## 분석 우선순위 및 원인 후보

### 1) HPSB 수신 경로

| 확인 항목 | 내용 |
|----------|------|
| MAX3485 RO → PA10(UART RX) | 회로상 RO(Output)가 MCU PA10에 연결되어 있는지, 단선/접촉 불량 없는지 |
| 프레임 수신 여부 | `process_frame()` 진입 여부 = **LED2 토글**로 확인 |

**디버그**: `HPSB_dbg_rx_len` — process_frame 진입 시점의 수신 바이트 수.  
- **8이면** 8바이트 전부 수신된 것. **8 미만이면** 일부만 수신(폴링 누락, UART 에러, 배선 문제).

---

### 2) HPSB Modbus slave 파싱

| 확인 항목 | 내용 |
|----------|------|
| slave id=1 인식 | `rx_buf[0] == 1` → **LED3 토글**로 확인 |
| CRC 검사 | `ModbusRTU_CRC16Check()` 통과 → **HPSB_dbg_crc_ok == 1** |
| frame complete | `rx_len >= 4` 후 t3.5(4ms) 무침 → `process_frame()` 호출 |

**디버그**:
- **LED2 토글, LED3 미토글** → slave id 불일치(수신 데이터가 0x01이 아님).
- **LED3 토글, HPSB_dbg_crc_ok == 0** → CRC 실패(수신 바이트 오류 또는 순서/길이 문제).
- **HPSB_dbg_crc_ok == 1** 인데도 응답 없음 → FC05 파싱 실패 또는 `send_response()` 이후(TX/DE) 문제.

---

### 3) HPSB 응답 송신

| 확인 항목 | 내용 |
|----------|------|
| FC05 응답 8바이트 생성 | `ModbusRTU_BuildFC05Response()` → 01 05 00 00 00 00 + CRC 2바이트 |
| PA9(TX) 파형 | 오실로스코프로 PA9에서 9600 8N1 파형 출력 여부 확인 |

**디버그**: **LED4 ON** 구간 = `send_response()` 진입 ~ TC 대기 후 `set_de_rx()` 직후까지.  
- LED4가 **한 번이라도 ON**이면 코드상으로는 응답 전송 경로 진입.
- **LED4가 절대 안 켜지면** CRC 실패 또는 FC05 파싱 실패로 `send_response()` 미호출.

---

### 4) RS485 DE(PA11) 제어

| 확인 항목 | 내용 |
|----------|------|
| idle(수신) | PA11 = LOW (DE=LOW → MAX3485 수신) |
| 응답 송신 시 | PA11 = HIGH (DE=HIGH → 송신) |
| TC 완료 후 | PA11 = LOW 복귀 |

**측정**: `send_response()` 내 `set_de_tx()` 호출 시점부터 `set_de_rx()` 직전까지 PA11이 HIGH여야 함.  
- PA11이 **항상 LOW** → 응답 경로 미진입이거나 DE 제어 회로/펌웨어 반전 오류.
- PA11 **HIGH 구간 있음** → DE 제어는 동작, 문제는 TX 라인(PA9) 또는 A/B 배선/터미네이션.

---

### 5) LED1 깜빡임 의미

| 가능성 | 설명 |
|--------|------|
| 첫 바이트 수신 | `process_frame()` 내 `dbg_led1_pulse_ms(300)` — 유효 프레임 처리 시 LED1 300ms 점등 |
| CRC 실패 | `dbg_led1_blink3_start()` — LED1 3회 점멸 |
| heartbeat 아님 | 메인 루프에 LED1 주기 점멸 없음 (LED_PWR_OFF 유지) |
| reset loop | 리셋 시 초기화만 하고 LED1 고정 점멸 패턴 없음 |
| watchdog/brownout | 현재 코드에 WDG 사용 없음; brownout 시 리셋이지 LED만 반복되지는 않음 |

**정리**: LED1이 **한 번 300ms 점등** → process_frame 진입(유효 프레임 수신). **3회 점멸** → CRC 실패.

---

## 디버그 추가 사항 (구현됨)

| 항목 | 구현 |
|------|------|
| 유효 프레임 수신 시 LED2 토글 | `process_frame()` 진입 시 `HAL_GPIO_TogglePin(LED02)` |
| slave id 일치 시 LED3 토글 | `rx_buf[0] == MODBUS_SLAVE_ADDR` 직후 `HAL_GPIO_TogglePin(LED03)` |
| 응답 송신 직전 LED4 ON | `send_response()` 시작 시 `LED04 = ON` (LOW active) |
| 송신 완료 후 LED4 OFF | `set_de_rx()` 직후 `LED04 = OFF` |
| 상태 변수 | `HPSB_dbg_rx_len`, `HPSB_dbg_crc_ok`, `HPSB_dbg_slave_match`, `HPSB_dbg_tx_reply_start` (modbus_slave.h extern) |

---

## 수정 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `Guro_HPSB/Modbus/Inc/modbus_slave.h` | 디버그용 extern 변수 4개 선언 |
| `Guro_HPSB/Modbus/Src/modbus_slave.c` | 디버그 변수 정의, process_frame에서 LED2/LED3 토글 및 플래그 설정, send_response에서 LED4 ON/OFF 및 `HPSB_dbg_tx_reply_start` 설정 |

---

## 수정 전/후 동작

### 수정 전

- LED2/LED3: CRC 통과/파싱 단계 등 다른 의미로 사용.
- LED4: parser 디버그 시 OFF 유지 또는 RELAY3 표시.
- 수신/파싱/응답 구간을 디버거 변수로만 구분 가능.

### 수정 후

- **LED2**: 프레임 무침 타임아웃 후 `process_frame()` 진입 시마다 토글 → “유효 프레임 수신” 여부 확인.
- **LED3**: slave id 일치 시마다 토글 → “slave=1 인식” 여부 확인.
- **LED4**: `send_response()` 진입 시 ON, 송신 완료 후 OFF → “응답 전송 시도” 구간 확인.
- **HPSB_dbg_*** : 디버거 watch로 `rx_len`, CRC 성공, slave 일치, 응답 진입 여부 확인 가능.

---

## PA10 / PA9 / PA11 측정 포인트

| 핀 | 신호 | 측정 내용 | 정상 시 |
|----|------|-----------|---------|
| **PA10** | USART1_RX (MCU 입력) | MAX3485 RO(출력) → PA10 구간 전압 파형 | PC에서 FC05 8바이트 보낼 때 9600bps 8N1 파형 수신 |
| **PA9** | USART1_TX (MCU 출력) | MCU → MAX3485 DI(입력) 구간 전압 파형 | HPSB가 응답 보낼 때 9600bps 8N1 파형 출력 (LED4 ON 구간과 동시) |
| **PA11** | RS485 DE | DE 제어 전압 | idle: LOW, 응답 송신 구간: HIGH, 송신 완료 후: LOW |

**측정 순서 제안**

1. PC에서 FC05 한 번 전송 시 **PA10**에 8바이트 분량 RX 파형이 오는지 확인 → 수신 경로 검증.
2. 동일 시 **LED2/LED3** 토글 여부 확인 → process_frame 진입 및 slave id 일치 여부.
3. **LED4**가 잠깐이라도 ON 되는지 확인 → `send_response()` 진입 여부.
4. LED4 ON 구간에 **PA11** HIGH, **PA9**에 TX 파형이 나오는지 확인 → DE 및 TX 경로 검증.
5. **HPSB_dbg_rx_len**, **HPSB_dbg_crc_ok**, **HPSB_dbg_slave_match**, **HPSB_dbg_tx_reply_start**를 디버거로 watch → 어느 단계에서 끊기는지 확인.

---

## 무응답 원인 정리 (판단 흐름)

1. **LED2 미토글**  
   → `process_frame()` 미진입.  
   → 수신 실패: PA10 경로, UART 설정(속도/폴링), 또는 frame silence(4ms) 내에 8바이트가 다 안 모임.

2. **LED2 토글, LED3 미토글**  
   → slave id 불일치.  
   → 수신 데이터가 `0x01`이 아님(배선 반전, 노이즈, 바이트 유실 등).

3. **LED3 토글, LED1 3회 점멸**  
   → CRC 실패.  
   → `HPSB_dbg_crc_ok == 0`. 수신 바이트 오류 또는 길이/순서 문제.

4. **LED3 토글, LED1 1회 점등, LED4 미점등**  
   → CRC 통과 후 FC05 파싱 실패 또는 switch 이외 FC 처리.  
   → `rx_len < 8`이면 FC05 파서가 -1 반환(ParseFC05Request는 len>=8 필요).

5. **LED4 ON 구간 있음, PC는 0 bytes**  
   → HPSB는 응답 전송 시도함.  
   → PA9 파형, PA11 HIGH, A/B 극성, 터미네이션, PC 쪽 RX 타임아웃/포트 확인.

위 순서대로 LED와 디버그 변수, PA10/PA9/PA11을 보면 **무응답의 정확한 구간**을 좁힐 수 있음.
