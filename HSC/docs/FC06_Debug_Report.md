# FC06 처리 경로 디버그 및 수정 보고서

## 1. FC06 호출 흐름 (PC → Mainboard)

FC06(Write Single Register)은 **USART2/하위버스를 사용하지 않습니다**. 주소 2101(DO bitmap), 2120(PC_ON_EN), 2121(PC_RESET_EN), 3000~3002(SYSCFG) 모두 메인보드 내부에서 처리됩니다.

```
PC (FC06 addr=2101 val=1)
  → USART1 RX (ReceiveToIdle_IT)
  → UpstreamSlaveUart1_Poll() : rx_ring에 프레임 수신, FRAME_END_MS(4ms) 후 처리
  → ModbusRTU_GetExpectedRequestLength() = 8 (FC06)
  → ModbusRTU_CRC16Check()
  → process_modbus_frame(frame_buf, expected, agg)
       → fc=0x06, write_data=&frame[4]
       → UpstreamSlave_HandleRequest(0x06, start_addr, 0, write_data, agg, resp_pdu, ...)
            → handle_fc06(start_addr, write_data, response, resp_max)  [Gateway/Src/upstream_slave_h2tech.c]
                 → start_addr==2101: IO_Main_WriteDO_Bitmap(value & 0x0F), response[0..5]=FC06 echo
                 → return 6
       → resp_len=6
  → tx_frame = slave_id + resp_pdu(6) + CRC(2), tx_len=9
  → set_de_tx() → HAL_UART_Transmit(huart1, tx_frame, 9) → UART TC 대기 → set_de_rx()
  → PC가 USART1에서 9바이트 수신
```

**실패가 나는 구간 후보:**
- **CRC/길이 검사**에서 걸리면 `process_modbus_frame`이 호출되지 않음 → 응답 없음
- **handle_fc06**가 -1 반환 시 기존에는 응답을 보내지 않음 → No Response
- **HAL_UART_Transmit** 직후 DE를 바로 RX로 전환하면, 마지막 바이트가 나가기 전에 전환되어 **응답이 잘림** → PC에서 decode fail / No response

## 2. 수정 사항 요약

### 2.1 FC06 상세 로그 (FC06_DEBUG_LOG=1 시)

| 단계 | 로그 문자열 |
|------|-------------|
| FC06 수신 직후 | `[GW] FC06 received from PC addr=%u val=%u` |
| 로컬 매핑(2101 DO) | `[GW] FC06 mapped to local (no sub) addr=%u value=%u` |
| PC 응답 송신 직전 | `[GW] sending response to PC len=%u` |
| 응답 HEX | `[GW] response HEX: xx xx xx ...` |

- **위치**: `handle_fc06()` 내 수신 직후·2101 처리 직전, `process_modbus_frame()` 내 송신 직전.
- **출력**: UART1. PC와 동시 사용 시 로그가 응답과 섞이므로, **FC06_DEBUG_LOG=1일 때는 별도 시리얼로 로그만 보는 것을 권장**.

### 2.2 FC06 실패 시 예외 응답 (무응답 방지)

- **파일**: `Application/Src/upstream_slave_uart1.c`
- **동작**: FC02/03/05/06/0F 처리 중 `resp_len <= 0`이면  
  `resp_pdu[0] = fc | 0x80`, `resp_pdu[1] = 0x04`(Slave device failure), `resp_len = 2`로 설정 후 **항상 응답 프레임 전송**.
- **효과**: handle_fc06 실패(-1) 또는 기타 오류 시에도 PC는 **예외 응답(0x86 0x04)**을 받고, “No Response”/멈춤을 피함.

### 2.3 UART1 응답 송신 후 TC 대기

- **파일**: `Application/Src/upstream_slave_uart1.c`
- **변경**: `HAL_UART_Transmit(huart1, tx_frame, tx_len)` 직후  
  `UART_FLAG_TC`가 set될 때까지 대기(최대 50ms)한 뒤 `set_de_rx()` 호출.
- **이유**: RS485 반이중에서 DE를 너무 빨리 RX로 바꾸면 마지막 바이트가 나가기 전에 수신 모드로 전환되어, **응답이 잘리거나 깨져** PC에서 “Unable to decode response” / “No response”가 발생할 수 있음.

### 2.4 USART2 write 경로 (참고)

- **파일**: `Modbus/Src/modbus_master.c`
- **순서**:  
  1) TX 전 RX flush  
  2) DE HIGH  
  3) HAL_UART_Transmit  
  4) UART TC 대기  
  5) DE LOW  
  6) 즉시 HAL_UART_Receive(또는 상태기 반복)로 수신, **TX 후에는 flush 없음**
- FC06은 하위버스를 타지 않으므로 위 순서는 FC05(WriteCoil) 등 USART2 경로에만 해당.

## 3. 타임아웃 / decode fail이 나는 단계

- **가능성 1 – 응답 잘림**  
  UART1에서 응답을 보낸 직후 DE를 바로 RX로 전환해, 마지막 1~2바이트가 나가기 전에 라인이 수신 모드로 바뀐 경우.  
  → **수정**: 응답 송신 후 **TC 대기** 추가로 해소.

- **가능성 2 – 처리 실패 시 무응답**  
  `handle_fc06`가 -1 반환 등으로 `resp_len <= 0`인데, 기존에는 아무 응답도 보내지 않음.  
  → **수정**: FC02/03/05/06/0F 실패 시 **예외 응답(0x86 0x04)** 전송으로 해소.

- **가능성 3 – CRC/길이/슬레이브 ID**  
  기대 길이 8이 아니거나, CRC 오류이거나, slave ID 불일치면 `process_modbus_frame` 자체가 호출되지 않아 응답 없음.  
  → 이 경우는 수정 범위 밖(프레임이 잘못된 것). FC06_DEBUG_LOG로 “[GW] FC06 received”가 찍히는지 보면, 이 단계까지 도달 여부를 구분할 수 있음.

## 4. 수정 전/후 로그 비교 (예시)

**수정 전 (문제 시):**
- PC: `[MAIN] TX FC06 addr=2101 val=1 unit=9` → `[MAIN] RX ERR` → “No Response received / Unable to decode response”
- 메인보드: (UART1에 별도 로그 없음 또는 DE 전환으로 인한 응답 잘림)

**수정 후 (정상):**
- PC: `[MAIN] TX FC06 addr=2101 val=1 unit=9` → `[MAIN] RX OK` → FC06 정상 응답
- 메인보드(FC06_DEBUG_LOG=1, 별도 시리얼):  
  `[GW] FC06 received from PC addr=2101 val=1`  
  `[GW] FC06 mapped to local (no sub) addr=2101 value=1`  
  `[GW] sending response to PC len=9`  
  `[GW] response HEX: 09 06 08 35 00 01 ...`

**수정 후 (실패 시 예외 응답):**
- handle_fc06 실패 등으로 resp_len<=0이면, PC는 0x86 0x04 예외 프레임을 수신하고 “No Response”가 아닌 **exception 0x04**로 표시됨.

## 5. 성공 시 Raw HEX 프레임 예시

- **요청 (PC → Mainboard, 8바이트)**  
  `09 06 08 35 00 01 XX XX`  
  - 09: slave id  
  - 06: FC06  
  - 08 35: addr 0x0835 = 2101  
  - 00 01: value 1  
  - XX XX: CRC16 (LSB first)

- **정상 응답 (Mainboard → PC, 9바이트)**  
  `09 06 08 35 00 01 XX XX`  
  - 09: slave id  
  - 06: FC06 echo  
  - 08 35: addr  
  - 00 01: value  
  - XX XX: CRC16

## 6. HPSB only / LPSB 주소 2 테스트

- **HPSB 단일보드**: slave 1만 연결, PC에서 HPSB RELAY1 ON/OFF(FC05 coil 0)만 반복.  
  TX frame / RX frame / fail reason은 기존 `[GW]`·`[SUB]` 로그(GATEWAY_WRITE_DEBUG_LOG=1) 및 MODBUS_MASTER_DEBUG_LOG로 확인.
- **LPSB 주소 2**:  
  - LPSB 펌웨어는 **ID_BIT(PB0/PB1/PB3)**으로 런타임 slave 주소 2/4/8 선택 가능.  
  - ID_BIT1만 HIGH → 2, ID_BIT2만 HIGH → 4, ID_BIT3만 HIGH → 8.  
  - 주소 2 한 장만 테스트할 때는 ID_BIT1만 HIGH로 배선/점퍼 후, 해당 보드만 연결하고 FC05/FC06 등으로 동작 확인.

## 7. 변경된 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `Application/Inc/app_config.h` | FC06_DEBUG_LOG 매크로 추가 (기본 0) |
| `Application/Inc/gateway_write_log.h` | FC06 로그 함수 선언 추가 |
| `Application/Src/modbus_master_log.c` | FC06 로그 구현 (Gateway_LogFc06* ) |
| `Gateway/Src/upstream_slave_h2tech.c` | handle_fc06 내 FC06 수신/로컬 매핑 로그 호출 |
| `Application/Src/upstream_slave_uart1.c` | FC06 실패 시 예외 응답 전송, 응답 송신 직전 FC06 로그, UART1 TX 후 TC 대기 후 set_de_rx() |
