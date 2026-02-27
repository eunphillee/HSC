# Current Register Map — HSC Gateway

Per-port current (CT/ACS) is sent from HPSB/LPSB to MAIN via Modbus, then MAIN exposes it to PC on request. No change to existing 1x (discrete) addressing.

---

## 1. RAW value definition

- **Format:** `uint16_t` (0 .. 65535).
- **v1 meaning:** **ADC raw** (e.g. 0 .. 4095 for 12-bit). Unscaled; no conversion on the board.
- **HPSB (CT):** One raw value per port (ch1..ch3). Resolution and scaling (e.g. mA per LSB) are application-specific; document in hardware notes.
- **LPSB (ACS712):** One raw value per port (ch1..ch3). Typically 12-bit ADC; mid-scale (e.g. 2048) = 0 A. MAIN may apply offset/scale for alarm (see aggregator).
- **Future:** Scaled or RMS values (e.g. RMS_x100) can use additional registers; v1 uses raw only.

---

## 2. Downstream (HPSB/LPSB → MAIN)

### 2.1 HPSB (Slave ID = 1)

| Area        | Type   | FC  | Start | Count | Content |
|-------------|--------|-----|-------|-------|---------|
| Coils       | 0x     | 01/05/15 | 0 | 8  | 0..2 = relay enable Port1~3 (existing) |
| Holding     | 4x     | 03/06/16 | 0 | 4  | Reg0=Status, Reg1=Alarm (existing) |
| Input Regs  | 3x     | 04  | 0 | 7  | See below |

**Input Registers (3x):**

| Reg | Name             | Description |
|-----|------------------|-------------|
| 0   | DI image         | Optional; 8-bit discrete image |
| 1   | CT_CH1_RAW       | Port1 current raw (ADC raw) |
| 2   | CT_CH2_RAW       | Port2 current raw |
| 3   | CT_CH3_RAW       | Port3 current raw |
| 4   | CT_CH1_RMS_x100  | Optional; 0 for v1 |
| 5   | CT_CH2_RMS_x100  | Optional; 0 for v1 |
| 6   | CT_CH3_RMS_x100  | Optional; 0 for v1 |

**Minimum:** InputReg 1..3 must exist and be readable by FC04.

### 2.2 LPSB (Slave IDs = 2, 3, 4; expandable)

| Area        | Type   | FC  | Start | Count | Content |
|-------------|--------|-----|-------|-------|---------|
| Coils       | 0x     | 01/05/15 | 0 | 8  | 0..2 = SSR enable Port1~3 (existing) |
| Holding     | 4x     | 03/06/16 | 0 | 4  | Reg0=Status, Reg1=Alarm (optional/legacy) |
| Input Regs  | 3x     | 04  | 0 | 4  | See below |

**Input Registers (3x):**

| Reg | Name          | Description |
|-----|---------------|-------------|
| 0   | DI image      | Optional; 8-bit discrete image |
| 1   | ACS_CH1_RAW   | Port1 current raw (ADC raw) |
| 2   | ACS_CH2_RAW   | Port2 current raw |
| 3   | ACS_CH3_RAW   | Port3 current raw |

**Minimum:** InputReg 1..3 must exist and be readable by FC04.

### 2.3 MAIN polling

- **HPSB:** FC04, start 0, count 7 (every 100 ms–500 ms in poll loop).
- **LPSB1/2/3:** FC04, start 0, count 4 each.
- **Storage:** `out->hpsb_sense_raw[3]`, `out->lpsb1_sense_raw[3]`, `out->lpsb2_sense_raw[3]`, `out->lpsb3_sense_raw[3]`.

---

## 3. Upstream (MAIN → PC)

PC reads per-port current via **Holding Registers (4x)**. Address range is fixed and stable.

**Block:** 4x**2000** .. 4x**200D** (Modbus register start address **2000**, count **14**). Read with **FC03**. **Read-only**; write (FC06/FC16) returns exception **0x03**.

**FC03 current read must be requested only as start=2000, count=14.** Any other start or count is rejected (0x02 Illegal Data Address if start≠2000, 0x03 Illegal Data Value if count≠14). This simplifies the PC test tool and avoids partial-range ambiguity.

| Reg (4x) | Modbus start + offset | Content |
|----------|------------------------|---------|
| 2000     | 0                      | HPSB Port1 current raw |
| 2001     | 1                      | HPSB Port2 current raw |
| 2002     | 2                      | HPSB Port3 current raw |
| 2003     | 3                      | LPSB1 Port1 current raw |
| 2004     | 4                      | LPSB1 Port2 current raw |
| 2005     | 5                      | LPSB1 Port3 current raw |
| 2006     | 6                      | LPSB2 Port1 current raw |
| 2007     | 7                      | LPSB2 Port2 current raw |
| 2008     | 8                      | LPSB2 Port3 current raw |
| 2009     | 9                      | LPSB3 Port1 current raw |
| 200A     | 10                     | LPSB3 Port2 current raw |
| 200B     | 11                     | LPSB3 Port3 current raw |
| 200C     | 12                     | MAIN DOOR1 current (0 if none) |
| 200D     | 13                     | MAIN DOOR2 current (0 if none) |

**Extend later:** LPSB4..9 can use 4x200E.. (3 regs per unit) as needed.

---

## 4. ADC / resolution assumptions (v1)

- **HPSB CT:** 12-bit ADC assumed; raw 0..4095. Adjust threshold and scaling in MAIN if different.
- **LPSB ACS712:** 12-bit ADC assumed; mid-scale 2048 = 0 A. MAIN uses `LPSB_SENSE_MIDSCALE` and `LPSB_OC_THRESHOLD_RAW` for overcurrent (both polarities).
- **Byte order:** Modbus high-byte-first per register.

---

## 5. Implementation summary

| Board  | File / change |
|--------|----------------|
| HPSB   | InputReg 1..3 = CT raw; 4..6 = RMS optional (0). `ModbusTable_RefreshInputRegs()` fills from ADC (or stub). |
| LPSB   | InputReg 1..3 = ACS raw. Same pattern. |
| MAIN   | Poll FC04 HPSB 7 regs, LPSB 4 regs; fill `*_sense_raw[3]`. FC03 4x2000 count 14 from aggregated status. |

Existing 1x (discrete) mapping is unchanged. This document defines only the current-related registers.
