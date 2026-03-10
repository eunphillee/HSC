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
        """PC_ON_EN 100ms high 펄스: High 출력 후 100ms 뒤 Low."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok1, err1 = self._client.write_pc_on_en(True)
        if not ok1:
            self.write_result.emit(False, err1)
            return
        time.sleep(0.1)
        ok2, err2 = self._client.write_pc_on_en(False)
        self.write_result.emit(ok2, err2 or err1)

    def on_request_pc_reset_pulse(self):
        """PC_RESET_EN 100ms high 펄스: High 출력 후 100ms 뒤 Low."""
        if not self._client.connected:
            self.write_result.emit(False, "Not connected")
            return
        ok1, err1 = self._client.write_pc_reset_en(True)
        if not ok1:
            self.write_result.emit(False, err1)
            return
        time.sleep(0.1)
        ok2, err2 = self._client.write_pc_reset_en(False)
        self.write_result.emit(ok2, err2 or err1)

    def on_request_read_raw(self):
        """Read raw bytes from serial (e.g. board TX 0xAA) and emit for log."""
        if not self._client.connected:
            return
        buf = self._client.read_raw_available()
        if buf:
            self.raw_bytes_received.emit(buf)


def create_worker_and_thread(client):
    """Create worker in a new thread; caller must start the thread."""
    thread = QThread()
    worker = MainboardWorker(client)
    worker.moveToThread(thread)
    return thread, worker
