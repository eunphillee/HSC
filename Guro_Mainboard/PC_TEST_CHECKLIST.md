# PC Test Checklist — MAIN Upstream (MAIN as Modbus Slave)

Short checklist for PC-side tests when MAIN is connected as Modbus slave (upstream link).

---

- **Read FC03 2000/14** to verify currents: send FC03 (Read Holding Registers) with start address **2000**, count **14**. Response is 14 registers: HPSB P1..P3, LPSB1 P1..P3, LPSB2 P1..P3, LPSB3 P1..P3, DOOR1, DOOR2. Only this exact request (start=2000, count=14) is accepted; other start or count returns exception.
