# HSC Gateway — Minimal Test Checklist

Minimal verification steps for MAIN (gateway) firmware. No architecture changes; safety and data-path only.

---

## 1. FC02 Read 0821~0836 (ON/OFF status)

- **Goal:** PC reads discrete inputs 1x0821–1x0836 and gets aggregated ON/OFF bits.
- **Steps:**
  1. Connect PC as Modbus master to MAIN (upstream UART).
  2. Send **FC02** (Read Discrete Inputs): start address **821**, count **16**.
  3. Verify response: byte count 2; bits map to ON/OFF 1~16 (0821=ONOFF_1 … 0836=ONOFF_16).
  4. Toggle MAIN DO, HPSB coils, LPSB coils via hardware or downstream and repeat read; confirm bits 1~14 follow state, 15~16 reserved (0).

---

## 2. FC05 Write 0897 / 0898 pulse verify

- **Goal:** PC writes single coil 0897 (Door 1) or 0898 (Door 2) and MAIN drives 300 ms pulse.
- **Steps:**
  1. Send **FC05**: address **897**, value ON (0xFF00).
  2. Observe MAIN DO for Door 1 relay: pulse ~300 ms then off.
  3. Send **FC05**: address **898**, value ON.
  4. Observe MAIN DO for Door 2 relay: pulse ~300 ms then off.

---

## 3. Overcurrent alarm simulation (raw values)

- **HPSB ALM5/6/7:** Driven by HPSB InputReg 1,2,3 (ADC ch0–ch2). Need 3 consecutive aggregator cycles above threshold.
  - **Simulate:** On HPSB, set InputReg 1 (and/or 2, 3) to a value **greater** than `HPSB_OC_THRESHOLD_RAW` (e.g. 2048) and keep it for 3+ poll cycles; or use a test mode that injects into MAIN’s Modbus image.
  - **Verify:** FC02 read 1x0873, 0874, 0875 (ALM_5, ALM_6, ALM_7) go high accordingly.
- **LPSB ALM8/9/10:** Driven by LPSB InputReg[1] with mid-scale offset; alarm if |raw − mid| > threshold.
  - **Simulate:** Set LPSB InputReg[1] (per slave) to `LPSB_SENSE_MIDSCALE + LPSB_OC_THRESHOLD_RAW + 1` or `LPSB_SENSE_MIDSCALE − LPSB_OC_THRESHOLD_RAW − 1` (e.g. 2048+2048+1 or 0).
  - **Verify:** FC02 read 1x0876, 0877, 0878 (ALM_8, ALM_9, ALM_10) go high for the corresponding LPSB.

---

## 4. Write-fail simulation → ALM12 (1x0880)

- **Goal:** Downstream WriteCoil failure sets ALM12; clear on PC read of 1x0880.
- **Steps:**
  1. Disconnect one LPSB (e.g. power or bus) so that MAIN’s WriteCoil to that slave fails.
  2. From PC, trigger a coil write that targets that LPSB (e.g. FC05/15 to toggle an output mapped to that slave). Write should fail on MAIN side.
  3. Read **FC02** start **869** count **12** (alarms 1~12). Confirm **1x0880 (ALM12)** is **1** (downstream write fail).
  4. Without fixing the bus, read again **FC02** including 0880 (e.g. start 869 count 12). After this read, ALM12 is cleared by policy (clear-on-read).
  5. Next FC02 read of 0821~0880 should show ALM12 = 0 until another write fail occurs.

---

## Reference

- **ALM12 clear policy:** Cleared when PC performs FC02 read that includes address **1x0880**. Optional: auto-clear after N seconds (if implemented).
- **HPSB current:** MAIN polls HPSB FC04 start 0 count 4; InputReg 1,2,3 = ADC ch0,1,2. `hpsb_sense_raw[3]` and OC use these.
- **LPSB sense:** InputReg[1] = current sense raw; MAIN uses `LPSB_SENSE_MIDSCALE` and `LPSB_OC_THRESHOLD_RAW` for ALM8/9/10.
