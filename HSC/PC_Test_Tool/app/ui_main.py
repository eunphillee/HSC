"""
Main window: connection panel, live read panels (FC02 x4 + FC03), control, log.
All Modbus reads run in worker thread; timer in main thread triggers poll.
"""
import sys
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QGridLayout,
    QComboBox, QPushButton, QLabel, QSpinBox, QCheckBox, QTableWidget,
    QTableWidgetItem, QScrollArea, QFrame, QMessageBox,
)
from PyQt6.QtCore import QTimer, pyqtSignal, Qt
from PyQt6.QtGui import QFont

from .modbus_client import ModbusClient
from .worker import PollWorker, create_worker_and_thread
from .logger import LogHandler, LogPanel
from .h2tech_map import (
    ONOFF_START, ONOFF_COUNT, DOOR_START, DOOR_COUNT,
    ALARMS_START, ALARMS_COUNT, CMD_ONOFF_START, CMD_ONOFF_COUNT,
    CURRENT_START, CURRENT_COUNT,
    DOOR_OPEN_1_COIL, DOOR_OPEN_2_COIL,
    VB_ONOFF_8_COIL, VB_ONOFF_12_COIL,
    INVALID_COIL_899, INVALID_COIL_900,
)


def list_serial_ports():
    """List available serial ports (cross-platform)."""
    try:
        import serial.tools.list_ports
        return [p.device for p in serial.tools.list_ports.comports()]
    except Exception:
        return []


class MainWindow(QMainWindow):
    request_poll = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HSC PC Test Tool — MAIN Modbus RTU")
        self._client = ModbusClient()
        self._log = LogHandler()
        self._poll_timer = QTimer(self)
        self._poll_timer.timeout.connect(self._on_timer)
        self._thread, self._worker = create_worker_and_thread(self._client)
        self._thread.start()
        self.request_poll.connect(self._worker.on_request_poll, Qt.ConnectionType.QueuedConnection)
        self._connect_worker_signals()
        self._build_ui()
        self._refresh_ports()

    def _connect_worker_signals(self):
        self._worker.onoff_result.connect(self._on_onoff)
        self._worker.door_result.connect(self._on_door)
        self._worker.alarms_result.connect(self._on_alarms)
        self._worker.cmd_onoff_result.connect(self._on_cmd_onoff)
        self._worker.currents_result.connect(self._on_currents)

    def _build_ui(self):
        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self._connection_group())
        layout.addWidget(self._read_group())
        layout.addWidget(self._control_group())
        layout.addWidget(LogPanel(self._log))
        self.setCentralWidget(central)
        self.resize(800, 700)

    def _connection_group(self) -> QGroupBox:
        g = QGroupBox("Connection")
        L = QHBoxLayout(g)
        L.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(120)
        L.addWidget(self._port_combo)
        btn_refresh = QPushButton("Refresh")
        btn_refresh.clicked.connect(self._refresh_ports)
        L.addWidget(btn_refresh)
        L.addWidget(QLabel("Baud:"))
        self._baud = QSpinBox()
        self._baud.setRange(1200, 115200)
        self._baud.setValue(9600)
        L.addWidget(self._baud)
        L.addWidget(QLabel("Slave ID:"))
        self._slave_id = QSpinBox()
        self._slave_id.setRange(1, 247)
        self._slave_id.setValue(1)
        L.addWidget(self._slave_id)
        self._btn_connect = QPushButton("Connect")
        self._btn_connect.clicked.connect(self._do_connect)
        L.addWidget(self._btn_connect)
        self._btn_disconnect = QPushButton("Disconnect")
        self._btn_disconnect.clicked.connect(self._do_disconnect)
        self._btn_disconnect.setEnabled(False)
        L.addWidget(self._btn_disconnect)
        L.addStretch()
        return g

    def _read_group(self) -> QGroupBox:
        g = QGroupBox("Live Read (FC02 / FC03)")
        layout = QVBoxLayout(g)
        row = QHBoxLayout()
        self._auto_poll = QCheckBox("Auto poll")
        self._auto_poll.stateChanged.connect(self._on_auto_poll_changed)
        row.addWidget(self._auto_poll)
        row.addWidget(QLabel("Interval (ms):"))
        self._poll_interval = QSpinBox()
        self._poll_interval.setRange(100, 5000)
        self._poll_interval.setValue(500)
        self._poll_interval.valueChanged.connect(self._set_poll_interval)
        row.addWidget(self._poll_interval)
        row.addStretch()
        layout.addLayout(row)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_w = QWidget()
        grid = QVBoxLayout(scroll_w)

        # ON/OFF 1~16
        g1 = QGroupBox("1) ON/OFF status 1~16 (FC02 start=820 count=16)")
        l1 = QVBoxLayout(g1)
        self._onoff_label = QLabel("—")
        self._onoff_label.setFont(QFont("Monospace", 10))
        l1.addWidget(self._onoff_label)
        btn = QPushButton("Read once")
        btn.clicked.connect(self._read_onoff_once)
        l1.addWidget(btn)
        grid.addWidget(g1)

        # Door
        g2 = QGroupBox("2) Door sensors (FC02 start=852 count=8)")
        l2 = QVBoxLayout(g2)
        self._door_label = QLabel("—")
        self._door_label.setFont(QFont("Monospace", 10))
        l2.addWidget(self._door_label)
        btn = QPushButton("Read once")
        btn.clicked.connect(self._read_door_once)
        l2.addWidget(btn)
        grid.addWidget(g2)

        # Alarms
        g3 = QGroupBox("3) Alarms 1~12 (FC02 start=868 count=12)")
        l3 = QVBoxLayout(g3)
        self._alarms_label = QLabel("—")
        self._alarms_label.setFont(QFont("Monospace", 10))
        l3.addWidget(self._alarms_label)
        btn = QPushButton("Read once")
        btn.clicked.connect(self._read_alarms_once)
        l3.addWidget(btn)
        grid.addWidget(g3)

        # CMD ON/OFF 1~7
        g4 = QGroupBox("4) CMD ON/OFF 1~7 (FC02 start=884 count=7)")
        l4 = QVBoxLayout(g4)
        self._cmd_onoff_label = QLabel("—")
        self._cmd_onoff_label.setFont(QFont("Monospace", 10))
        l4.addWidget(self._cmd_onoff_label)
        btn = QPushButton("Read once")
        btn.clicked.connect(self._read_cmd_onoff_once)
        l4.addWidget(btn)
        grid.addWidget(g4)

        # Currents
        g5 = QGroupBox("5) Currents (FC03 start=2000 count=14 only)")
        l5 = QVBoxLayout(g5)
        self._current_table = QTableWidget(14, 2)
        self._current_table.setHorizontalHeaderLabels(["Register", "Value"])
        for r in range(14):
            self._current_table.setItem(r, 0, QTableWidgetItem(""))
            self._current_table.setItem(r, 1, QTableWidgetItem("—"))
        labels = [
            "2000 HPSB P1", "2001 HPSB P2", "2002 HPSB P3",
            "2003 LPSB1 P1", "2004 LPSB1 P2", "2005 LPSB1 P3",
            "2006 LPSB2 P1", "2007 LPSB2 P2", "2008 LPSB2 P3",
            "2009 LPSB3 P1", "200A LPSB3 P2", "200B LPSB3 P3",
            "200C Door1", "200D Door2",
        ]
        for r, lb in enumerate(labels):
            self._current_table.item(r, 0).setText(lb)
        btn = QPushButton("Read once")
        btn.clicked.connect(self._read_currents_once)
        l5.addWidget(self._current_table)
        l5.addWidget(btn)
        grid.addWidget(g5)

        scroll.setWidget(scroll_w)
        layout.addWidget(scroll)
        return g

    def _control_group(self) -> QGroupBox:
        g = QGroupBox("Control (FC05 Write)")
        L = QHBoxLayout(g)
        L.addWidget(QLabel("Door open:"))
        btn_d1 = QPushButton("Door 1 (1x0897)")
        btn_d1.clicked.connect(self._write_door1)
        L.addWidget(btn_d1)
        btn_d2 = QPushButton("Door 2 (1x0898)")
        btn_d2.clicked.connect(self._write_door2)
        L.addWidget(btn_d2)
        L.addWidget(QLabel("  VB ON/OFF 8~12:"))
        for i in range(8, 13):
            b = QPushButton(f"{i}")
            b.clicked.connect(lambda checked=False, x=i: self._write_vb(x))
            L.addWidget(b)
        L.addWidget(QLabel("  Exception test:"))
        b899 = QPushButton("0899 (0x02)")
        b899.clicked.connect(self._write_invalid_899)
        L.addWidget(b899)
        b900 = QPushButton("0900 (0x02)")
        b900.clicked.connect(self._write_invalid_900)
        L.addWidget(b900)
        L.addStretch()
        return g

    def _refresh_ports(self):
        self._port_combo.clear()
        for p in list_serial_ports():
            self._port_combo.addItem(p)
        if self._port_combo.count() == 0:
            self._port_combo.addItem("(no ports)")

    def _do_connect(self):
        port = self._port_combo.currentText() if self._port_combo.currentIndex() >= 0 else ""
        if not port or port == "(no ports)":
            QMessageBox.warning(self, "Connection", "Select a valid port.")
            return
        ok, msg = self._client.connect(
            port,
            baudrate=self._baud.value(),
            slave_id=self._slave_id.value(),
        )
        if ok:
            self._btn_connect.setEnabled(False)
            self._btn_disconnect.setEnabled(True)
            self._port_combo.setEnabled(False)
            self._worker.set_running(True)
            self._log.log("Connect", port, self._baud.value(), "OK")
        else:
            QMessageBox.warning(self, "Connection", msg)
            self._log.log("Connect", port, self._baud.value(), "Fail", msg)

    def _do_disconnect(self):
        self._worker.set_running(False)
        self._poll_timer.stop()
        self._auto_poll.setChecked(False)
        self._client.disconnect()
        self._btn_connect.setEnabled(True)
        self._btn_disconnect.setEnabled(False)
        self._port_combo.setEnabled(True)
        self._log.log("Disconnect", "", "", "OK")

    def _on_auto_poll_changed(self, state):
        if state:  # checked
            self._worker.set_running(self._client.connected)
            self._poll_timer.start(self._poll_interval.value())
        else:
            self._poll_timer.stop()

    def _set_poll_interval(self, ms: int):
        if self._poll_timer.isActive():
            self._poll_timer.start(ms)

    def _on_timer(self):
        self.request_poll.emit()

    def _read_onoff_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_onoff()
        self._log.log("FC02", ONOFF_START, ONOFF_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_onoff(ok, data, err)

    def _read_door_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_door_sensors()
        self._log.log("FC02", DOOR_START, DOOR_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_door(ok, data, err)

    def _read_alarms_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_alarms()
        self._log.log("FC02", ALARMS_START, ALARMS_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_alarms(ok, data, err)

    def _read_cmd_onoff_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_cmd_onoff()
        self._log.log("FC02", CMD_ONOFF_START, CMD_ONOFF_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_cmd_onoff(ok, data, err)

    def _read_currents_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_currents()
        self._log.log("FC03", CURRENT_START, CURRENT_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_currents(ok, data, err)

    def _apply_onoff(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            self._onoff_label.setText(" ".join(str(b) for b in data[:16]))
        else:
            self._onoff_label.setText(err or "—")

    def _apply_door(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            self._door_label.setText(" ".join(str(b) for b in data[:8]))
        else:
            self._door_label.setText(err or "—")

    def _apply_alarms(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            self._alarms_label.setText(" ".join(str(b) for b in data[:12]))
        else:
            self._alarms_label.setText(err or "—")

    def _apply_cmd_onoff(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            self._cmd_onoff_label.setText(" ".join(str(b) for b in data[:7]))
        else:
            self._cmd_onoff_label.setText(err or "—")

    def _apply_currents(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            for r in range(min(14, len(data))):
                self._current_table.item(r, 1).setText(str(data[r]))
            for r in range(len(data), 14):
                self._current_table.item(r, 1).setText("—")
        else:
            for r in range(14):
                self._current_table.item(r, 1).setText(err or "—")

    def _on_onoff(self, ok: bool, data, err):
        self._apply_onoff(ok, data, err)

    def _on_door(self, ok: bool, data, err):
        self._apply_door(ok, data, err)

    def _on_alarms(self, ok: bool, data, err):
        self._apply_alarms(ok, data, err)

    def _on_cmd_onoff(self, ok: bool, data, err):
        self._apply_cmd_onoff(ok, data, err)

    def _on_currents(self, ok: bool, data, err):
        self._apply_currents(ok, data, err)

    def _write_door1(self):
        if not self._client.connected:
            return
        ok, err = self._client.write_door_open_1()
        self._log.log("FC05", DOOR_OPEN_1_COIL, 1, "OK" if ok else "Fail", err or "")
        if not ok and err:
            QMessageBox.warning(self, "Write", err)

    def _write_door2(self):
        if not self._client.connected:
            return
        ok, err = self._client.write_door_open_2()
        self._log.log("FC05", DOOR_OPEN_2_COIL, 1, "OK" if ok else "Fail", err or "")
        if not ok and err:
            QMessageBox.warning(self, "Write", err)

    def _write_vb(self, index: int):
        if not self._client.connected:
            return
        ok, err = self._client.write_vb_onoff(index)
        self._log.log("FC05", f"VB{index}", 1, "OK" if ok else "Fail", err or "")
        if not ok and err:
            QMessageBox.warning(self, "Write", err)

    def _write_invalid_899(self):
        if not self._client.connected:
            return
        ok, err = self._client.write_invalid_coil_899()
        self._log.log("FC05", INVALID_COIL_899, 1, "Expect 0x02", err or "")
        if not ok and err:
            QMessageBox.information(self, "Exception test", f"Expected exception; got: {err}")

    def _write_invalid_900(self):
        if not self._client.connected:
            return
        ok, err = self._client.write_invalid_coil_900()
        self._log.log("FC05", INVALID_COIL_900, 1, "Expect 0x02", err or "")
        if not ok and err:
            QMessageBox.information(self, "Exception test", f"Expected exception; got: {err}")

    def closeEvent(self, event):
        self._worker.set_running(False)
        self._poll_timer.stop()
        self._client.disconnect()
        self._thread.quit()
        self._thread.wait(1000)
        event.accept()