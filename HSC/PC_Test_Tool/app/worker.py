"""
Worker thread: runs Modbus reads so UI does not block. Emits results for UI update.
"""
from PyQt6.QtCore import QObject, QThread, pyqtSignal, QTimer

from .h2tech_map import MAIN_IO_ENABLED


class PollWorker(QObject):
    """Runs in a separate thread; performs all Modbus reads and emits results."""
    onoff_result = pyqtSignal(bool, object, object)
    door_result = pyqtSignal(bool, object, object)
    alarms_result = pyqtSignal(bool, object, object)
    cmd_onoff_result = pyqtSignal(bool, object, object)
    currents_result = pyqtSignal(bool, object, object)
    main_io_result = pyqtSignal(bool, object, object, object, bool)  # ok, di_bits, do_bits, err, from_toggle

    def __init__(self, client):
        super().__init__()
        self._client = client
        self._running = False

    def set_running(self, running: bool):
        self._running = running

    def on_request_poll(self):
        """Called from main thread via signal; runs in worker thread."""
        self.poll_once()

    def on_request_main_do_toggle(self, index: int):
        """Toggle MAIN DO bit (0..3); read, flip, write, emit main_io_result."""
        if not MAIN_IO_ENABLED or not self._client.connected:
            return
        ok, di, do, err = self._client.read_main_io()
        if not ok or do is None:
            self.main_io_result.emit(ok, di, do, err, True)
            return
        do = list(do)
        if 0 <= index < 4:
            do[index] = 1 - do[index]
        ok2, err2 = self._client.write_main_do_bitmap(do)
        if ok2:
            ok, di, do, err = self._client.read_main_io()
        self.main_io_result.emit(ok, di, do, err2 or err, True)

    def on_request_main_io_read(self):
        """Read MAIN IO once and emit main_io_result (from_toggle=False)."""
        if not MAIN_IO_ENABLED or not self._client.connected:
            return
        ok, di, do, err = self._client.read_main_io()
        self.main_io_result.emit(ok, di, do, err, False)

    def poll_once(self):
        """Run one full poll of all read blocks."""
        if not self._running or not self._client.connected:
            return
        ok, data, err = self._client.read_onoff()
        self.onoff_result.emit(ok, data, err)
        if not self._running:
            return
        ok, data, err = self._client.read_door_sensors()
        self.door_result.emit(ok, data, err)
        if not self._running:
            return
        ok, data, err = self._client.read_alarms()
        self.alarms_result.emit(ok, data, err)
        if not self._running:
            return
        ok, data, err = self._client.read_cmd_onoff()
        self.cmd_onoff_result.emit(ok, data, err)
        if not self._running:
            return
        ok, data, err = self._client.read_currents()
        self.currents_result.emit(ok, data, err)
        if not self._running:
            return
        if MAIN_IO_ENABLED:
            ok, di, do, err = self._client.read_main_io()
            self.main_io_result.emit(ok, di, do, err, False)


def create_worker_and_thread(client):
    """Create worker in a new thread; caller must start the thread."""
    thread = QThread()
    worker = PollWorker(client)
    worker.moveToThread(thread)
    return thread, worker
