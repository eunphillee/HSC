# PC Test Checklist — MAIN Upstream (MAIN as Modbus Slave)

Short checklist for PC-side tests when MAIN is connected as Modbus slave (upstream link).

---

- **Read FC03 2000/14** to verify currents: send FC03 (Read Holding Registers) with start address **2000**, count **14**. Response is 14 registers: HPSB P1..P3, LPSB1 P1..P3, LPSB2 P1..P3, LPSB3 P1..P3, DOOR1, DOOR2. Only this exact request (start=2000, count=14) is accepted; other start or count returns exception.

---

## MAIN I/O (4x2100, 4x2101) — Hardware test checklist

1. **Connect:** RS485 to MAIN upstream (USART2). In PC Test Tool press **Refresh**, select the correct port, click **Connect**.
2. **MAIN I/O panel — DI:**
   - Press **PC_RESET** button on board → **DI1** LED in tool should change.
   - Press **PC_ON** button on board → **DI2** LED should change.
3. **MAIN I/O panel — DO:**
   - Toggle **DO1 (PC_RESET_EN)** → observe output on board.
   - Toggle **DO2 (PC_ON_EN)** → observe output.
   - Toggle **DO3 (PC_LED)** → LED should change.
4. **Log:** Confirm log records **FC03 2100 2** (DI/DO bitmap) and **FC06 2101** (write) with no exception. If FC06 fails, log shows exception decode (e.g. 0x02 — Illegal Data Address).
