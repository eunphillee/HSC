"""
Worker thread: runs Mainboard Modbus read/write so UI does not block.
"""
import time
from PyQt6.QtCore import QObject, QThread, pyqtSignal, Qt


class MainboardWorker(QObject):
    """Runs in a separate thread; performs Mainboard Modbus I/O and emits results."""
    di_result = pyqtSignal(bool, object, object)   # ok, bits[8], err
    pc_led_result = pyqtSignal(bool, object, object)  # ok, state, err
    env_result = pyqtSignal(bool, object, object)     # ok, (temp_c, rh_pct), err
    write_result = pyqtSignal(bool, object)       # ok, err
    raw_bytes_received = pyqtSignal(list)          # raw RX bytes (e.g. 0xAA from board)
    # HPSB/LPSB: ok, sense[14], coil_bits[14], error_flags u16, err
    sub_data_result = pyqtSignal(bool, object, object, object, object)

    def __init__(self, client):
        super().__init__()
        self._client = client

    def on_request_read_di(self):
        if not self._client.connected:
            self.di_result.emit(False, None, "Not connected")
            return
        ok, bits, err = self._client.read_di_bitmap()
        self.di_result.emit(ok, bits, err)

    def on_request_read_pc_led(self):
        if not self._client.connected:
            self.pc_led_result.emit(False, None, "Not connected")
            return
        ok, state, err = self._client.read_pc_led_in()
        self.pc_led_result.emit(ok, state, err)

    def on_request_read_env(self):
        if not self._client.connected:
            self.env_result.emit(False, None, "Not connected")
            return
        ok, val, err = self._client.read_env_shtc3()
        self.env_result.emit(ok, val, err)

    def on_request_write_relay(self, ch: int, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_relay(ch, onoff)
        self.write_result.emit(ok, err)

    def on_request_write_pc_on_en(self, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_on_en(onoff)
        self.write_result.emit(ok, err)

    def on_request_write_pc_reset_en(self, onoff: bool):
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_reset_en(onoff)
        self.write_result.emit(ok, err)

    def on_request_pc_on_pulse(self):
        """PC_ON_EN: FC06 2120 value=1 → 보드에서 500ms HIGH 펄스 수행."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_on_en(True)
        self.write_result.emit(ok, err)

    def on_request_pc_reset_pulse(self):
        """PC_RESET_EN: FC06 2121 value=1 → 보드에서 500ms HIGH 펄스 수행."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_pc_reset_en(True)
        self.write_result.emit(ok, err)

    def on_request_read_raw(self):
        """Read raw bytes from serial (e.g. board TX 0xAA) and emit for log."""
        if not self._client.connected:
            return
        buf = self._client.read_raw_available()
        if buf:
            self.raw_bytes_received.emit(buf)

    def on_request_read_sub(self):
        """Read HPSB/LPSB: sense(14), coil status(14), error_flags. Emit sub_data_result."""
        if not self._client.connected:
            self.sub_data_result.emit(False, None, None, None, "Not connected")
            return
        ok_s, sense, err_s = self._client.read_sub_sense()
        ok_c, coils, err_c = self._client.read_sub_coil_status()
        ok_f, flags, err_f = self._client.read_error_flags()
        if not ok_s:
            self.sub_data_result.emit(False, None, None, None, err_s or "sense fail")
            return
        if not ok_c:
            self.sub_data_result.emit(False, sense, None, (flags if ok_f else None), err_c or "coil fail")
            return
        self.sub_data_result.emit(True, sense, coils, (flags if ok_f else 0), None)

    def on_request_sub_pulse(self, coil_index: int):
        """FC05 pulse one LPSB VB (coil_index 0..4 = VB 8..12). Emit write_result."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_sub_coil_pulse(coil_index)
        self.write_result.emit(ok, err)

    def on_request_write_sub_coil(self, addr: int, value: bool):
        """FC05 write sub-board coil (addr 898..909). Mainboard forwards to HPSB/LPSB. Emit write_result."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok, err = self._client.write_sub_coil(addr, value)
        self.write_result.emit(ok, err)

    def on_request_diagnostic_sequence(self):
        """
        Run LED diagnostic sequence: HPSB RELAY1/2/3 ON then OFF, then LPSB1 SSR1/2/3 ON then OFF.
        Each step sends FC05 via Mainboard; observe LED2/3/4 on HPSB and LPSB to verify communication.
        """
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        # (addr, value): 898..900 = HPSB coil 0,1,2; 901..903 = LPSB1 coil 0,1,2
        steps = [
            (898, True), (898, False),   # HPSB RELAY1 -> LED2
            (899, True), (899, False),   # HPSB RELAY2 -> LED3
            (900, True), (900, False),   # HPSB RELAY3 -> LED4
            (901, True), (901, False),   # LPSB1 SSR1 -> LED2
            (902, True), (902, False),   # LPSB1 SSR2 -> LED3
            (903, True), (903, False),   # LPSB1 SSR3 -> LED4
        ]
        delay_on_s = 1.2   # time to see LED on
        delay_off_s = 0.6  # pause before next
        for addr, value in steps:
            ok, err = self._client.write_sub_coil(addr, value)
            self.write_result.emit(ok, err if not ok else None)
            time.sleep(delay_on_s if value else delay_off_s)
        self.write_result.emit(True, "Diagnostic sequence done")


def create_worker_and_thread(client):
    """Create worker in a new thread; caller must start the thread."""
    thread = QThread()
    worker = MainboardWorker(client)
    worker.moveToThread(thread)
    return thread, worker
