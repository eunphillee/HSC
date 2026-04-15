"""
Modbus RTU client for Mainboard only. All I/O synchronous; run from worker thread.
Compatible with pymodbus 2.5.3 (ModbusSerialClient, unit= for slave).
Direct HPSB 시 raw_only 연결 시 pyserial만 사용해 수신이 pymodbus에 가로채이지 않도록 함.
"""
import threading
import time
from typing import Callable

import serial
from pymodbus.client.sync import ModbusSerialClient
from pymodbus.exceptions import ModbusException


def _modbus_crc16(data: bytes) -> int:
    """Modbus RTU CRC-16 (LSB first in frame)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_fc05_rtu_frame(slave_id: int, coil_addr: int, value: bool) -> bytes:
    """Build FC05 Write Single Coil RTU frame (8 bytes). value=True -> 0xFF00, False -> 0x0000 per Modbus."""
    # PDU: FC=05, Addr_H Addr_L, Val_H Val_L (0xFF00 ON, 0x0000 OFF)
    addr_hi = (coil_addr >> 8) & 0xFF
    addr_lo = coil_addr & 0xFF
    if value:
        val_hi, val_lo = 0xFF, 0x00
    else:
        val_hi, val_lo = 0x00, 0x00
    payload = bytes([slave_id, 0x05, addr_hi, addr_lo, val_hi, val_lo])
    crc = _modbus_crc16(payload)
    return payload + bytes([crc & 0xFF, (crc >> 8) & 0xFF])

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
    SUB_SENSE_REG,
    SUB_SENSE_COUNT,
    SUB_COIL_STATUS_START,
    SUB_COIL_STATUS_COUNT,
    SUB_ALARM_START,
    SUB_ALARM_COUNT,
    SUB_VB_COIL_BASE,
    SUB_VB_COIL_COUNT,
    SUB_HPSB_COIL_BASE,
    SUB_HPSB_COIL_COUNT,
    SUB_LPSB_COIL_BASE,
    SUB_LPSB_COIL_COUNT,
    ERROR_FLAGS_REG,
    MAIN_VBIT_COIL_BASE,
    MAIN_VBIT_COUNT,
    MAIN_FC04_DI_VBIT_COUNT,
    MAIN_RTC_REG_START,
    MAIN_RTC_REG_COUNT,
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
        self._raw_serial: serial.Serial | None = None  # Direct HPSB 시 수신 전용 (pymodbus 미사용)
        self._port: str = ""
        self._baudrate: int = 9600
        self._slave_id: int = MAINBOARD_SLAVE_ID_DEFAULT
        self._request_logger: Callable[[int, str, int | str, int | str], None] | None = None
        self._response_logger: Callable[[bool, int | None], None] | None = None
        self._tx_frame_hex_logger: Callable[[str], None] | None = None
        self._raw_exception_logger: Callable[[str], None] | None = None
        self._last_sub_raw: dict | None = None

    def set_raw_exception_logger(self, callback: Callable[[str], None] | None):
        """Set callback(msg) for serial raw-read exceptions (UI immediate debug)."""
        self._raw_exception_logger = callback

    def set_tx_frame_hex_logger(self, callback: Callable[[str], None] | None):
        """Set callback(msg) to log raw TX frame hex (e.g. Direct HPSB FC05)."""
        self._tx_frame_hex_logger = callback

    def set_request_logger(self, callback: Callable[[int, str, int | str, int | str], None] | None):
        """Set callback(unit, func, addr, count_or_value) for each request (TX)."""
        self._request_logger = callback

    def set_response_logger(self, callback: Callable[[bool, int | None], None] | None):
        """Set callback(ok, exception_code) for each response (RX). exception_code is None on success or non-Modbus error."""
        self._response_logger = callback

    def connect(
        self,
        port: str,
        baudrate: int = 9600,
        slave_id: int = MAINBOARD_SLAVE_ID_DEFAULT,
        raw_only: bool = False,
    ) -> tuple[bool, str]:
        """Connect to serial port. raw_only=True면 Modbus 미사용, pyserial만 열어 HPSB raw 수신 전용(가로채임 방지)."""
        with self._lock:
            if (self._client and self._client.is_socket_open()) or (
                self._raw_serial and self._raw_serial.is_open
            ):
                return False, "Already connected"
            try:
                if raw_only:
                    if self._client:
                        try:
                            self._client.close()
                        except Exception:
                            pass
                        self._client = None
                    self._raw_serial = serial.Serial(
                        port=port,
                        baudrate=baudrate,
                        bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE,
                        timeout=0.1,
                    )
                    self._port = port
                    self._baudrate = baudrate
                    self._slave_id = slave_id
                    return True, "Connected (raw only)"
                self._raw_serial = None
                self._client = ModbusSerialClient(
                    method="rtu",
                    port=port,
                    baudrate=baudrate,
                    bytesize=8,
                    parity="N",
                    stopbits=1,
                    # FC04 응답(최대 16regs=37B) 기준 여유 타임아웃.
                    # OUTPUT_MONITORING 중 간헐 지연을 흡수하기 위해 2.0s로 설정.
                    timeout=2.0,
                    retries=1,
                )
                # pymodbus connect()는 실패 원인을 숨기는 경우가 있어,
                # 동일 파라미터로 pyserial을 먼저 열어 예외 메시지를 보존한다.
                try:
                    test_ser = serial.Serial(
                        port=port,
                        baudrate=baudrate,
                        bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE,
                        timeout=0.1,
                    )
                    test_ser.close()
                except Exception as e:
                    return False, format_modbus_error(exc=e)

                if not self._client.connect():
                    return False, "Failed to open port (pymodbus connect returned False)"
                self._port = port
                self._baudrate = baudrate
                self._slave_id = slave_id
                return True, "Connected"
            except Exception as e:
                if raw_only and self._raw_serial:
                    try:
                        self._raw_serial.close()
                    except Exception:
                        pass
                    self._raw_serial = None
                return False, format_modbus_error(exc=e)

    def disconnect(self) -> None:
        with self._lock:
            if self._client:
                try:
                    self._client.close()
                except Exception:
                    pass
                self._client = None
            if self._raw_serial:
                try:
                    self._raw_serial.close()
                except Exception:
                    pass
                self._raw_serial = None
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
                timeout=1.2,
                retries=1,
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
            if self._raw_serial and self._raw_serial.is_open:
                return True
            return self._client is not None

    def _get_serial_for_raw_read(self):
        """Raw 수신용 시리얼: raw_only 연결이면 _raw_serial, 아니면 pymodbus 내부 객체. 호출 시 _lock 확보 필수."""
        if self._raw_serial and self._raw_serial.is_open:
            return self._raw_serial
        if not self._client or not self._client.is_socket_open():
            return None
        for name in ("socket", "connection", "client", "_connection", "serial", "serial_", "_serial"):
            c = getattr(self._client, name, None)
            if c is not None and callable(getattr(c, "read", None)):
                if hasattr(c, "in_waiting"):
                    return c
                if hasattr(c, "inWaiting"):  # PySerial 구버전
                    return c
        # 재귀: 한 단계 감싼 객체 안에서 찾기
        for name in ("socket", "connection", "client", "_connection", "serial"):
            c = getattr(self._client, name, None)
            if c is None:
                continue
            for sub in ("serial", "connection", "_serial", "client"):
                s = getattr(c, sub, None)
                if s is not None and callable(getattr(s, "read", None)) and (hasattr(s, "in_waiting") or hasattr(s, "inWaiting")):
                    return s
        return None

    def read_raw_available(self) -> list[int]:
        """Read any bytes currently in the serial RX buffer (e.g. board TX 0xAA, HPSB_TEST string)."""
        with self._lock:
            sock = self._get_serial_for_raw_read()
            if sock is None:
                return []
            out: list[int] = []
            try:
                # pyserial 버전에 따라 in_waiting이 "프로퍼티"가 아니라 "메서드"처럼 동작하는 경우가 있음.
                # 그 경우 n이 method 객체가 되어 `n > 0` 비교에서 TypeError가 발생.
                n = 0
                in_waiting = getattr(sock, "in_waiting", None)
                if in_waiting is not None:
                    n = in_waiting() if callable(in_waiting) else in_waiting
                else:
                    inWaiting = getattr(sock, "inWaiting", 0)
                    n = inWaiting() if callable(inWaiting) else inWaiting
                try:
                    n = int(n) if n is not None else 0
                except Exception:
                    n = 0
                if n > 0:
                    data = sock.read(min(n, 512))
                    out = list(data)
            except Exception as e:
                if self._raw_exception_logger:
                    self._raw_exception_logger(f"[PC-TOOL] serial read exception={type(e).__name__}: {e}")
            return out

    def can_read_raw(self) -> bool:
        """Direct HPSB 등 보드→PC raw 수신이 가능한지 (내부 시리얼 객체 발견 여부)."""
        with self._lock:
            return self._get_serial_for_raw_read() is not None

    def sniff_raw(self, timeout_sec: float = 2.0) -> list[int]:
        """직접 HPSB(raw_only) 연결 시, 지정 시간만큼 대기하며 수신된 바이트 수집. 포트에 데이터가 오는지 확인용."""
        with self._lock:
            if not self._raw_serial or not self._raw_serial.is_open:
                return []
            prev_timeout = self._raw_serial.timeout
            out: list[int] = []
            try:
                self._raw_serial.timeout = 0.25  # 250ms마다 읽기
                deadline = time.monotonic() + timeout_sec
                while time.monotonic() < deadline:
                    data = self._raw_serial.read(512)
                    if data:
                        out.extend(data)
                        if len(out) >= 2048:
                            break
            except Exception as e:
                if self._raw_exception_logger:
                    self._raw_exception_logger(f"[PC-TOOL] serial sniff exception={type(e).__name__}: {e}")
            finally:
                self._raw_serial.timeout = prev_timeout
            return out

    def read_di_bitmap(self) -> tuple[bool, list[int] | None, list[int] | None, list[int] | None, str | None]:
        """Mainboard FC04 0/24 read: DI(reg2..9), relay(reg11..14), VBIT(reg20..23)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, None, None, err or "Not connected"
            # FC04 mainboard map: reg2..9 = DI1..DI8, reg11..14 = relay1..4 actual, reg20..23 = VBIT
            last_err: str | None = None
            for attempt in range(2):  # 간헐 timeout/noise 완화: 최대 1회 재시도
                try:
                    if self._request_logger:
                        self._request_logger(self._slave_id, "FC04", 0, MAIN_FC04_DI_VBIT_COUNT)
                    rr = self._client.read_input_registers(
                        address=0,
                        count=MAIN_FC04_DI_VBIT_COUNT,
                        unit=self._slave_id,
                    )
                    if self._response_logger:
                        self._response_logger(not rr.isError(), _response_exception_code(rr))
                    if rr.isError():
                        last_err = format_modbus_error(resp=rr)
                        continue
                    regs = list(rr.registers) if rr.registers else [0] * MAIN_FC04_DI_VBIT_COUNT
                    regs = (regs + [0] * MAIN_FC04_DI_VBIT_COUNT)[:MAIN_FC04_DI_VBIT_COUNT]
                    bits = [1 if regs[2 + i] else 0 for i in range(MAIN_DI_COUNT)]
                    relay_states = [1 if regs[11 + i] else 0 for i in range(4)]
                    vbits = [1 if regs[20 + i] else 0 for i in range(MAIN_VBIT_COUNT)]
                    return True, bits, relay_states, vbits, None
                except Exception as e:
                    if self._response_logger:
                        self._response_logger(False, None)
                    last_err = format_modbus_error(exc=e)
                    if attempt == 0:
                        continue
            return False, None, None, None, (last_err or "No response/timeout")

    def write_mb_virtual_bit(self, ch: int, onoff: bool) -> tuple[bool, str | None]:
        if ch < 0 or ch >= MAIN_VBIT_COUNT:
            return False, "Invalid virtual bit index 0..3"
        return self.write_single_coil(MAIN_VBIT_COIL_BASE + ch, onoff, unit=None)

    def read_board_time(self) -> tuple[bool, list[int] | None, str | None]:
        return self.read_input_registers(MAIN_RTC_REG_START, MAIN_RTC_REG_COUNT, unit=None)

    def write_board_time(self, values: list[int]) -> tuple[bool, str | None]:
        if len(values) != MAIN_RTC_REG_COUNT:
            return False, f"RTC values length must be {MAIN_RTC_REG_COUNT}"
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC16", MAIN_RTC_REG_START, MAIN_RTC_REG_COUNT)
                wr = self._client.write_registers(
                    address=MAIN_RTC_REG_START,
                    values=[int(v) & 0xFFFF for v in values],
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

    def read_pc_led_in(self) -> tuple[bool, bool | None, str | None]:
        """Unified Rule: Mainboard PC_LED_IN via FC04 mainboard map reg10 (addr=10, count=1)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC04", 10, 1)
                rr = self._client.read_input_registers(
                    address=10,
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

    def read_env_shtc3(self) -> tuple[bool, tuple[float, float, int] | None, str | None]:
        """Env Sensor(SHTC3) via Mainboard FC04 local map.
        - FC04 addr=0 count=17 응답에서 reg15=temp_c_x10(s16), reg16=rh_x10(u16)로 제공.
        - 센서 에러/미연결은 보드가 -32768 / 0xFFFF로 채움. 이 경우도 통신 성공으로 처리한다.
        Returns (ok, (temp_c, rh_pct, flags_u16), err).
        """
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC04", 0, 17)
                rr = self._client.read_input_registers(
                    address=0,
                    count=17,
                    unit=self._slave_id,
                )
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0] * 17
                regs = (regs + [0] * 17)[:17]

                # flags from reg1 (u16)
                flags = int(regs[1]) & 0xFFFF

                # temp reg15 is signed 16-bit (cx10)
                t_raw = int(regs[15]) & 0xFFFF
                if t_raw & 0x8000:
                    t_raw -= 0x10000
                rh_raw = int(regs[16]) & 0xFFFF

                # Sentinel: -32768 / 0xFFFF -> N/A
                if t_raw == -32768 or rh_raw == 0xFFFF:
                    return True, (-3276.8, 6553.5, flags), None

                return True, (t_raw / 10.0, rh_raw / 10.0, flags), None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def write_relay(self, ch: int, onoff: bool) -> tuple[bool, str | None]:
        """Unified Rule: Mainboard FC05 coil0..3 = Relay1..4 state."""
        if ch < 0 or ch >= MAIN_DO_COUNT:
            return False, "Invalid relay index 0..3"
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC05", ch, 1 if onoff else 0)
                wr = self._client.write_coil(
                    address=ch,
                    value=onoff,
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
        """Unified Rule: Mainboard FC05 coil4 = PC_ON (value=True -> pulse)."""
        return self.write_single_coil(4, onoff, unit=None)

    def write_pc_reset_en(self, onoff: bool) -> tuple[bool, str | None]:
        """Unified Rule: Mainboard FC05 coil6 = RESET (value=True -> pulse)."""
        return self.write_single_coil(6, onoff, unit=None)

    def read_sub_sense(self) -> tuple[bool, list[int] | None, str | None]:
        """Unified Rule v1.3: FC04 addr=24 count=58 (packed map) → sense array (SUB_SENSE_COUNT=40)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            try:
                # Unified Rule v1.3: FC04 addr=24 count=58
                # Packed layout:
                #   24..33 : alive/status  (HPSB=24, LPSB2=28, LPSB4=29, LPSB8=30)
                #   34..45 : coils         (HPSB r0-2 at 34-36; LPSB2/4/8 s0-2 at 37-45)
                #   46..57 : AVG           (HPSB a0-2 at 46-48; LPSB2/4/8 at 49-57)
                #   58..69 : PKPK          (same board/channel order)
                #   70..81 : CUR           (same board/channel order)
                def rd(addr: int, count: int) -> list[int]:
                    if self._request_logger:
                        self._request_logger(self._slave_id, "FC04", addr, count)
                    r = self._client.read_input_registers(address=addr, count=count, unit=self._slave_id)
                    if self._response_logger:
                        self._response_logger(not r.isError(), _response_exception_code(r))
                    if r.isError():
                        raise RuntimeError(format_modbus_error(resp=r))
                    out = list(r.registers) if r.registers else []
                    out = (out + [0] * count)[:count]
                    return [x & 0xFFFF for x in out]

                packed = rd(24, 58)   # packed[0] = addr 24, packed[57] = addr 81
                # raw dump 보관 (lock 내부)
                self._last_sub_raw = {"packed_24_81": list(packed)}

                sense = [0] * SUB_SENSE_COUNT
                # packed offset = abs_addr - 24
                # AVG 46..57  → packed[22..33]
                # PKPK 58..69 → packed[34..45]
                # CUR  70..81 → packed[46..57]
                for ch in range(3):
                    sense[0 + ch] = packed[22 + ch]   # HPSB AVG
                    sense[3 + ch] = packed[34 + ch]   # HPSB PKPK
                    sense[6 + ch] = packed[46 + ch]   # HPSB CUR
                    sense[9 + ch]  = packed[25 + ch]  # LPSB2 AVG
                    sense[12 + ch] = packed[37 + ch]  # LPSB2 PKPK
                    sense[15 + ch] = packed[49 + ch]  # LPSB2 CUR
                    sense[18 + ch] = packed[28 + ch]  # LPSB4 AVG
                    sense[21 + ch] = packed[40 + ch]  # LPSB4 PKPK
                    sense[24 + ch] = packed[52 + ch]  # LPSB4 CUR
                    sense[27 + ch] = packed[31 + ch]  # LPSB8 AVG
                    sense[30 + ch] = packed[43 + ch]  # LPSB8 PKPK
                    sense[33 + ch] = packed[55 + ch]  # LPSB8 CUR

                return True, sense, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def get_last_sub_raw_copy(self) -> dict | None:
        """최근 read_sub_sense()에서 읽은 raw 블록을 복사해 반환."""
        with self._lock:
            if not self._last_sub_raw:
                return None
            try:
                return {k: list(v) for k, v in self._last_sub_raw.items()}
            except Exception:
                return None

    def read_sub_coil_status(self) -> tuple[bool, list[bool] | None, str | None]:
        """Unified Rule v1.3: FC04 addr=34 count=12 → coils layout (len=14, last 2 unused)."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            try:
                def rd(addr: int, count: int) -> list[int]:
                    if self._request_logger:
                        self._request_logger(self._slave_id, "FC04", addr, count)
                    r = self._client.read_input_registers(address=addr, count=count, unit=self._slave_id)
                    if self._response_logger:
                        self._response_logger(not r.isError(), _response_exception_code(r))
                    if r.isError():
                        raise RuntimeError(format_modbus_error(resp=r))
                    out = list(r.registers) if r.registers else []
                    out = (out + [0] * count)[:count]
                    return [x & 0xFFFF for x in out]

                # Coils 34..45: HPSB r0-2 at [0-2], LPSB2 s0-2 at [3-5],
                #               LPSB4 s0-2 at [6-8], LPSB8 s0-2 at [9-11]
                c = rd(34, 12)
                coils = [False] * 14
                coils[0]  = bool(c[0]);  coils[1]  = bool(c[1]);  coils[2]  = bool(c[2])   # HPSB
                coils[3]  = bool(c[3]);  coils[4]  = bool(c[4]);  coils[5]  = bool(c[5])   # LPSB2
                coils[6]  = bool(c[6]);  coils[7]  = bool(c[7]);  coils[8]  = bool(c[8])   # LPSB4
                coils[9]  = bool(c[9]);  coils[10] = bool(c[10]); coils[11] = bool(c[11])  # LPSB8

                return True, coils, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def read_full_state(self) -> tuple[bool, dict | None, str | None]:
        """
        Unified Rule v1.3: FC04 2-read pass (single source of truth).

        Returns (ok, state_dict, err) where state_dict has keys:
          'main':   regs[0..23]  – FC04 addr=0  count=24  (DI/relay/vbits/env)
          'packed': regs[0..57]  – FC04 addr=24 count=58  (HPSB+LPSB packed map)

        Packed layout (abs_addr → packed index = abs_addr - 24):
          24=HPSB alive,  25-27=rsvd,  28=LPSB2 alive, 29=LPSB4, 30=LPSB8, 31-33=rsvd
          34-36=HPSB coil0-2,  37-39=LPSB2 SSR0-2,  40-42=LPSB4 SSR0-2, 43-45=LPSB8 SSR0-2
          46-48=HPSB AVG0-2,   49-51=LPSB2 AVG,     52-54=LPSB4 AVG,    55-57=LPSB8 AVG
          58-60=HPSB PKPK0-2,  61-63=LPSB2 PKPK,    64-66=LPSB4 PKPK,   67-69=LPSB8 PKPK
          70-72=HPSB CUR0-2,   73-75=LPSB2 CUR,     76-78=LPSB4 CUR,    79-81=LPSB8 CUR
        """
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"

            def _rd(addr: int, count: int) -> list[int]:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC04", addr, count)
                r = self._client.read_input_registers(
                    address=addr, count=count, unit=self._slave_id
                )
                if self._response_logger:
                    self._response_logger(not r.isError(), _response_exception_code(r))
                if r.isError():
                    raise RuntimeError(format_modbus_error(resp=r))
                out = list(r.registers) if r.registers else []
                out = (out + [0] * count)[:count]
                return [x & 0xFFFF for x in out]

            try:
                state = {
                    "main":   _rd(0,  24),
                    "packed": _rd(24, 58),
                }
                self._last_sub_raw = {"packed_24_81": list(state["packed"])}
                return True, state, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def read_sub_alarms(self) -> tuple[bool, list[bool] | None, str | None]:
        """FC02 not allowed per Unified Rule v1.2 (FC04 read only). Always returns error."""
        return False, None, "FC02 not allowed (Unified Rule v1.2: FC04 read only)"

    def read_error_flags(self) -> tuple[bool, int | None, str | None]:
        """Unified Rule: Mainboard error_flags via FC04 mainboard reg1."""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC04", 0, 14)
                rr = self._client.read_input_registers(
                    address=0,
                    count=14,
                    unit=self._slave_id,
                )
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else [0] * 14
                regs = (regs + [0] * 14)[:14]
                flags = regs[1] & 0xFFFF
                return True, flags, None
            except Exception as e:
                return False, None, format_modbus_error(exc=e)

    def write_sub_coil_pulse(self, coil_index_0_to_4: int) -> tuple[bool, str | None]:
        """레거시: VB 펄스(891~895). 펌웨어에서 SUB_VB_COIL_COUNT=0 이면 미지원."""
        if SUB_VB_COIL_COUNT <= 0:
            return False, "VB coil pulse disabled (FC05 891~897 not supported by firmware)"
        if coil_index_0_to_4 < 0 or coil_index_0_to_4 >= SUB_VB_COIL_COUNT:
            return False, "Invalid coil index"
        addr = SUB_VB_COIL_BASE + coil_index_0_to_4
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC05", addr, 1)
                wr = self._client.write_coil(address=addr, value=True, unit=self._slave_id)
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)

    def read_holding_registers_direct(
        self, slave_id: int, start: int, count: int
    ) -> tuple[bool, list[int] | str]:
        """FC03 not allowed per Unified Rule v1.2 (FC04 read only). Use read_input_registers instead."""
        del slave_id, start, count
        return False, "FC03 not allowed (Unified Rule v1.2: use FC04 read_input_registers)"

    # ---- Generic test helpers (Mainboard only) ----
    # Unified Rule v1.2: FC01/FC02/FC03 금지. read_coils/read_discrete_inputs는 항상 에러 반환.
    def read_coils(self, start: int, count: int, unit: int | None = None) -> tuple[bool, list[bool] | None, str | None]:
        """FC01 not allowed per Unified Rule v1.2 (FC04 read only)."""
        del start, count, unit
        return False, None, "FC01 not allowed (Unified Rule v1.2: FC04 read only)"

    def read_discrete_inputs(self, start: int, count: int, unit: int | None = None) -> tuple[bool, list[bool] | None, str | None]:
        """FC02 not allowed per Unified Rule v1.2 (FC04 read only)."""
        del start, count, unit
        return False, None, "FC02 not allowed (Unified Rule v1.2: FC04 read only)"

    def read_input_registers(self, start: int, count: int, unit: int | None = None) -> tuple[bool, list[int] | None, str | None]:
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, None, err or "Not connected"
            u = self._slave_id if unit is None else unit
            try:
                if self._request_logger:
                    self._request_logger(u, "FC04", start, count)
                rr = self._client.read_input_registers(address=start, count=count, unit=u)
                if self._response_logger:
                    self._response_logger(not rr.isError(), _response_exception_code(rr))
                if rr.isError():
                    return False, None, format_modbus_error(resp=rr)
                regs = list(rr.registers) if rr.registers else []
                regs = (regs + [0] * count)[:count]
                return True, [r & 0xFFFF for r in regs], None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, None, format_modbus_error(exc=e)

    def write_single_coil(self, addr: int, value: bool, unit: int | None = None) -> tuple[bool, str | None]:
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            u = self._slave_id if unit is None else unit
            try:
                val_int = 1 if value else 0
                if self._request_logger:
                    self._request_logger(u, "FC05", addr, val_int)
                wr = self._client.write_coil(address=addr, value=value, unit=u)
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)

    def write_multiple_coils(self, start: int, values: list[bool], unit: int | None = None) -> tuple[bool, str | None]:
        """FC15 not allowed per Unified Rule v1.2 (FC05 write only)."""
        del start, values, unit
        return False, "FC15 not allowed (Unified Rule v1.2: FC05 write only)"

    def write_coil_direct(self, slave_id: int, coil_addr: int, value: bool) -> tuple[bool, str | None]:
        """FC05 직접 전송 (메인보드 경유 없음). raw_only 연결이면 시리얼로 프레임만 전송."""
        with self._lock:
            if self._raw_serial and self._raw_serial.is_open:
                try:
                    val_int = 1 if value else 0
                    if self._request_logger:
                        self._request_logger(slave_id, "FC05", coil_addr, val_int)
                    frame = build_fc05_rtu_frame(slave_id, coil_addr, value)
                    hex_str = " ".join(f"{b:02X}" for b in frame)
                    if self._tx_frame_hex_logger:
                        self._tx_frame_hex_logger(f"[DIRECT] TX frame (hex): {hex_str}")
                    self._raw_serial.write(frame)
                    if self._response_logger:
                        self._response_logger(True, None)
                    return True, None
                except Exception as e:
                    if self._response_logger:
                        self._response_logger(False, None)
                    return False, format_modbus_error(exc=e)
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                val_int = 1 if value else 0
                if self._request_logger:
                    self._request_logger(slave_id, "FC05", coil_addr, val_int)
                frame = build_fc05_rtu_frame(slave_id, coil_addr, value)
                hex_str = " ".join(f"{b:02X}" for b in frame)
                if self._tx_frame_hex_logger:
                    self._tx_frame_hex_logger(f"[DIRECT] TX frame (hex): {hex_str}")
                wr = self._client.write_coil(address=coil_addr, value=value, unit=slave_id)
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)

    def write_sub_coil(self, addr: int, value: bool) -> tuple[bool, str | None]:
        """Unified Rule: Mainboard FC05 coil write.
        Unified addresses: coil0..3=Relay1..4, coil4=PC_ON, coil5=PC_OFF, coil6=RESET.
        (Legacy addresses도 펌웨어가 유지하는 경우는 그대로 통과)"""
        with self._lock:
            ok, err = self._ensure_socket_open()
            if not ok:
                return False, err or "Not connected"
            try:
                val_int = 1 if value else 0
                if self._request_logger:
                    self._request_logger(self._slave_id, "FC05", addr, val_int)
                wr = self._client.write_coil(address=addr, value=value, unit=self._slave_id)
                if self._response_logger:
                    self._response_logger(not wr.isError(), _response_exception_code(wr))
                if wr.isError():
                    return False, format_modbus_error(resp=wr)
                return True, None
            except Exception as e:
                if self._response_logger:
                    self._response_logger(False, None)
                return False, format_modbus_error(exc=e)

    def _write_pc_reg(self, address: int, value: int) -> tuple[bool, str | None]:
        """FC06 not allowed per Unified Rule v1.2 (FC05 write only)."""
        del address, value
        return False, "FC06 not allowed (Unified Rule v1.2: FC05 write only)"
