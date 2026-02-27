# PC Test Tool — HSC MAIN Board (H2TECH Modbus RTU)

Production-friendly PC test tool that talks to the MAIN board over RS485 Modbus RTU using H2TECH addressing.

## Requirements

- Python 3.11+
- RS485 adapter (USB–RS485 or built-in serial with RS485 transceiver)

## Install

```bash
cd HSC/PC_Test_Tool
python -m venv .venv
# Windows:
.venv\Scripts\activate
# macOS/Linux:
source .venv/bin/activate
pip install -r requirements.txt
```

## Run

```bash
python main.py
```

Select COM port (e.g. `COM3` on Windows, `/dev/tty.usbserial-*` or `/dev/ttyUSB0` on macOS/Linux), set Slave ID (default 1), then Connect. Use the read panels and control buttons as needed.

## Serial settings

- **9600** baud, **8N1**, Modbus RTU
- User selects port; refresh to rescan

## Notes

- If you get `TypeError` about `slave` when using pymodbus 3.x, replace `slave=` with `unit=` in `app/modbus_client.py` for the pymodbus client calls.

## Protocol notes

- H2TECH “1xNNNN” uses Modbus starting address = **NNNN - 1** (e.g. 1x0821 → start 820).
- FC02 responses: LSB-first bit packing.
- Current block: **FC03 only** with **start=2000, count=14**. Any other start/count returns exception 0x02/0x03.

## RS485 adapter

- Ensure the adapter’s driver is installed (e.g. FTDI, CH340, CP210x).
- On Windows, the port appears as `COMn`; on macOS as `/dev/tty.usbserial-*` or similar; on Linux as `/dev/ttyUSB0` or `/dev/ttyACM0`.
- Do not hardcode the port name; use the dropdown and refresh to pick the correct port.
