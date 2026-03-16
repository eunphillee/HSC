# HPSB Direct Modbus 무응답 — 펌웨어 중심 분석

## 전제

- 하드웨어 패스(배선, A/B 차동 신호, /RE·DE LOW)는 정상으로 간주.
- **응답 0 bytes** → 원인을 **펌웨어 수신/파싱/응답** 경로에서 구분.

---

## 확정 정보

- PC → 정상 Modbus RTU FC05 프레임 전송 (`01 05 00 00 00 00 CD CA`)
- A/B 라인 차동 신호 있음, /RE=DE=LOW 확인
- 응답 0 bytes → **수신(RX) / 파싱(parse) / 응답(reply)** 중 한 구간에서 막힘.

---

## 1) UART RX 처리 흐름 점검

### 구현 요약

- **방식**: 폴링. 인터럽트 없음. `ModbusSlave_Poll()`에서 `HAL_UART_Receive(..., 1, 0)`으로 1바이트씩 읽음.
- **버퍼**: `rx_buf[]`, `rx_len` — 바이트 수신 시 `rx_buf[rx_len++] = b`.
- **프레임 종료**: 수신 후 **4ms 무침**(`FRAME_SILENCE_MS`)이 지나면 한 프레임 완료로 보고 `process_frame()` 호출. (Modbus t3.5 ≈ 3.65ms와 유사.)

### 점검 포인트

| 항목 | 확인 방법 |
|------|-----------|
| 바이트가 버퍼에 쌓이는지 | **LED2**: 바이트 수신 시마다 토글. 8바이트 수신 시 LED2가 8번 토글. |
| rx_len 증가 여부 | 디버거에서 `HPSB_dbg_rx_len` watch. `process_frame()` 진입 시 복사됨. |
| 3.5 char / idle 기준 | `last_rx_tick` 갱신 후 `(HAL_GetTick() - last_rx_tick) >= 4` 이면 `process_frame()` 호출. |

### 막히는 경우 (RX 단계)

- **LED2가 전혀 안 움직임** → UART로 1바이트도 수신 안 됨. (초기화, 포트, 클럭, 폴링 주기 등 확인.)
- **LED2는 움직이는데 `HPSB_dbg_rx_len`이 8 미만** → 프레임이 다 오기 전에 폴링이 끊기거나, 4ms 안에 8바이트가 안 모임(폴링 주기/지연 확인).

---

## 2) Modbus 파싱 점검

### 구현 요약

- **slave id**: `rx_buf[0] == MODBUS_SLAVE_ADDR`(1). 불일치 시 즉시 return, 응답 없음.
- **function code**: `rx_buf[1]` → 0x05면 FC05 처리.
- **FC05**: `ModbusRTU_ParseFC05Request()` — `len >= 8`, coil_addr = (buf[2]<<8)|buf[3], value = (buf[4]==0xFF && buf[5]==0x00) ? 1 : 0.
- **coil 0**: coil_addr == 0 처리 후 `ModbusTable_SetCoil(0, value)`, `ModbusRTU_BuildFC05Response()` 호출.
- **CRC**: `ModbusRTU_CRC16Check(rx_buf, rx_len)` — 실패 시 return, 응답 없음.

### 점검 포인트

| 항목 | 확인 방법 |
|------|-----------|
| slave id = 1 | **LED4** 토글 = slave id 일치 구간 진입. |
| CRC 통과 | **LED3** 토글 = CRC 통과. `HPSB_dbg_crc_ok == 1`. |
| FC05·coil 0 | CRC 통과 후 switch(case 0x05), ParseFC05 성공, coil_addr < COIL_COUNT 이면 `send_response()` 호출. |

### 막히는 경우 (Parse 단계)

- **LED2는 8번 토글, LED4 미토글** → `process_frame()`은 들어오지만 slave id 불일치. (수신 데이터가 0x01이 아님.)
- **LED4 토글, LED3 미토글** → slave id 일치, CRC 실패. `HPSB_dbg_crc_ok == 0`. (수신 바이트 오류/길이/순서.)
- **LED3 토글, LED1이 “응답 송신”으로 안 켜짐** → FC05 파싱 실패 또는 coil 범위 이탈 등으로 `send_response()` 미호출.

---

## 3) 응답 송신 점검

### 구현 요약

- **진입**: FC05 처리 경로에서 `send_response(tx_pdu, tx_len)` 호출.
- **순서**:  
  1) `HPSB_dbg_reply_started = 1`, LED1 ON  
  2) `ModbusRTU_AppendCRC(pdu, pdu_len)`  
  3) `set_de_tx()` → **DE(PA11) HIGH**  
  4) 짧은 딜레이 후 `HAL_UART_Transmit(..., pdu_len+2, 100)`  
  5) `while (!UART_FLAG_TC)`  
  6) `set_de_rx()` → **DE(PA11) LOW**  
  7) LED1 OFF, `HPSB_dbg_reply_done = 1`, `HPSB_dbg_reply_started = 0`

### 점검 포인트

| 항목 | 확인 방법 |
|------|-----------|
| 응답 프레임 생성 | FC05 응답 = 01 05 00 00 00 00 + CRC 2바이트 = 8바이트. |
| send_response() 진입 | **LED1** ON 구간 있음. `HPSB_dbg_reply_started` 1 되었다가 0, `HPSB_dbg_reply_done == 1`. |
| DE HIGH → TX → TC → DE LOW | PA11 스코프: 송신 구간에서 HIGH, 이후 LOW. PA9에서 9600 8N1 파형. |

### 막히는 경우 (Reply 단계)

- **LED1 ON 구간 없음** → `send_response()` 자체 미진입. (위 Parse 단계에서 막힘.)
- **LED1 ON/OFF·reply_done=1 인데 PC는 0 bytes** → HPSB는 송신 시도함. PA9/PA11/회로·터미네이션·PC 쪽 RX·타임아웃 확인.

---

## 4) 디버그 추가 사항 (현재 구현)

### LED 의미

| LED | 의미 |
|-----|------|
| **LED2** | 바이트 수신 시마다 토글 (폴링 루프에서 1바이트 받을 때마다) |
| **LED3** | CRC 통과 시 토글 |
| **LED4** | slave id 일치 시 토글 |
| **LED1** | 응답 송신 직전 ON, 송신 완료 직후 OFF |

### 상태 변수 (디버거 watch)

| 변수 | 의미 |
|------|------|
| `HPSB_dbg_rx_len` | `process_frame()` 진입 시점의 수신 바이트 수 (기대: 8) |
| `HPSB_dbg_crc_ok` | 1=CRC 통과, 0=미통과 |
| `HPSB_dbg_slave_match` | 1=slave id 일치 |
| `HPSB_dbg_reply_started` | 1=응답 송신 중 (`send_response()` 내) |
| `HPSB_dbg_reply_done` | 1=응답 송신 완료 (한 번 1이 된 뒤 유지) |

---

## 5) TX 경로 단독 검증 (테스트 함수)

- **함수**: `ModbusSlave_SendTestFrame(void)`  
  Modbus 파싱 없이 **고정 8바이트** `01 05 00 00 00 00 CD CA` 만 반복 송신.
- **동작**: `set_de_tx()` → 짧은 딜레이 → `HAL_UART_Transmit(..., 8, 100)` → TC 대기 → `set_de_rx()`.
- **활성화**: `modbus_cfg.h`에서 `HPSB_TX_TEST_ENABLE` 을 **1**로 하면, `main` 루프에서 **2초마다** `ModbusSlave_SendTestFrame()` 호출.
- **목적**: 수신/파싱 없이 **DE HIGH → UART TX → TC → DE LOW** 및 PA9/PA11 동작만 검증. PC에서 해당 구간에 8바이트가 수신되면 TX 경로는 정상.

---

## 막히는 단계 구분 (판단 흐름)

| 관찰 | 막히는 단계 | 다음 확인 |
|------|-------------|-----------|
| LED2 전혀 토글 안 함 | **RX** | UART 설정, 폴링 호출 빈도, 클럭, 핀 매핑 |
| LED2만 토글, LED4/LED3 안 함 | **Parse (slave/CRC)** | `HPSB_dbg_rx_len`(8인지), `HPSB_dbg_slave_match`, `HPSB_dbg_crc_ok` |
| LED2·LED4·LED3 토글, LED1 ON 없음 | **Parse (FC05/coil)** | FC05 분기, ParseFC05 반환값, coil_addr |
| LED1 ON/OFF, reply_done=1, PC 0 bytes | **Reply (물리/타이밍)** | PA11 HIGH 구간, PA9 파형, A/B·터미네이션, PC 타임아웃 |

---

## 수정 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `Modbus/Inc/modbus_slave.h` | `HPSB_dbg_reply_started`, `HPSB_dbg_reply_done` 추가. `ModbusSlave_SendTestFrame()` 선언. (기존 `tx_reply_start` 제거) |
| `Modbus/Inc/modbus_cfg.h` | `HPSB_TX_TEST_ENABLE` 추가 (0=기본, 1=2초마다 테스트 프레임 송신). |
| `Modbus/Src/modbus_slave.c` | ① 바이트 수신 시 LED2 토글 ② CRC 통과 시 LED3 토글 ③ slave 일치 시 LED4 토글 ④ 응답 송신 직전/직후 LED1 ON/OFF 및 reply_started/reply_done 설정 ⑤ `ModbusSlave_SendTestFrame()` 구현 (고정 8바이트 송신). |
| `Core/Src/main.c` | `HPSB_TX_TEST_ENABLE` 시 2초마다 `ModbusSlave_SendTestFrame()` 호출 및 `s_tx_test_last_tick` 관리. |

---

## 수정 전/후 동작

### 수정 전

- LED2: process_frame 진입 시 토글.
- LED3: slave id 일치 시 토글.
- LED4: send_response 구간 ON/OFF.
- 응답 완료 플래그 없음.

### 수정 후

- **LED2**: 바이트가 실제로 UART에서 읽힐 때마다 토글 → **RX 경로** 가시화.
- **LED3**: CRC 통과 시에만 토글 → **Parse(CRC)** 구간 명확.
- **LED4**: slave id 일치 시에만 토글 → **Parse(slave)** 구간 명확.
- **LED1**: 응답 송신 직전 ON, 직후 OFF → **Reply** 구간 명확.
- **reply_started / reply_done**: 디버거로 응답 진입·완료 여부 확인.
- **ModbusSlave_SendTestFrame()**: TX만 단독으로 검증 가능.

---

## 무응답 원인 정리 (펌웨어 관점)

1. **RX에서 막힘**  
   LED2 미토글 또는 `HPSB_dbg_rx_len` 8 미만 → 폴링/타이밍/버퍼/에러 플래그(ORE 등) 점검.

2. **Parse에서 막힘**  
   LED4 미토글(slave 불일치) 또는 LED3 미토글(CRC 실패) 또는 LED3까지 오는데 LED1 안 켜짐(FC05/coil 파싱·범위) → 수신 데이터·CRC·파서 로직 점검.

3. **Reply에서 막힘**  
   LED1 ON/OFF·reply_done=1 인데 PC 0 bytes → 펌웨어는 정상 송신 시도. DE/TX 핀·회로·PC 수신·타이밍 점검.  
   TX 단독 검증 시 `HPSB_TX_TEST_ENABLE=1` 로 2초 주기 송신하여 PC 수신 여부로 TX 경로 판단.
