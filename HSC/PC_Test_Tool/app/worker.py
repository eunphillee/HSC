"""
Worker thread: runs Modbus reads so UI does not block. Emits results for UI update.
"""
from PyQt6.QtCore import QObject, QThread, pyqtSignal, QTimer


class PollWorker(QObject):
    """Runs in a separate thread; performs all Modbus reads and emits results."""
    # (ok, bits or regs list or None, error_message or None)
    onoff_result = pyqtSignal(bool, object, object)
    door_result = pyqtSignal(bool, object, object)
    alarms_result = pyqtSignal(bool, object, object)
    cmd_onoff_result = pyqtSignal(bool, object, object)
    currents_result = pyqtSignal(bool, object, object)

    def __init__(self, client):
        super().__init__()
        self._client = client
        self._running = False

    def set_running(self, running: bool):
        self._running = running

    def on_request_poll(self):
        """Called from main thread via signal; runs in worker thread."""
        self.poll_once()

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


def create_worker_and_thread(client):
    """Create worker in a new thread; caller must start the thread."""
    thread = QThread()
    worker = PollWorker(client)
    worker.moveToThread(thread)
    return thread, worker
