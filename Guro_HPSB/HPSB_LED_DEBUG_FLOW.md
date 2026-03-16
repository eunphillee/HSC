# HPSB process_frame() 및 LED 디버그 점검

## 1. 클릭 1회당 process_frame() 호출 횟수

### 호출 조건 (modbus_slave.c)

- `ModbusSlave_Poll()` 내부에서 **한 번만** 호출되는 조건:
  - `rx_len > 0`
  - `(HAL_GetTick() - last_rx_tick) >= FRAME_SILENCE_MS` (4 ms)

즉, **바이트 수신 후 4ms 이상 무침(침묵)이 지나면** “프레임 끝”으로 간주하고 `process_frame()`을 **1회** 호출한다.

### 1회 호출이 보장되는 이유

- 호출 직후 `rx_len = 0`, `last_rx_tick = 0`, `uart_clear_errors()` 로 수신 상태를 리셋한다.
- 다음 프레임은 **새로 들어온 바이트 + 그 후 4ms 침묵**이 있어야만 다시 조건을 만족한다.
- USART1 수신은 **폴링만 사용** (`HAL_UART_Receive(..., 0)`). RXNE 인터럽트로 수신하지 않으므로, 한 바이트가 두 곳에서 처리되는 일은 없다.

**결론: PC에서 클릭 1회 → 메인보드가 FC05 1회 전송 → HPSB는 그 1개 프레임에 대해 process_frame()을 정확히 1회만 호출하는 구조가 맞다.**  
(메인보드가 같은 요청을 두 번 보내면 그때는 process_frame()도 2회 호출될 수 있음.)

---

## 2. process_frame() 내부 단계별 진행 및 LED

| 순서 | 단계 | 조건 | LED 동작 | 진행 후 |
|------|------|------|----------|---------|
| 0 | **process_frame entry** | 진입 시 항상 | **LED1 ON 300ms 후 OFF** | 아래 검사로 진행 |
| 1 | **rx_len < 4** | 프레임 길이 부족 | (없음) | `set_de_rx(); return;` |
| 2 | **slave id check** | `rx_buf[0] != MODBUS_SLAVE_ADDR` | (없음) | `set_de_rx(); return;` |
| 3 | **CRC check** | `ModbusRTU_CRC16Check(...) != 0` | **LED1 3회 점멸 (각 150ms)** | `set_de_rx(); return;` |
| 4 | **CRC pass** | CRC 통과 | **LED2 ON 300ms 후 OFF** | FC 분기로 진행 |
| 5 | **FC05 parse** | FC==0x05 이고 `ParseFC05Request` 실패 | **LED2 3회 점멸 (각 150ms)** | `set_de_rx(); break;` |
| 6 | **coil_addr 범위** | `coil_addr >= COIL_COUNT` | **LED3 3회 점멸 (각 150ms)** | `set_de_rx(); break;` |
| 7 | **FC05 coil_addr==0** | 파싱 성공, coil_addr==0 | **LED3 ON 500ms 후 OFF** | `send_response()` 호출 |
| 8 | **FC05 coil_addr!=0** | 파싱 성공, coil_addr!=0 | **LED3 2회 점멸 (각 150ms)** | `send_response()` 호출 |
| 9 | **send_response entry** | `send_response()` 진입 시 | **LED1+LED2+LED3 전부 ON 500ms 후 OFF** | UART TX 수행 |

- **slave id matched** 단계에는 별도 LED 없음. **LED2는 CRC 통과 이후에만** 켜지며, “CRC pass” 의미로 사용한다.

---

## 3. LED2 / LED3 / LED123 이 나오는 조건 요약표

| LED | 나타나는 조건 | 코드 위치 |
|-----|----------------|-----------|
| **LED1** | process_frame() 진입 (프레임 수신 완료로 간주) | 항상: `led_pulse(LED01, 300)` |
| **LED1** | CRC 실패 | `ModbusRTU_CRC16Check != 0` → `led_blink_times(LED01, 3, 150)` |
| **LED2** | CRC 통과 | CRC 통과 직후: `led_pulse(LED02, 300)` |
| **LED2** | FC05 파싱 실패 | `ModbusRTU_ParseFC05Request != 0` → `led_blink_times(LED02, 3, 150)` |
| **LED3** | coil 주소 범위 초과 | `coil_addr >= COIL_COUNT` → `led_blink_times(LED03, 3, 150)` |
| **LED3** | FC05 성공, coil_addr==0 | `led_pulse(LED03, 500)` |
| **LED3** | FC05 성공, coil_addr!=0 | `led_blink_times(LED03, 2, 150)` |
| **LED1+2+3** | send_response() 진입 (응답 직전) | `led123_on_500ms_then_off()` |

- LED2가 켜졌다 = **CRC까지 통과**한 프레임이다.  
- LED3가 500ms 점등했다 = **FC05 파싱 성공 + coil_addr==0** (RELAY1)이다.  
- LED1+2+3 동시 500ms = **응답 전송 직전**이다.

---

## 4. 클릭 1회 기준 예상 LED 시퀀스 (FC05 addr=0 성공 시)

**가정:** PC에서 “HPSB RELAY1 EN” 1회 클릭 → 메인보드가 slave_id=1, FC05, coil=0 한 프레임만 전송 → HPSB가 정상 수신·CRC·파싱·응답.

| 시간순 | 동작 | 관찰 가능 LED |
|--------|------|----------------|
| 1 | process_frame() 진입 | **LED1** 300ms ON 후 OFF |
| 2 | CRC 통과 | **LED2** 300ms ON 후 OFF |
| 3 | FC05 파싱 성공, coil_addr==0 | **LED3** 500ms ON 후 OFF |
| 4 | send_response() 진입 | **LED1+LED2+LED3** 동시 500ms ON 후 OFF |
| 5 | UART TX 후 set_de_rx() | (LED 추가 점등 없음) |

**총 예상 흐름:**  
LED1 300ms → LED2 300ms → LED3 500ms → LED1·2·3 동시 500ms  
(중간 대기 포함 약 2초 내외로 이어져 보일 수 있음.)

- **LED1만 300ms:** 프레임은 들어왔으나 그 안에서 **rx_len<4** 또는 **slave id 불일치**로 곧바로 return.
- **LED1 300ms + LED1 3회 점멸:** **CRC 실패**.
- **LED1 300ms + LED2 300ms:** CRC 통과. 이후 **LED2 3회 점멸**이면 FC05 파싱 실패.
- **LED1 300ms + LED2 300ms + LED3 500ms:** FC05 coil_addr==0 성공. 이어서 **LED1·2·3 동시 500ms**가 나오면 send_response()까지 진입한 것이다.

---

## 5. process_frame() 중복 호출 가능성

### 원인 후보

| 후보 | 설명 | 현재 코드상 |
|------|------|-------------|
| **RX 인터럽트** | RXNE 등으로 인터럽트에서 수신 시, Poll과 이중 처리 가능성 | USART1은 **폴링만 사용**. `HAL_UART_Receive(..., 0)` 호출만 있음. 인터럽트 수신 미사용 → **해당 없음**. |
| **Frame timeout** | 4ms 침묵이 한 프레임 중간에 만족되어 잘린 프레임으로 1회, 나머지로 또 1회 호출 | 9600bps에서 8바이트 수신은 약 8ms. 메인 루프가 4ms 이상 지연되면 **이론상** 중간에 timeout 가능. 다만 호출 후 `rx_len=0`으로 리셋되므로, “같은 프레임”에 대해 2회 호출되려면 **첫 번째 호출 시점에 이미 rx_len>0이고 4ms 지남**이어야 함. 정상적으로 한 프레임 다 받은 뒤 4ms 지나면 1회만 호출됨. |
| **Duplicate request** | 메인보드/PC가 같은 FC05를 두 번 보냄 | 애플리케이션 계층 이슈. HPSB는 수신한 프레임당 1회 process_frame() 호출. |

### 정리

- **클릭 1회에 process_frame()이 2회 이상 보인다면:**  
  - 메인보드가 실제로 2회 이상 전송하는지,  
  - 또는 메인 루프 지연으로 **한 프레임이 두 번 나뉘어** (예: 앞 4바이트로 1회, 나머지로 1회) timeout 되는지 확인하는 것이 좋다.
- **클릭 1회에 1회만 호출되도록 설계된 구조**이며, 수신은 폴링 전용이라 RX 인터럽트로 인한 중복 호출은 없다.

---

## 6. HPSB LED Low-Active 확인

- **led_status.c:**  
  `LED01~04: LOW active (RESET=ON, SET=OFF)`  
  `LED_PWR_ON()  = HAL_GPIO_WritePin(..., GPIO_PIN_RESET);`  
  `LED_PWR_OFF() = HAL_GPIO_WritePin(..., GPIO_PIN_SET);`

- **modbus_slave.c:**  
  - `led_pulse(port, pin, ms)`  
    - ON: `GPIO_PIN_RESET`  
    - OFF: `GPIO_PIN_SET`  
  - `led123_on_500ms_then_off()`  
    - ON: 세 핀 모두 `GPIO_PIN_RESET`  
    - OFF: 세 핀 모두 `GPIO_PIN_SET`

**결론: HPSB LED는 Low-Active가 맞다. GPIO_PIN_RESET = LED ON, GPIO_PIN_SET = LED OFF 로 일관되게 동작한다.**
