"""
Modbus RTU client wrapper using pymodbus. All I/O is synchronous; run from worker thread.
"""
import threading
from pymodbus.client import ModbusSerialClient
from pymodbus.exceptions import ModbusException

from .h2tech_map import (
    ONOFF_START, ONOFF_COUNT,
    DOOR_START, DOOR_COUNT,
    ALARMS_START, ALARMS_COUNT,
    CMD_ONOFF_START, CMD_ONOFF_COUNT,
    CURRENT_START, CURRENT_COUNT,
    DOOR_OPEN_1_COIL, DOOR_OPEN_2_COIL,
    VB_ONOFF_8_COIL, VB_ONOFF_12_COIL,
    INVALID_COIL_899, INVALID_COIL_900,
)


class ModbusClient:
    """Thread-safe Modbus RTU client. Call from worker thread only."""
    _lock = threading.Lock()

    def __init__(self):
        self._client: ModbusSerialClient | None = None
        self._port: str = ""
        self._slave_id: int = 1

    def connect(self, port: str, baudrate: int = 9600, slave_id: int = 1) -> tuple[bool, str]:
        """Connect to serial port. Returns (success, message)."""
        with self._lock:
            if self._client and self._client.connected:
                return False, "Already connected"
            try:
                self._client = ModbusSerialClient(
                    port=port,
                    baudrate=baudrate,
                    bytesize=8,
                    parity="N",
                    stopbits=1,
                )
                if not self._client.connect():
                    return False, "Failed to open port"
                self._port = port
                self._slave_id = slave_id
                return True, "Connected"
            except Exception as e:
                return False, str(e)

    def disconnect(self) -> None:
        with self._lock:
            if self._client:
                try:
                    self._client.close()
                except Exception:
                    pass
                self._client = None
            self._port = ""

    @property
    def connected(self) -> bool:
        with self._lock:
            return self._client is not None and self._client.connected

    def _read_discrete(self, start: int, count: int) -> tuple[bool, list[int] | None, str | None]:
        """FC02 Read Discrete Inputs. Returns (ok, bits or None, exception_message)."""
        with self._lock:
            if not self._client or not self._client.connected:
                return False, None, "Not connected"
            try:
                rr = self._client.read_discrete_inputs(
                    address=start,
                    count=count,
                    slave=self._slave_id,
                )
                if rr.isError():
                    return False, None, f"Exception 0x{rr.exception_code:02X}"
                # LSB-first: first address = bit0
                raw = getattr(rr, "bits", None) or []
                bits = [1 if (i < len(raw) and raw[i]) else 0 for i in range(count)]
                return True, bits, None
            except ModbusException as e:
                return False, None, str(e)
            except Exception as e:
                return False, None, str(e)

    def read_onoff(self) -> tuple[bool, list[int] | None, str | None]:
        return self._read_discrete(ONOFF_START, ONOFF_COUNT)

    def read_door_sensors(self) -> tuple[bool, list[int] | None, str | None]:
        return self._read_discrete(DOOR_START, DOOR_COUNT)

    def read_alarms(self) -> tuple[bool, list[int] | None, str | None]:
        return self._read_discrete(ALARMS_START, ALARMS_COUNT)

    def read_cmd_onoff(self) -> tuple[bool, list[int] | None, str | None]:
        return self._read_discrete(CMD_ONOFF_START, CMD_ONOFF_COUNT)

    def read_currents(self) -> tuple[bool, list[int] | None, str | None]:
        """FC03 start=2000 count=14 only."""
        with self._lock:
            if not self._client or not self._client.connected:
                return False, None, "Not connected"
            try:
                rr = self._client.read_holding_registers(
                    address=CURRENT_START,
                    count=CURRENT_COUNT,
                    slave=self._slave_id,
                )
                if rr.isError():
                    return False, None, f"Exception 0x{rr.exception_code:02X}"
                regs = list(rr.registers) if rr.registers else []
                return True, regs, None
            except ModbusException as e:
                return False, None, str(e)
            except Exception as e:
                return False, None, str(e)

    def write_coil(self, address: int, value: bool) -> tuple[bool, str | None]:
        """FC05 Write Single Coil. Returns (ok, exception_message)."""
        with self._lock:
            if not self._client or not self._client.connected:
                return False, "Not connected"
            try:
                rr = self._client.write_coil(
                    address=address,
                    value=value,
                    slave=self._slave_id,
                )
                if rr.isError():
                    return False, f"Exception 0x{rr.exception_code:02X}"
                return True, None
            except ModbusException as e:
                return False, str(e)
            except Exception as e:
                return False, str(e)

    def write_door_open_1(self) -> tuple[bool, str | None]:
        return self.write_coil(DOOR_OPEN_1_COIL, True)

    def write_door_open_2(self) -> tuple[bool, str | None]:
        return self.write_coil(DOOR_OPEN_2_COIL, True)

    def write_vb_onoff(self, index_8_to_12: int) -> tuple[bool, str | None]:
        """index_8_to_12: 8..12 for ON/OFF 8~12."""
        coil_map = {
            8: VB_ONOFF_8_COIL,
            9: VB_ONOFF_9_COIL,
            10: VB_ONOFF_10_COIL,
            11: VB_ONOFF_11_COIL,
            12: VB_ONOFF_12_COIL,
        }
        addr = coil_map.get(index_8_to_12)
        if addr is None:
            return False, "Invalid index 8..12"
        return self.write_coil(addr, True)

    def write_invalid_coil_899(self) -> tuple[bool, str | None]:
        """Expect exception 0x02."""
        return self.write_coil(INVALID_COIL_899, True)

    def write_invalid_coil_900(self) -> tuple[bool, str | None]:
        """Expect exception 0x02."""
        return self.write_coil(INVALID_COIL_900, True)
