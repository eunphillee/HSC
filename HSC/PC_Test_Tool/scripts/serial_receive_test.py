#!/usr/bin/env python3
"""
HPSB 보드 수신 확인용: 지정 포트를 9600 8N1로 열고 5초 동안 수신된 바이트를 출력합니다.
PC 툴에서 수신이 안 될 때, 이 스크립트로 포트/배선을 먼저 확인하세요.

사용법:
  python scripts/serial_receive_test.py /dev/cu.usbserial-1120
  또는 (프로젝트 루트에서)
  python -m scripts.serial_receive_test /dev/cu.usbserial-1120
"""
import sys
import time

def main():
    if len(sys.argv) < 2:
        print("Usage: python serial_receive_test.py <PORT>")
        print("  e.g. python serial_receive_test.py /dev/cu.usbserial-1120")
        sys.exit(1)
    port = sys.argv[1]
    try:
        import serial
    except ImportError:
        print("pip install pyserial 후 다시 실행하세요.")
        sys.exit(1)
    print(f"Opening {port} at 9600 8N1, waiting 5 seconds for data...")
    try:
        ser = serial.Serial(port, 9600, timeout=0.5)
    except Exception as e:
        print(f"Failed to open port: {e}")
        sys.exit(1)
    start = time.monotonic()
    collected = []
    while (time.monotonic() - start) < 5.0:
        data = ser.read(512)
        if data:
            collected.extend(data)
            print(f"  Received {len(data)} bytes (total {len(collected)})")
        time.sleep(0.05)
    ser.close()
    if not collected:
        print("Result: 0 bytes received in 5 seconds.")
        print("  - Check that the HPSB board is connected to this port (not the mainboard).")
        print("  - Check cable: PC RX <-> Board TX (PA9).")
        print("  - Ensure HPSB_TEST firmware is running and sending every 1 second.")
    else:
        print(f"Result: {len(collected)} bytes total.")
        try:
            text = bytes(collected).decode("ascii", errors="replace")
            print("  ASCII:", repr(text[:200]))
        except Exception:
            print("  Hex:", " ".join(f"{b:02X}" for b in collected[:100]))

if __name__ == "__main__":
    main()
