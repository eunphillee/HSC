# Gateway Write Path (PC → Mainboard → HPSB/LPSB)

## Topology

- **PC** is connected to **Mainboard upper RS485** (UART1, DE=PB1). PC talks only to Mainboard **unit 9**.
- **Mainboard** talks to **HPSB/LPSB** on **lower RS485** (UART2, DE/RE=**PB12**, MAX3485).
- PC does **not** connect directly to HPSB/LPSB. All subboard control writes go through Mainboard.

## Lower bus slave IDs

| Board  | Slave ID |
|--------|----------|
| HPSB   | 1        |
| LPSB1  | 2        |
| LPSB2  | 4        |
| LPSB3  | 8        |

## Write path (FC05)

1. **PC** sends **FC05** to **Mainboard (unit 9)** with:
   - **Address 898, 899, 900** → HPSB coil 0, 1, 2  
   - **Address 901..909** → LPSB1/2/3 coil 0,1,2 (901–903=LPSB1, 904–906=LPSB2, 907–909=LPSB3)
   - **Value** 0 (OFF) or 1 (ON); Modbus frame uses 0xFF00 / 0x0000.

2. **Mainboard** receives on UART1:
   - `upstream_slave_uart1.c` → `process_modbus_frame` → `UpstreamSlave_HandleRequest(0x05, start_addr, 0, write_data, ...)`.

3. **upstream_slave_h2tech.c** `handle_fc05`:
   - `h2_dec = H2Map_ModbusAddrToH2Dec(start_addr)` (= start_addr + 1).
   - Find H2 entry for 1x0899..0910 (e.g. WR_HPSB_COIL_0, WR_LPSB1_COIL_0, ...).
   - `value = (write_data[0] != 0)`.
   - `H2Map_ApplyWrite(e, value, PULSE_MS_DEFAULT)`.

4. **h2tech_address_map.c** `H2Map_ApplyWrite` for `H2_ACT_WRITE_SUB_COIL`:
   - `h2_dec_to_sub_coil(e->h2_dec, &slave_id, &coil_index)` → (1/2/4/8, 0/1/2).
   - `Gateway_Action_WriteSubCoil(slave_id, coil_index, value)`.

5. **gateway_actions.c** `Gateway_Action_WriteSubCoil`:
   - Validates slave_id (1,2,4,8) and coil_index (0..7).
   - **ModbusMaster_WriteCoil(slave_id, coil_index, value)** → builds FC05 PDU and sends on **UART2**.

6. **modbus_master.c** `ModbusMaster_WriteCoil`:
   - **PB12 (DE/RE)**: `set_de_tx()` → 1 ms settle → `HAL_UART_Transmit(UART2, pdu, 8)` → 2 ms guard → `set_de_rx()`.
   - **Wait for FC05 response** (8 bytes, timeout 60 ms). Success only if valid response received (subboard applied command).
   - Return 0 only when TX and RX both OK; else -1 (PC gets exception 0x85).

## HPSB RELAY1 mapping (verification)

- PC sends **FC05 addr=898** val=1.
- Mainboard: `h2_dec = 898+1 = 899` → entry **WR_HPSB_COIL_0** → `h2_dec_to_sub_coil(899)` → **slave_id=1, coil_index=0**.
- Mainboard sends on UART2: **FC05 slave=1, coil_addr=0, value=1**.
- HPSB (slave 1): receives FC05, `ModbusTable_SetCoil(0, 1)` → `IO_HPSB_WriteCoil(0, 1)` → RELAY1 ON; LED2 follows in `led_status.c` (LED_DIAG_COMM_OUTPUT=1).

## HPSB RELAY1 EN path verification (6 items)

| # | 항목 | 구현 위치 | 비고 |
|---|------|------------|------|
| 1 | PC 명령 수신 후 HPSB RELAY1 EN 처리 분기 | `upstream_slave_h2tech.c` `handle_fc05()`: start_addr=898 → h2_dec=899 → `H2Map_FindByDec(1X,899)` → entry WR_HPSB_COIL_0, `e->action == H2_ACT_WRITE_SUB_COIL` → `H2Map_ApplyWrite` | 분기 존재 |
| 2 | slave=1 대상 Modbus write 프레임 생성 | `gateway_actions.c` `Gateway_Action_WriteSubCoil(1, 0, value)` → `modbus_master.c` `ModbusMaster_WriteCoil(1, 0, value)` → `ModbusRTU_BuildFC05(pdu, 1, coil_addr, value)` | FC05 PDU 8바이트 생성 |
| 3 | UART2 송신 함수 호출 | `modbus_master.c` `ModbusMaster_WriteCoil`: `HAL_UART_Transmit(&MODBUS_UART, pdu, len+2, 100)` (MODBUS_UART = huart2) | 호출됨 |
| 4 | 송신 전후 PB12(DE/RE) 제어 | `modbus_master.c`: TX 전 `set_de_tx()` (PB12=HIGH), TX 후 `DE_RX_GUARD_MS`(2ms) 대기 후 `set_de_rx()` (PB12=LOW). `modbus_cfg.h`: MODBUS_DE_GPIO = RS_485_DE_RE (PB12) | 제어됨 |
| 5 | TX complete 이후 DE LOW | `set_de_rx()` 가 `HAL_UART_Transmit` 직후 2ms 뒤 호출됨 → PB12 LOW (수신 모드) | 처리됨 |
| 6 | [GW] 로그 (UART1 출력) | `app_config.h` GATEWAY_WRITE_DEBUG_LOG=1 시 `modbus_master_log.c`에서 UART1(huart1)로 전송. 순서: command parsed → (resolved target) → UART2 TX start → UART2 TX done → UART2 RX timeout/OK → final result | 로그 추가됨 |

## Debug log (UART1)

Set **GATEWAY_WRITE_DEBUG_LOG 1** in **app_config.h** (default for lower-bus debug). Watch **UART1** (PC 연결 포트와 동일; 별도 터미널/캡처 필요):

1. `[GW] command parsed: FC05 addr=898 val=1`
2. `[GW] resolved target board=HPSB slave_id=1 FC=05 coil=0 val=1`
3. `[GW] UART2 TX start: FC05 slave=1 (HPSB) coil=0 val=1`
4. `[GW] USART2 TX done`
5. `[GW] UART2 RX OK` or `[GW] UART2 RX timeout or invalid`
6. `[GW] final gateway result OK` or `final gateway result FAIL`

- **RX timeout** → Mainboard did not receive reply from HPSB (wiring, baud, HPSB not running, or DE/RE timing).
- **Final FAIL** → PC gets exception; do not treat upper-bus RX OK as success.

## DE/RE (PB12) timing

- **modbus_cfg.h**: `MODBUS_DE_TX_SETTLE_MS` (1) = delay after DE=TX before first byte.
- **modbus_master.c**: `DE_RX_GUARD_MS` (2) = delay after last byte before DE=RX.
- PB12 = **RS_485_DE_RE** in CubeMX; active **high** = TX, **low** = RX.

## If HPSB LED2 does not turn on

1. Confirm all six log lines; note whether **USART2 RX received OK** or **timeout**.
2. **RX timeout**: Lower bus problem (Mainboard UART2/RS485, wiring, HPSB UART1, baud 9600, HPSB slave ID 1).
3. **RX OK** but no LED2: HPSB applied coil but LED not linked (check HPSB `LED_DIAG_COMM_OUTPUT=1`, `led_status.c` LED2 ← `IO_HPSB_ReadCoil(0)`).
4. **TX result FAIL**: UART2 not initialized or DE (PB12) not driven.
