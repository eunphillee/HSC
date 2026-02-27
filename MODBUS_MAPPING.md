# HSC Modbus Address Mapping Table

**Bit ordering:** LSB-first within each byte. Byte grouping: 8 bits per byte.  
**Register byte order:** High byte first per 16-bit register (Modbus standard).

---

## 1. HPSB (High Power Sub Board) — Slave Address 1

| Area        | Modbus Type | FC   | Start Addr | Count | Content (LSB = bit 0) |
|------------|-------------|------|------------|-------|------------------------|
| Coils      | 0x          | 01/05/15 | 0   | 8  | Coil0=RLY_EN01, Coil1=RLY_EN02, Coil2=RLY_EN03, Coil3–7=reserved(0) |
| Discrete   | 1x          | 02   | 0   | 8  | Bit0=ID_BIT1, Bit1=ID_BIT2, Bit2=ID_BIT3, Bit3=ID_BIT4, Bit4–7=reserved(0) |
| Holding    | 4x          | 03/06/16 | 0   | 4  | Reg0=Status, Reg1=Alarm, Reg2–3=Reserved |
| Input Regs | 3x          | 04   | 0   | 4  | Reg0=Discrete_image(8bit), Reg1=ADC_ch0, Reg2=ADC_ch1, Reg3=ADC_ch2 (HCT17W current raw, 12-bit) |

**Coil response (FC01) example — 8 coils, 1 byte:**  
`[Byte0]` = Coil0 | (Coil1<<1) | (Coil2<<2) | ... (Coil7<<7). LSB = Coil0.

**Discrete response (FC02) example — 8 inputs, 1 byte:**  
`[Byte0]` = DI0 | (DI1<<1) | ... (DI7<<7). LSB = Discrete0.

---

## 2. LPSB (Low Power Sub Board) — Slave Address 2

| Area        | Modbus Type | FC   | Start Addr | Count | Content (LSB = bit 0) |
|------------|-------------|------|------------|-------|------------------------|
| Coils      | 0x          | 01/05/15 | 0   | 8  | Coil0=SSR1_EN, Coil1=SSR2_EN, Coil2=SSR3_EN, Coil3–7=reserved(0) |
| Discrete   | 1x          | 02   | 0   | 8  | Bit0=ID_BIT1, Bit1=ID_BIT2, Bit2=ID_BIT3, Bit3=ID_BIT4, Bit4–7=reserved(0) |
| Holding    | 4x          | 03/06/16 | 0   | 4  | Reg0=Status, Reg1=Alarm, Reg2–3=Reserved |
| Input Regs | 3x          | 04   | 0   | 2  | Reg0=Discrete_image(8bit), Reg1=Current sense (see below) |

Bit packing same as HPSB (LSB-first, 8 bits per byte).

**LPSB InputReg[1] (Current sense):** Single 16-bit register. Meaning: ADC raw from ACS712 (or equivalent) current sensor. Typically 12-bit ADC; value is **unscaled raw count**. For ACS712 centered at mid-supply (e.g. 3.3V/2): 0A corresponds to mid-scale raw (e.g. 2048 for 12-bit). MAIN uses this value with configurable threshold; optional offset/scale: `effective_margin = |raw - LPSB_SENSE_MIDSCALE|`, alarm if `effective_margin > LPSB_OC_THRESHOLD_RAW`. Averaging (if any) is done on the LPSB before placing in Reg1.

---

## 3. MAIN Board — Master Polling View

MAIN uses enum-based addresses (no magic numbers). Polling reads from slaves:

| Source   | Content read by MAIN                    | FC  | Addr  | Count |
|----------|-----------------------------------------|-----|-------|-------|
| HPSB     | Discrete inputs (DI image)              | 02  | 0     | 8     |
| HPSB     | Coils (relay status)                    | 01  | 0     | 8     |
| HPSB     | Holding (Status, Alarm)                  | 03  | 0     | 4     |
| HPSB     | Input regs (ADC ch0–2)              | 04  | 0     | 4     |
| LPSB     | Discrete inputs                         | 02  | 0     | 8     |
| LPSB     | Coils (SSR status)                      | 01  | 0     | 8     |
| LPSB     | Holding (Status, Alarm)                  | 03  | 0     | 4     |
| LPSB     | Input regs (optional)                    | 04  | 0     | 2     |

MAIN writes: FC05/15 for Coils, FC06/16 for Holding (e.g. control commands).

---

## 4. Enum-Based Address Definitions (in code)

- **MAIN:** `SlaveId_t` (HPSB=1, LPSB=2), `PollType_t` (ReadDiscrete, ReadCoil, ReadHolding, ReadInputReg, WriteCoil, WriteHolding), and start/count from this table via enums or constants — no raw 0/1/2 in logic.
- **HPSB/LPSB:** `CoilIdx_t`, `DiscreteIdx_t`, `HoldingRegIdx_t`, `InputRegIdx_t` with counts `COIL_COUNT`, `DISCRETE_COUNT`, `HOLDING_REG_COUNT`, `INPUT_REG_COUNT`. Address 0-based; coil 0 = first coil, etc.

---

## 5. Implementation Files (Mapping Table Structure)

| Board | File | Role |
|-------|------|------|
| MAIN | IO/Inc/io_map.h | `SlaveId_t`, `PollType_t`, `MainDiChannel_t`, `MainDoChannel_t`, `HoldingRegIdx_t`, `CoilIdx_t`; constants `MODBUS_*_START`, `MODBUS_*_COUNT` |
| MAIN | Modbus/Src/modbus_table.c | Poll table array, per-slave image buffers |
| MAIN | Modbus/Src/modbus_master.c | One transaction per `ModbusMaster_Poll()` |
| HPSB | IO/Inc/io_map.h | `HpsbCoilIdx_t`, `HpsbDiscreteIdx_t`, etc.; COIL/DISCRETE/HOLDING/INPUT counts |
| HPSB | Modbus/Src/modbus_table.c | Coil/Discrete from IO; Holding/Input Reg in RAM |
| HPSB | Modbus/Src/modbus_slave.c | FC01–04/05/06/15/16; LSB-first coil/discrete bytes |
| LPSB | IO/Inc/io_map.h | `LpsbCoilIdx_t`, etc. (SSR instead of RLY) |
| LPSB | Modbus/Src/modbus_slave.c | Same as HPSB, slave address 2 |

