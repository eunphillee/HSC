# FC05 Gateway 상태/cleanup 및 0x04·FC03 실패 원인 보고서

## 1. exception 0x04 생성 지점

| 위치 | 설명 |
|------|------|
| **Mainboard 자체 생성** | `Guro_Mainboard/Gateway/Src/upstream_slave_h2tech.c` 의 `handle_fc05()` 에서, `H2Map_ApplyWrite(e, value, ...)` 가 **false** 를 반환할 때 `response[1] = EX_SLAVE_DEVICE_FAIL`(0x04) 로 설정 후 return 2. |
| **HPSB에서 0x04 반환 시** | HPSB가 예외 프레임(0x81 0x04 …)을 보내면, Mainboard `ModbusMaster_WriteCoil()` 의 `HAL_UART_Receive` 로 수신하고, `ModbusRTU_ValidateFC05Response()` 가 실패하여 `rx_ok = 0` → `Gateway_Action_WriteSubCoil` 이 -1 반환 → `H2Map_ApplyWrite` false → 위와 동일하게 **Mainboard가 PC에게 0x85 0x04** 를 보냄. 즉, 서브보드 예외는 “하위버스 쓰기 실패”로 간주되어 동일한 0x04 응답으로 PC에 전달됨. |

**구분 방법 (FC05_GW_STEP_LOG=1 시)**  
- **로컬 0x04**: `[GW] local exception 0x04` → `[GW] before sending exception to PC exc=0x04` 순서로 출력.  
- **서브보드 예외**: `[GW] USART2 rx exception byte=0x85`(또는 0x84 등) → `[GW] subboard returned exception 01 85 02 ...` (raw RX hex) 출력 후, ApplyWrite 실패로 동일하게 0x04 응답 전송.

**정리**: 0x04는 **항상 Mainboard가 PC로 보내는 값**이며, “주소는 유효하나 하위버스/게이트웨이 쓰기 실패”를 의미. 서브보드가 0x02(Illegal Data Address) 등을 보내도 Mainboard는 이를 실패로 보고 0x04로 통일해 PC에 전달.

---

## 2. FC05 실패 후 FC03까지 실패하는 이유 (가설 및 수정 방향)

**가설**  
1. **블로킹 시간**: `ModbusMaster_WriteCoil()` 내부 `HAL_UART_Receive(..., MODBUS_FC05_RX_TIMEOUT_MS)`(기존 120ms) 동안 Mainboard가 블로킹되어, 그 사이 도착한 UART1(PC) FC03 프레임이 제때 처리되지 않거나, 처리 직후 상태가 꼬임.  
2. **USART2 RX 미정리**: FC05 타임아웃/실패 후 USART2 RX에 남은 0~7바이트가 다음 폴링과 섞이거나, 폴 상태기(rx_len 등)가 이전 데이터를 갖고 있음.  
3. **busy 플래그**: `s_write_in_progress` 가 예외 경로에서 한 번이라도 해제되지 않으면, 이후 `ModbusMaster_Poll()` 이 계속 return 하여 폴이 멈추고, 간접적으로 다른 처리에 영향을 줄 수 있음 (단, 기존 코드는 WriteCoil 단일 return 경로에서만 0 클리어).

**수정 사항**  
- **타임아웃 단축**: `MODBUS_FC05_RX_TIMEOUT_MS` 를 120 → **80ms** 로 줄여 블로킹 구간 축소.  
- **트랜잭션 후 cleanup**: `ModbusMaster_WriteCoil()` 종료 전 **무조건**  
  - `uart2_flush_rx()`  
  - `s_write_in_progress = 0`  
  실행. 성공/실패/타임아웃/서브보드 예외와 관계없이 단일 퇴장 경로에서만 반환하도록 유지.  
- **단계 로그**: `FC05_GW_STEP_LOG=1` 로 빌드 시 `[GW] cleanup done / busy flag clear` 로 cleanup 실행 여부 확인 가능.

---

## 3. cleanup 누락 지점 (점검 결과 및 조치)

| 항목 | 점검 결과 | 조치 |
|------|-----------|------|
| **s_write_in_progress** | `ModbusMaster_WriteCoil()` 에서 설정(1) 후 해제(0)는 **한 return 경로** 뿐. early return 없음. | 해제 직전에 `uart2_flush_rx()` 추가하고, 그 다음 `s_write_in_progress = 0` 실행하도록 이미 수정됨. |
| **state** | WriteCoil 진입 시 `state = MST_IDLE` 로 폴 트랜잭션 취소. Poll은 IDLE 시 poll_index=0 부터 send_request. | 추가 변경 없음. |
| **USART2 RX** | 타임아웃/실패 시 수신 버퍼에 일부만 찬 데이터가 남을 수 있음. | WriteCoil 퇴장 전 **항상** `uart2_flush_rx()` 호출. |
| **UART1 slave task** | `process_modbus_frame()` 은 무상태(static 버퍼만 사용). FC05 처리 후 정상 return 하면 다음 Poll에서 FC03 처리 가능. | FC05 경로에서 블로킹만 줄이고 cleanup을 보장해 두었음. |

**추가 확인**: `ModbusMaster_Poll()` 은 `if (s_write_in_progress) return;` 으로, gateway write 중에는 USART2를 건드리지 않음. WriteCoil 반환 후에는 항상 flush + busy clear 하므로, 다음 Poll부터 정상 동작.

---

## 4. 수정 전/후 로그 (FC05_GW_STEP_LOG=1 기준)

**수정 전 (예상)**  
- FC05 실패 시: `[GW]` 로그 없거나 기존 GATEWAY_WRITE_DEBUG_LOG 수준만.  
- 0x04가 로컬인지 서브보드 예외인지 구분 불가.  
- cleanup 실행 여부 확인 불가.

**수정 후 (FC05 실패 – 타임아웃 예)**  
```
[GW] FC05 recv from PC
[GW] raw coil addr=898 value=1
[GW] mapping result: target slave=1 fc=5 sub_addr=0
[GW] before USART2 tx
[GW] after USART2 tx complete
[GW] before USART2 rx wait
[GW] USART2 rx timeout
[GW] local exception 0x04
[GW] before sending exception to PC exc=0x04
[GW] cleanup done / busy flag clear
```

**수정 후 (서브보드가 예외 0x85 0x02 반환 시)**  
```
...
[GW] before USART2 rx wait
[GW] USART2 rx exception byte=0x85
[GW] subboard returned exception 01 85 02 XX XX XX XX XX
[GW] local exception 0x04
[GW] before sending exception to PC exc=0x04
[GW] cleanup done / busy flag clear
```

**수정 후 (FC05 성공)**  
```
[GW] FC05 recv from PC
[GW] raw coil addr=898 value=1
[GW] mapping result: target slave=1 fc=5 sub_addr=0
[GW] before USART2 tx
[GW] after USART2 tx complete
[GW] before USART2 rx wait
[GW] USART2 rx ok
[GW] before sending normal response to PC
[GW] cleanup done / busy flag clear
```

---

## 5. FC05 실패 후에도 FC03 정상 유지되는 검증

**확인 절차**  
1. Mainboard 빌드: `FC05_GW_STEP_LOG=0` (또는 1로 로그 확인용).  
2. PC 툴에서 HPSB RELAY1 ON(FC05 addr=898) 전송 → HPSB 미연결 또는 전원 OFF 로 의도적으로 실패 유도.  
3. PC에서 **즉시** Mainboard FC03 read(unit=9, 예: addr 2110, count 3) 전송.  
4. **기대**: FC05에 대해 0x85 0x04 수신 후, FC03에 대해 정상 응답(0x03 + 데이터) 수신.  
5. **로그**: `[GW] cleanup done / busy flag clear` 직후 다음 프레임(FC03)이 처리되면, UART1 slave는 정상 응답 가능.

**구조적 보장**  
- FC05 처리 후 `process_modbus_frame()` 이 정상 return.  
- 다음 `UpstreamSlaveUart1_Poll()` 호출에서 UART1 ring에 쌓인 FC03이 한 프레임씩 처리됨.  
- FC05 경로에서 블로킹 구간을 80ms로 제한하고, 퇴장 시 항상 USART2 flush + busy clear 하므로, FC03 수신/처리 지연만 최소화되면 “FC05 실패 후 FC03까지 실패” 현상은 완화되는 구조.

---

## 6. USART2 트랜잭션 공통화

- **함수**: `uart2_transaction_tx(tx_buf, tx_len)`  
  - 내부: `uart2_flush_rx()` → `uart2_tx()` (DE HIGH → settle → TX → TC wait → DE LOW).  
- **WriteCoil 흐름**:  
  - `uart2_transaction_tx()` 호출  
  - 바로 이어서 `HAL_UART_Receive(..., MODBUS_FC05_RX_TIMEOUT_MS)`  
  - 그 다음 **항상** `uart2_flush_rx()` → `s_write_in_progress = 0` → (옵션) `Gateway_LogFc05StepCleanupDone()`.

추가로 WriteHoldingReg 등 다른 gateway write에서도 동일한 `uart2_transaction_tx()` + RX 대기 + flush + busy clear 패턴을 쓰면, “트랜잭션 후 cleanup”을 한 곳에서 유지할 수 있음.

---

## 7. HPSB 릴레이 제어 (참고)

- **HPSB**: `Modbus/Src/modbus_slave.c` 에서 FC05 수신 시 `coil_addr >= COIL_COUNT` 이면 break(응답 안 함).  
- **io_map**: coil index 0/1/2 → RLY_EN01/02/03 GPIO.  
- **0x04**: HPSB는 0x02(Illegal Data Address) 등만 반환 가능하며, Mainboard가 하위버스 실패를 0x04로 통일해 PC에 전달.

---

## 8. 요약

| 항목 | 내용 |
|------|------|
| **0x04 생성** | Mainboard `handle_fc05()` 에서 ApplyWrite 실패 시에만 설정. 서브보드 예외는 “실패”로 간주되어 동일 0x04로 PC에 전달. |
| **FC05 실패 후 FC03 실패** | 블로킹 구간 단축(80ms), 퇴장 전 USART2 flush + busy clear 로 상태 꼬임 방지. |
| **cleanup** | WriteCoil 단일 퇴장 경로에서 `uart2_flush_rx()` + `s_write_in_progress = 0` 필수 실행. |
| **로그** | `FC05_GW_STEP_LOG=1` 로 로컬 0x04 vs 서브보드 예외(raw hex) 및 cleanup 여부 확인. |
