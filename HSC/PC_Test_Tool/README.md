# PC Test Tool — HSC MAIN Board (H2TECH Modbus RTU)

Production-friendly PC test tool that talks to the MAIN board over RS485 Modbus RTU using H2TECH addressing.

## Requirements

- Python 3.11+
- RS485 adapter (USB–RS485 or built-in serial with RS485 transceiver)

## Install

From the **repository root** (e.g. `workspace_2.0.0`), or from `HSC/PC_Test_Tool`:

```bash
cd HSC/PC_Test_Tool
python3 -m venv .venv
source .venv/bin/activate   # macOS/Linux
pip install -r requirements.txt
```

**macOS — exact run from repo root:**

```bash
cd HSC/PC_Test_Tool && python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt && python main.py
```

## Run

```bash
python main.py
```

## Windows 실행 파일 (.exe)

PyInstaller로 단일 실행 파일을 만들 수 있습니다. **반드시 Windows PC에서** 빌드해야 합니다 (macOS에서 `.exe` 크로스 빌드는 불가).

1. Python 3.11+ 설치 후 `HSC/PC_Test_Tool` 폴더에서:
2. `scripts\build_windows.bat` 실행 (더블클릭 또는 명령 프롬프트)
3. 결과: `dist\HSC_PC_Test_Tool.exe` (콘솔 창 없음)

수동 빌드: `pip install -r requirements.txt -r requirements-build.txt` 후 `pyinstaller --noconfirm hsc_pc_tool.spec`

After the window opens, the **console** prints a layout verification block: main window size, layout tree (top bar, 3 left cards, 1 right card, bottom 2 cards), and that all sections are visible on 1440×900 without scrolling.

Select COM port (e.g. `COM3` on Windows, `/dev/tty.usbserial-*` or `/dev/ttyUSB0` on macOS/Linux), set **Slave ID** to match the mainboard EEPROM `slave_id` (SpinBox default **9** is the firmware default when EEPROM is unset/invalid—**not** a hardcoded bus ID), then Connect. After changing ID on the board (save to EEPROM + reboot), set the same value here and reconnect.

## UI overview (text layout)

- **Top bar (single row):** Port dropdown, Refresh, Baud 9600 (fixed), Slave ID input, Connect/Disconnect button, status badge (Connected / Disconnected), Auto Poll checkbox + interval (ms), Last poll timestamp.
- **Main area (2 columns):**
  - **Left — Status Monitor (3 stacked cards):**
    - **ON/OFF 1~16:** 4×4 grid; each cell: small LED (green/gray) + number + short name (Door1, Door2, HPSB Fan, Heated Bench, … Reserved).
    - **Door sensors:** 2×2 grid (MAG1, MAG2, BTN1, BTN2) with same LED style.
    - **Alarms 1~12:** 3×4 grid; red LED when active, blinking every 400 ms.
  - **Right — Current Monitor:** Single card with table: Name | Raw Value | Min/Max (since connect); rows: HPSB P1–P3, LPSB1 P1–P3, LPSB2 P1–P3, LPSB3 P1–P3, Door1, Door2.
- **Bottom (full width, 2 cards):**
  - **Controls:** Open Door 1, Open Door 2 (primary); Virtual Buttons 8~12 (ON/OFF toggles); Error test buttons (0899, 0900) for exception 0x02.
  - **Log:** Monospace log view, filter box, Clear and Save CSV buttons.
- **Usability:** Read Once buttons per section (Status, Door, Alarm, Currents). When disconnected, read/write controls are disabled. Dark theme with blue accent and high-contrast panels.

## Visual verification (screenshots)

1. **Main window full view** — Entire window at default size (920×720) or on a 1440×900 display: top bar, left column (3 cards), right column (Currents), bottom (Controls + Log) all visible without scrolling.
2. **Close-up: ON/OFF grid and Alarm LEDs** — Left column showing the 4×4 ON/OFF grid with LED + labels, and the 3×4 Alarms grid; if any alarm is active, the red LED should be blinking (400 ms).
3. **Current table with Min/Max** — Right column Current Monitor table with columns Name | Raw Value | Min / Max; optional numeric min/max since connect in the third column.

If any panel is clipped on 1440×900: the window has minimum size 880×700 and the main content and bottom areas have minimum heights; you can also increase the default `resize(920, 720)` in `app/ui_main.py` or add a QSplitter for the middle/bottom to adjust stretch factors.

## Serial settings

- **9600** baud, **8N1**, Modbus RTU
- User selects port; refresh to rescan

## Mainboard Slave ID (EEPROM)

- The mainboard answers Modbus RTU on USART1 using `slave_id` from EEPROM **SystemConfig** (not a fixed 9 in firmware).
- PC tool sends all mainboard requests with `unit=` = the **Slave ID** spinbox value.
- If EEPROM is blank (`0xFF`) or out of range **1–247**, firmware falls back to **9**.
- To change the board ID: write holding register **3000** (per project syscfg / Modbus map), save, **reboot** the board, then set the spinbox to the new ID and **Connect** again.

## Notes

- If you get `TypeError` about `slave` when using pymodbus 3.x, replace `slave=` with `unit=` in `app/modbus_client.py` for the pymodbus client calls.

## Protocol notes

- H2TECH “1xNNNN” uses Modbus starting address = **NNNN - 1** (e.g. 1x0821 → start 820).
- FC02 responses: LSB-first bit packing.
- Current block: **FC03** **start=2000, count=40** (HPSB+LPSB×3 각 AVG/PKPK/CURRENT + reserved). Other start/count → exception 0x02/0x03.

## RS485 adapter

- Ensure the adapter’s driver is installed (e.g. FTDI, CH340, CP210x).
- On Windows, the port appears as `COMn`; on macOS as `/dev/tty.usbserial-*` or similar; on Linux as `/dev/ttyUSB0` or `/dev/ttyACM0`.
- Do not hardcode the port name; use the dropdown and refresh to pick the correct port.
