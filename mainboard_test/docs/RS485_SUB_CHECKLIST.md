# RS485 하단 버스 (Mainboard ↔ HPSB/LPSB) 연결·통신 체크리스트

하드웨어(PA2/PA3/PB12, A/B/GND)는 확인된 상태에서 **통신이 안 될 때** 펌웨어·설정을 점검할 때 사용합니다.

---

## 1. 보드레이트 (반드시 동일)

| 보드        | UART    | 용도     | 보드레이트 | 설정 위치 |
|------------|---------|----------|------------|-----------|
| **Mainboard** | USART2  | Sub RS485 | **9600**   | `Core/Src/main.c` MX_USART2_UART_Init, `huart2.Init.BaudRate` |
| **HPSB**      | USART1  | RS485     | **9600**   | `Core/Src/main.c` MX_USART1_UART_Init, `huart1.Init.BaudRate` |
| **LPSB**      | USART1  | RS485     | **9600**   | `Core/Src/main.c` MX_USART1_UART_Init, `huart1.Init.BaudRate` |

- **주의**: HPSB/LPSB를 115200으로 두면 메인보드(9600)와 불일치하여 통신 불가.  
- 이번 수정에서 HPSB·LPSB를 **9600**으로 맞춰 두었음.  
- **다시 빌드 후 HPSB·LPSB 펌웨어를 재다운로드**해야 적용됩니다.

---

## 2. Mainboard 핀 (MAX3485 연결)

| 핀    | 신호           | 용도        | 비고                    |
|-------|----------------|-------------|-------------------------|
| **PA2** | USART2_TX      | RS485 TX    | CubeMX: RS485_TX        |
| **PA3** | USART2_RX      | RS485 RX    | CubeMX: RS485_RX        |
| **PB12**| RS_485_DE_RE   | DE/RE       | HIGH=송신, LOW=수신     |

- 초기값: PB12 = LOW (수신 모드). `main.c` GPIO 초기화에서 `RS_485_DE_RE_Pin` = RESET.
- Modbus 전송 시: `set_de_tx()` → 1ms 대기 → TX → 2ms 대기 → `set_de_rx()`.

---

## 3. Slave ID (펌웨어 설정)

| 보드   | Slave ID | 설정 위치 |
|--------|----------|-----------|
| HPSB   | **1**     | `Modbus/Inc/modbus_cfg.h` → `MODBUS_SLAVE_ADDR` (HPSB는 1) |
| LPSB   | **2**     | `Modbus/Inc/modbus_cfg.h` → `MODBUS_SLAVE_ADDR` (LPSB는 2) |

- 메인보드 코드: HPSB=1, LPSB1=2, LPSB2=4, LPSB3=8.  
- 현재 구성이 HPSB 1대 + LPSB 1대(2번)이면 위 설정이면 됨.

---

## 4. HPSB/LPSB RS485 핀

- HPSB: **USART1** (PA9=TX, PA10=RX) + **DE** (modbus_cfg: `RS485_DE_GPIO_Port/Pin` → CubeMX에서 RS485 DE 핀).
- LPSB: 동일하게 USART1 + DE.
- 같은 라인(A, B, GND)에 연결되어 있으면 됨 (이미 확인하신 내용과 동일).

---

## 5. 통신이 안 될 때 확인 순서

1. **보드레이트**
   - HPSB/LPSB `main.c`에서 `huart1.Init.BaudRate = 9600` 인지 확인.
   - 9600으로 수정했다면 **HPSB·LPSB 펌웨어 다시 빌드·다운로드**.

2. **메인보드 로그 (UART1)**
   - `GATEWAY_WRITE_DEBUG_LOG 1` 로 빌드 후, PC에서 HPSB RELAY1 EN 누를 때:
     - `[GW] USART2 TX done` 까지 나오면 → 메인보드 TX는 동작하는 것.
     - `[GW] USART2 RX timeout or invalid` → 메인보드가 응답을 못 받은 것 (배선, 슬레이브 전원/펌웨어, 보드레이트 재확인).
     - `[GW] USART2 RX received OK` → 하단 버스 수신 성공.

3. **DE/RE (PB12)**
   - 오실로스코프 있으면: TX 시 PB12가 HIGH로 갔다가, 전송 끝나고 2ms 후 LOW로 내려오는지 확인.
   - MAX3485 데이터시트: DE high = driver enable(TX), /RE low = receiver enable(RX).  
     한 핀으로 DE·/RE 같이 쓰면 HIGH=TX, LOW=RX가 일반적.

4. **배선**
   - Mainboard A ↔ HPSB A ↔ LPSB A (같은 라인).
   - Mainboard B ↔ HPSB B ↔ LPSB B.
   - GND 공통.
   - 터미네이션: 라인 끝단 120Ω 권장 (필요 시).

5. **전원·리셋**
   - HPSB/LPSB 전원 들어온 뒤 리셋 한 번씩 해서 Modbus 슬레이브가 정상 대기하는지 확인.

---

## 6. 이번에 수정한 내용 요약

- **HPSB** `Core/Src/main.c`: `huart1.Init.BaudRate` **115200 → 9600**.
- **LPSB** `Core/Src/main.c`: `huart1.Init.BaudRate` **115200 → 9600**.
- **Mainboard** `Modbus/Src/modbus_master.c`:  
  - FC05 전송 후 **5ms 대기** (`MODBUS_FC05_RX_DELAY_MS`) 후,  
  - **RX 버퍼 플러시** (에코/잔류 데이터 제거),  
  - 그 다음 8바이트 응답 대기.

위 적용 후 **메인보드 + HPSB + LPSB 모두 다시 빌드·다운로드**한 뒤 통신을 다시 확인하면 됩니다.
