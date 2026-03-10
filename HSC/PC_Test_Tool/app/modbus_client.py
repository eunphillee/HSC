"""
Modbus RTU client for Mainboard only. All I/O synchronous; run from worker thread.
Compatible with pymodbus 2.5.3 (ModbusSerialClient, unit= for slave).
"""
import threading
from typing import Callable

from pymodbus.client.sync import ModbusSerialClient
from pymodbus.exceptions import ModbusException

from .address_map import (
    MAINBOARD_SLAVE_ID_DEFAULT,
    MAIN_DI_REG,
    MAIN_DI_COUNT,
    MAIN_DO_REG,
    MAIN_DO_COUNT,
    PC_ON_EN_REG,
    PC_RESET_EN_REG,
    PC_LED_IN_REG,
    MAIN_ENV_REG,
    MAIN_ENV_COUNT,
)


def format_modbus_error(resp=None, exc=None) -> str:
    """
    Safe error message from Modbus response or Python exception.
    - resp: use exception_code only if present (not on ModbusIOException).
    - exc: never access exception_code; use type and str only.
    """
    if resp is not None:
        if getattr(resp, "isError", lambda: False)():
            code = getattr(resp, "exception_code", None)
            fc = getattr(resp, "function_code", None)
            parts = []
            if fc is not None:
                parts.append(f"function_code=0x{fc:02X}")
            if code is not None:
                parts.append(f"exception_code=0x{code:02X}")
            if parts:
                return "; ".join(parts)
            return str(resp) if str(resp) else f"{type(resp).__name__}(error response)"
    if exc is not None:
        return f"{type(exc).__name__}: {exc}"
    return "Unknown error"


def _response_exception_code(resp) -> int | None:
    """Modbus exception code from response if error; else None."""
    if resp is None:
        return None
    if not getattr(resp, "isError", lambda: False)():
        return None
    return getattr(resp, "exception_code", None)


class ModbusClient:
    """Thread-safe Modbus RTU client for Mainboard. Call from worker thread only."""
    _lock = threading.Lock()

    def __init__(self):
        self._client: ModbusSerialClient | None = None
        self._port: str = ""
        self._baudrate: int = 9600
        self._slave_id: int = MAINBOARD_SLAVE_ID_DEFAULT
        self._request_logger: Callable[[int, str, int | str, int | str], None] | None = None
        self._response_logger: Callable[[bool, int | None], None] | None = None

    def set_request_logger(self, callback: Callable[[int, str, int | str, int | str], None] | None):
        """Set callback(unit, func, addr, count_or_value) for each request (TX)."""
        self._request_logger = callback

    def set_response_logger(self, callback: Callable[[bool, int | None], None] | None):
        """Set callback(ok, exception_code) for each response (RX). exception_code is None on success or non-Modbus error."""
        self._response_logger = callback

    def connect(self, port: str, baudrate: int = 9600, slave_id: int = MAINBOARD_SLAVE_ID_DEFAULT) -> tuple[bool, str]:
        """Connect to serial port. Returns (success, message)."""
        with self._lock:
            if self._client and self._client.is_socket_open():
                return False, "Already connected"
            try:
                self._client = ModbusSerialClient(
                    method="rtu",
                    port=port,
                    baudrate=baudrate,
                    bytesize=8,
                    parity="N",
                    stopbits=1,
                )
                if not self._client.connect():
                    return False, "Failed to open port"
                self._port = port
                self._baudrate = baudrate
                self._slave_id = slave_id
                return True, "Connected"
            except Exception as e:
                return False, format_modbus_error(exc=e)

    def disconnect(self) -> None:
        with self._lock:
            if self._client:
                try:
                    self._client.close()
                except Exception:
                    pass
                self._client = None
            self._port = ""

    def _ensure_socket_open(self) -> tuple[bool, str | None]:
        """
        If client exists but socket is closed (e.g. after timeout), try reconnect once.
        Caller must hold _lock. Never calls disconnect(); on failure returns (False, err).
        """
        if self._client is None:
            return False, "Not connected"
        if self._client.is_socket_open():
            return True, None
        try:
            old = self._client
            self._client = ModbusSerialClient(
                method="rtu",
                port=self._port,
                baudrate=self._baudrate,
                bytesize=8,
                parity="N",
                stopbits=1,
            )
            if not self._client.connect():
                self._client = old
                return False, "Reconnect failed (port open failed)"
            try:
                old.close()
            except Exception:
                pass
            return True, None
        except Exception as e:
            if self._client is not old:
                self._client = old
            return False, format_modbus_error(exc=e)

    @property
    def connected(self) -> bool:
        """True while user has connected and not disconnected. Does not drop on request failure."""
        with self._lock:
            return self._client is not None

    def read_raw_available(self) -> list[int]:
        """Read any bytes currently in the serial RX buffer (e.g. board TX 0xAA). Returns list of byte values."""
        with self._lock:
            if not self._client or not self._client.is_socket_open():
                return []
            # pymodbus may store serial as socket, connection, client, or _connection
            sock = None
            for name in ("socket", "connection", "client", "_connection"):
                c = getattr(self._client, name, None)
                if c is not None and callable(getattr(c, "read", None)) and hasattr(c, "in_waiting"):
                    sock = c
                    break
            if sock is None:
                return []
            out: list[int] = []
            try:
                n = getattr(sock, "in_waiting", 0) or 0
                if n > 0:
                    data = sock.read(min(n, 256))
                    out = list(data)
            except Exception:
                pass
            return out

    def read_di_bitmap(self) -> tuple[bool, list[int] | None, str | None]:
        """FC03 read MAIN_DI_REG count=1 → 8 bits DI_01..DI_08. Returns (ok, bits[8] or None, err)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            if self._request_logger:
                self._request_logger(self._slave_id, "FC03", MAIN_DI_REG, 2)
            try:
                rr = self._client.read_holding_registers(
                    address=MAIN_DI_REG,
                    count=2,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0, 0]
                di_val = regs[0] if len(regs) > 0 else 0
                bits = [(di_val >> i) & 1 for i in range(MAIN_DI_COUNT)]
                return True, bits, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def read_pc_led_in(self) -> tuple[bool, bool | None, str | None]:
        """FC03 read PC_LED_IN_REG count=1 → bit0 = PC_LED_IN. Returns (ok, state or None, err)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            if self._request_logger:
                self._request_logger(self._slave_id, "FC03", PC_LED_IN_REG, 1)
            try:
                rr = self._client.read_holding_registers(
                    address=PC_LED_IN_REG,
                    count=1,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0]
                state = bool(regs[0] & 1) if regs else False
                return True, state, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def read_env_shtc3(self) -> tuple[bool, tuple[float, float] | None, str | None]:
        """FC03 read MAIN_ENV_REG count=3 → (temp_c, rh_pct, error_flags). Returns (ok, (t,rh,flags) or None, err)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            if self._request_logger:
                self._request_logger(self._slave_id, "FC03", MAIN_ENV_REG, MAIN_ENV_COUNT)
            try:
                rr = self._client.read_holding_registers(
                    address=MAIN_ENV_REG,
                    count=MAIN_ENV_COUNT,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0, 0, 0]
                if len(regs) < 3:
                    regs = (regs + [0, 0, 0])[:3]
                raw_t = regs[0] & 0xFFFF
                # signed int16
                if raw_t & 0x8000:
                    raw_t = raw_t - 0x10000
                raw_rh = regs[1] & 0xFFFF
                flags = regs[2] & 0xFFFF
                temp_c = float(raw_t) / 10.0
                rh_pct = float(raw_rh) / 10.0
                return True, (temp_c, rh_pct, flags), None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def write_relay(self, ch: int, onoff: bool) -> tuple[bool, str | None]:
        """Set one relay (ch 0..3). Read current DO bitmap, set bit ch, FC06 write. Returns (ok, err)."""
        if ch < 0 or ch >= MAIN_DO_COUNT:
            return False, "Invalid relay index 0..3"
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC03", MAIN_DI_REG, 2)
                rr = self._client.read_holding_registers(
                    address=MAIN_DI_REG,
                    count=2,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0, 0]
                do_val = regs[1] & 0x0F if len(regs) > 1 else 0
                if onoff:
                    do_val |= 1 << ch
                else:
                    do_val &= ~(1 << ch)
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC06", MAIN_DO_REG, do_val)
                wr = self._client.write_register(
                    address=MAIN_DO_REG,
                    value=do_val,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)

    def write_pc_on_en(self, onoff: bool) -> tuple[bool, str | None]:
        """FC06 write PC_ON_EN_REG (2120): value=1 → 100ms pulse, 0 → LOW. Returns (ok, err)."""
        return self._write_pc_reg(PC_ON_EN_REG, 1 if onoff else 0)

    def write_pc_reset_en(self, onoff: bool) -> tuple[bool, str | None]:
        """FC06 write PC_RESET_EN_REG (2121): value=1 → 100ms pulse, 0 → LOW. Returns (ok, err)."""
        return self._write_pc_reg(PC_RESET_EN_REG, 1 if onoff else 0)

    def _write_pc_reg(self, address: int, value: int) -> tuple[bool, str | None]:
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC06", address, value)
                wr = self._client.write_register(
                    address=address,
                    value=value,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)
