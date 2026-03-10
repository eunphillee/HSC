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
   - **PB12 (DE/RE)**: `set_de_tx()` → optional 1 ms settle (`MODBUS_DE_TX_SETTLE_MS`) → `HAL_UART_Transmit(UART2, pdu, 8)` → 2 ms guard → `set_de_rx()`.
   - Subboard receives FC05 and toggles relay/SSR (hardware-dependent).

## Debug log (UART1)

Set in **app_config.h**:

```c
#define GATEWAY_WRITE_DEBUG_LOG  1
```

Rebuild and watch **UART1** (same as PC link; use a second terminal or log capture). You will see:

- `[GW] upstream FC05 addr=X val=Y` — request received from PC.
- `[GW] map target slave=Z coil=W val=V (FC05)` — mapped to sub slave/coil.
- `[GW] UART2 TX start FC05 slave=Z coil=W val=V` — before UART2 send.
- `[GW] UART2 TX result OK` or `[GW] UART2 TX result FAIL` — after send.

## DE/RE (PB12) timing

- **modbus_cfg.h**: `MODBUS_DE_TX_SETTLE_MS` (default 1) = delay after DE=TX before first byte.
- **modbus_master.c**: `DE_RX_GUARD_MS` (2) = delay after last byte before DE=RX.
- PB12 = **RS_485_DE_RE** in CubeMX; active **high** = TX, **low** = RX.

## If relay/SSR does not toggle

1. Enable **GATEWAY_WRITE_DEBUG_LOG** and confirm all four log lines (upstream, map, TX start, TX result OK).
2. If **TX result FAIL**: UART2 not initialized, wrong port, or bus conflict.
3. If **TX result OK** but no hardware change:
   - Subboard coil addressing (0-based vs 1-based).
   - Subboard relay/SSR logic (active-high vs active-low).
   - Wiring and power to HPSB/LPSB.
