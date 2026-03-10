"""
Main window: Mainboard 최소 테스트 툴 — Connection, Mainboard I/O (4 out, 8 in), PC Status (2 out, 1 in), Log.
All Modbus RTU to single Mainboard slave. Exceptions handled safely; no crash on timeout.
"""
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QComboBox, QPushButton, QLabel, QSpinBox, QFrame, QMessageBox,
    QPlainTextEdit, QGroupBox, QCheckBox, QApplication,
)
from PyQt6.QtCore import pyqtSignal, Qt, QTimer
from PyQt6.QtGui import QFont

import pymodbus
from pymodbus.client.sync import ModbusSerialClient

from .modbus_client import ModbusClient
from .worker import MainboardWorker, create_worker_and_thread
from .logger import LogHandler
from .address_map import MAINBOARD_SLAVE_ID_DEFAULT, MAIN_DI_REG, MAIN_DO_REG, PC_ON_EN_REG, PC_RESET_EN_REG, PC_LED_IN_REG


DARK_QSS = """
QMainWindow, QWidget { background-color: #1e1e1e; }
QFrame#card { background-color: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 6px; padding: 8px; }
QGroupBox { font-weight: bold; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; margin-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }
QLabel { color: #e0e0e0; }
QComboBox, QSpinBox { background-color: #252525; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; padding: 4px; min-height: 20px; }
QPushButton { background-color: #252525; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; padding: 6px 12px; min-height: 20px; }
QPushButton:hover { background-color: #2a2a2a; border-color: #2d8cf0; }
QPushButton:pressed { background-color: #2d8cf0; color: white; }
QPushButton:disabled { background-color: #252525; color: #555555; border-color: #2a2a2a; }
QCheckBox { color: #e0e0e0; spacing: 6px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 3px; border: 2px solid #3a3a3a; background: #252525; }
QCheckBox::indicator:checked { background: #2d8cf0; border-color: #2d8cf0; }
QPlainTextEdit { background-color: #1e1e1e; color: #e0e0e0; font-family: Consolas, Monaco, monospace; font-size: 12px; border: 1px solid #3a3a3a; border-radius: 4px; }
"""


def list_serial_ports():
    try:
        import serial.tools.list_ports
        return [p.device for p in serial.tools.list_ports.comports()]
    except Exception:
        return []


class LedIndicator(QFrame):
    """Small LED: on=#00c853, off=#555555. For generic on/off."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(14, 14)
        self.setStyleSheet("border-radius: 7px; background-color: #555555;")
        self._on = False

    def set_state(self, on: bool):
        self._on = on
        if on:
            self.setStyleSheet("border-radius: 7px; background-color: #00c853;")
        else:
            self.setStyleSheet("border-radius: 7px; background-color: #555555;")


class DiLedIndicator(QFrame):
    """DI/PC_LED용 원: 입력 없음=파란색, 입력 있음=빨간색, 미확인=회색."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(14, 14)
        self.setStyleSheet("border-radius: 7px; background-color: #555555;")
        self._state = None  # None=미확인, True=입력 있음(빨강), False=입력 없음(파랑)

    def set_di_state(self, has_input: bool | None):
        """has_input: True=입력 있음(빨강), False=입력 없음(파랑), None=미확인(회색)."""
        self._state = has_input
        if has_input is None:
            self.setStyleSheet("border-radius: 7px; background-color: #555555;")
        elif has_input:
            self.setStyleSheet("border-radius: 7px; background-color: #e53935;")
        else:
            self.setStyleSheet("border-radius: 7px; background-color: #2196F3;")


def card_frame(title: str = "") -> tuple[QFrame, QVBoxLayout]:
    f = QFrame()
    f.setObjectName("card")
    f.setStyleSheet("QFrame#card { background-color: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 6px; padding: 8px; }")
    lay = QVBoxLayout(f)
    lay.setSpacing(8)
    if title:
        lbl = QLabel(title)
        lbl.setStyleSheet("font-weight: bold; color: #e0e0e0; font-size: 13px;")
        lay.addWidget(lbl)
    return f, lay


class MainWindow(QMainWindow):
    request_read_di = pyqtSignal()
    request_read_pc_led = pyqtSignal()
    request_write_relay = pyqtSignal(int, bool)
    request_pc_on_pulse = pyqtSignal()   # 클릭 시 100ms high 펄스
    request_pc_reset_pulse = pyqtSignal()
    request_read_env = pyqtSignal()      # SHTC3 env read (FC03 2110 cnt=2)
    request_read_raw = pyqtSignal()  # 보드→PC raw 수신 (0xAA 등) 로그용

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Mainboard 최소 테스트 툴 — Modbus RTU")
        self.setStyleSheet(DARK_QSS)
        self._client = ModbusClient()
        self._log = LogHandler()
        try:
            self._log.log_info(
                f"pymodbus {getattr(pymodbus, '__version__', '?')} | client={ModbusSerialClient.__module__}.{ModbusSerialClient.__name__}"
            )
        except Exception as e:
            self._log.log_info(f"Startup: {type(e).__name__}: {e}")
        self._client.set_request_logger(lambda u, f, a, c: self._log.log_request(u, f, a, c))
        self._client.set_response_logger(lambda ok, exc: self._log.log_response(ok, exc))
        self._thread, self._worker = create_worker_and_thread(self._client)
        self._thread.start()
        self._connect_worker_signals()
        self._log_lines: list[str] = []
        self._raw_poll_timer = QTimer(self)
        self._raw_poll_timer.setInterval(100)
        self._raw_poll_timer.timeout.connect(self._poll_raw_rx)
        self._env_poll_timer = QTimer(self)
        self._env_poll_timer.setInterval(5000)  # 5초 주기 (1초→5초: 통신 안정성 확인용)
        self._env_poll_timer.timeout.connect(lambda: self.request_read_env.emit())
        self._seen_0xaa_since_connect = False
        self._modbus_fail_0xaa_hint_shown = False
        self._build_ui()
        self._refresh_ports()
        self._set_connected_ui(False)

    def _connect_worker_signals(self):
        self.request_read_di.connect(self._worker.on_request_read_di, Qt.ConnectionType.QueuedConnection)
        self.request_read_pc_led.connect(self._worker.on_request_read_pc_led, Qt.ConnectionType.QueuedConnection)
        self.request_write_relay.connect(self._worker.on_request_write_relay, Qt.ConnectionType.QueuedConnection)
        self.request_pc_on_pulse.connect(self._worker.on_request_pc_on_pulse, Qt.ConnectionType.QueuedConnection)
        self.request_pc_reset_pulse.connect(self._worker.on_request_pc_reset_pulse, Qt.ConnectionType.QueuedConnection)
        self.request_read_env.connect(self._worker.on_request_read_env, Qt.ConnectionType.QueuedConnection)
        self.request_read_raw.connect(self._worker.on_request_read_raw, Qt.ConnectionType.QueuedConnection)
        self._worker.di_result.connect(self._on_di_result)
        self._worker.pc_led_result.connect(self._on_pc_led_result)
        self._worker.env_result.connect(self._on_env_result)
        self._worker.write_result.connect(self._on_write_result)
        self._worker.raw_bytes_received.connect(self._on_raw_bytes)

    def _build_ui(self):
        central = QWidget()
        central.setStyleSheet("background-color: #1e1e1e;")
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(12)
        main_layout.setContentsMargins(12, 12, 12, 12)

        # ---- A) Connection Bar ----
        top = QWidget()
        top_lay = QHBoxLayout(top)
        top_lay.setSpacing(12)
        top_lay.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(140)
        top_lay.addWidget(self._port_combo)
        btn_refresh = QPushButton("Refresh")
        btn_refresh.clicked.connect(self._refresh_ports)
        top_lay.addWidget(btn_refresh)
        top_lay.addWidget(QLabel("Baud:"))
        self._baud = QSpinBox()
        self._baud.setRange(9600, 9600)
        self._baud.setValue(9600)
        self._baud.setMinimumWidth(70)
        self._baud.setEnabled(False)
        top_lay.addWidget(self._baud)
        top_lay.addWidget(QLabel("Slave ID:"))
        self._slave_id = QSpinBox()
        self._slave_id.setRange(1, 247)
        self._slave_id.setValue(MAINBOARD_SLAVE_ID_DEFAULT)
        self._slave_id.setMinimumWidth(50)
        top_lay.addWidget(self._slave_id)
        self._btn_connect = QPushButton("Connect")
        self._btn_connect.clicked.connect(self._do_connect)
        top_lay.addWidget(self._btn_connect)
        self._btn_disconnect = QPushButton("Disconnect")
        self._btn_disconnect.clicked.connect(self._do_disconnect)
        self._btn_disconnect.setEnabled(False)
        top_lay.addWidget(self._btn_disconnect)
        self._status_badge = QLabel("Disconnected")
        self._status_badge.setStyleSheet("color: #555555; font-weight: bold; padding: 4px 8px;")
        top_lay.addWidget(self._status_badge)
        top_lay.addStretch()
        main_layout.addWidget(top)

        # ---- B) Mainboard Outputs (4) ----
        card_out, lay_out = card_frame("Mainboard Outputs (Relay1~4)")
        out_labels = ["RELAY1_EN", "RELAY2_EN", "RELAY3_EN", "RELAY4_EN"]
        self._relay_checks = []
        grid_out = QGridLayout()
        for i in range(4):
            chk = QCheckBox(out_labels[i])
            chk.stateChanged.connect(lambda s, idx=i: self._on_relay_toggle(idx, s == Qt.CheckState.Checked.value))
            self._relay_checks.append(chk)
            grid_out.addWidget(chk, i // 2, i % 2)
        lay_out.addLayout(grid_out)
        main_layout.addWidget(card_out)

        # ---- C) Mainboard Inputs (8) + Read DI ----
        card_in, lay_in = card_frame("Mainboard Inputs (DI_01~DI_08)")
        self._di_leds = []
        grid_di = QGridLayout()
        for i in range(8):
            led = DiLedIndicator()
            lbl = QLabel(f"DI_{i+1:02d}")
            lbl.setStyleSheet("color: #e0e0e0;")
            grid_di.addWidget(led, i // 4, (i % 4) * 2)
            grid_di.addWidget(lbl, i // 4, (i % 4) * 2 + 1)
            self._di_leds.append(led)
        lay_in.addLayout(grid_di)
        btn_read_di = QPushButton("Read DI")
        btn_read_di.clicked.connect(lambda: self.request_read_di.emit())
        lay_in.addWidget(btn_read_di)
        self._btn_read_di = btn_read_di
        main_layout.addWidget(card_in)

        # ---- D) PC Status ----
        card_pc, lay_pc = card_frame("PC Status")
        lay_pc.addWidget(QLabel("Outputs (클릭 시 500ms high 출력):"))
        btn_pc_on = QPushButton("PC_ON_EN (500ms)")
        btn_pc_on.clicked.connect(lambda: self.request_pc_on_pulse.emit())
        btn_pc_reset = QPushButton("PC_RESET_EN (500ms)")
        btn_pc_reset.clicked.connect(lambda: self.request_pc_reset_pulse.emit())
        lay_pc.addWidget(btn_pc_on)
        lay_pc.addWidget(btn_pc_reset)
        self._btn_pc_on = btn_pc_on
        self._btn_pc_reset = btn_pc_reset
        lay_pc.addWidget(QLabel("Input:"))
        row_pc_led = QHBoxLayout()
        self._pc_led_led = DiLedIndicator()  # high=빨강, low=파랑
        row_pc_led.addWidget(self._pc_led_led)
        row_pc_led.addWidget(QLabel("PC_LED_IN"))
        row_pc_led.addStretch()
        lay_pc.addLayout(row_pc_led)
        btn_read_pc_led = QPushButton("Read PC LED")
        btn_read_pc_led.clicked.connect(lambda: self.request_read_pc_led.emit())
        lay_pc.addWidget(btn_read_pc_led)
        self._btn_read_pc_led = btn_read_pc_led
        main_layout.addWidget(card_pc)

        # ---- E) Env Sensor (SHTC3) ----
        card_env, lay_env = card_frame("Env Sensor (SHTC3)")
        row_env = QHBoxLayout()
        self._lbl_temp = QLabel("Temp: -.- °C")
        self._lbl_rh = QLabel("RH: -.- %")
        self._lbl_env_status = QLabel("Status: (not read)")
        self._lbl_env_flags = QLabel("Flags: ----")
        row_env.addWidget(self._lbl_temp)
        row_env.addSpacing(16)
        row_env.addWidget(self._lbl_rh)
        row_env.addSpacing(16)
        row_env.addWidget(self._lbl_env_status)
        row_env.addSpacing(16)
        row_env.addWidget(self._lbl_env_flags)
        row_env.addStretch()
        lay_env.addLayout(row_env)
        btn_read_env = QPushButton("Read Sensor (5s auto)")
        btn_read_env.clicked.connect(lambda: self.request_read_env.emit())
        lay_env.addWidget(btn_read_env)
        self._btn_read_env = btn_read_env
        main_layout.addWidget(card_env)

        # ---- F) Log ----
        card_log, lay_log = card_frame("Log")
        self._log_edit = QPlainTextEdit()
        self._log_edit.setReadOnly(True)
        self._log_edit.setMinimumHeight(140)
        self._log_edit.setFont(QFont("Consolas", 10))
        lay_log.addWidget(self._log_edit)
        h = QHBoxLayout()
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._log_clear)
        h.addWidget(btn_clear)
        h.addStretch()
        lay_log.addLayout(h)
        main_layout.addWidget(card_log)

        self.setCentralWidget(central)
        self.resize(560, 640)
        self.setMinimumSize(480, 500)

        self._log.log_line.connect(self._on_log_line)

    def _on_log_line(self, line: str):
        self._log_lines.append(line)
        try:
            self._log_edit.appendPlainText(line)
        except Exception:
            pass

    def _log_clear(self):
        try:
            self._log.clear()
            self._log_lines.clear()
            self._log_edit.clear()
        except Exception:
            pass

    def _set_connected_ui(self, connected: bool):
        self._btn_connect.setEnabled(not connected)
        self._btn_disconnect.setEnabled(connected)
        self._port_combo.setEnabled(not connected)
        self._slave_id.setEnabled(not connected)
        self._btn_read_di.setEnabled(connected)
        self._btn_read_pc_led.setEnabled(connected)
        self._btn_read_env.setEnabled(connected)
        for chk in self._relay_checks:
            chk.setEnabled(connected)
        self._btn_pc_on.setEnabled(connected)
        self._btn_pc_reset.setEnabled(connected)
        if connected:
            self._status_badge.setText("Connected")
            self._status_badge.setStyleSheet("color: #00c853; font-weight: bold; padding: 4px 8px;")
            self._seen_0xaa_since_connect = False
            self._modbus_fail_0xaa_hint_shown = False
            self._raw_poll_timer.start()
            self._env_poll_timer.start()
            self._log.log_info("→ Read DI 버튼을 눌러 보드 응답을 확인하세요. (보드는 요청 받을 때만 응답 전송)")
        else:
            self._raw_poll_timer.stop()
            self._env_poll_timer.stop()
            self._status_badge.setText("Disconnected")
            self._status_badge.setStyleSheet("color: #555555; font-weight: bold; padding: 4px 8px;")

    def _refresh_ports(self):
        try:
            self._port_combo.clear()
            for p in list_serial_ports():
                self._port_combo.addItem(p)
            if self._port_combo.count() == 0:
                self._port_combo.addItem("(no ports)")
        except Exception as e:
            self._log.log_info(f"Refresh ports: {type(e).__name__}: {e}")

    def _do_connect(self):
        port = self._port_combo.currentText() if self._port_combo.currentIndex() >= 0 else ""
        if not port or port == "(no ports)":
            QMessageBox.warning(self, "Connection", "Select a valid port.")
            return
        try:
            ok, msg = self._client.connect(
                port, baudrate=self._baud.value(), slave_id=self._slave_id.value()
            )
            if ok:
                self._set_connected_ui(True)
                self._log.log("Connect", port, self._baud.value(), "OK")
                if self._slave_id.value() != MAINBOARD_SLAVE_ID_DEFAULT:
                    self._log.log_info(f"경고: 메인보드 Slave ID는 {MAINBOARD_SLAVE_ID_DEFAULT}입니다. 현재 {self._slave_id.value()}이면 Read DI/Relay 응답이 없을 수 있습니다.")
            else:
                QMessageBox.warning(self, "Connection", msg or "No response/timeout")
                self._log.log("Connect", port, self._baud.value(), "Fail", msg or "No response/timeout")
        except Exception as e:
            err = f"{type(e).__name__}: {e}"
            QMessageBox.warning(self, "Connection", err)
            self._log.log("Connect", port, self._baud.value(), "Fail", err)

    def _do_disconnect(self):
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._set_connected_ui(False)
        self._log.log("Disconnect", "", "", "OK")

    def _poll_raw_rx(self):
        """주기적으로 시리얼 버퍼에서 raw 수신 확인 (보드 0xAA 등)."""
        if self._client.connected:
            self.request_read_raw.emit()

    def _show_0xaa_mode_hint_if_needed(self, msg: str):
        """Modbus 실패 시 로그에 'No response'/'Unable to decode' 있고 0xAA를 받은 적 있으면 한 번만 팝업 안내."""
        if self._modbus_fail_0xaa_hint_shown or not msg:
            return
        if "No response" not in msg and "Unable to decode" not in msg:
            return
        if not self._seen_0xaa_since_connect:
            return
        self._modbus_fail_0xaa_hint_shown = True
        QMessageBox.warning(
            self,
            "Modbus 응답 없음",
            "지금 보드가 0xAA 전용 모드로 동작 중입니다.\n\n"
            "Read DI / Relay / PC Status를 쓰려면 메인보드를 Modbus 모드로 바꿔야 합니다.\n\n"
            "→ Guro_Mainboard 프로젝트에서 app_config.h 를 열고\n"
            "   ENABLE_PC_TEST_AA_STREAM 을 0 으로 설정한 뒤\n"
            "   빌드 → Run(다운로드) 하세요.\n\n"
            "그 다음 PC 툴에서 Disconnect 후 다시 Connect 하면 됩니다."
        )

    def _on_raw_bytes(self, bytes_list: list):
        """보드에서 보낸 raw 바이트를 로그에 표시."""
        if bytes_list and 0xAA in bytes_list:
            self._seen_0xaa_since_connect = True
        self._log.log_raw_rx(bytes_list)

    def _on_relay_toggle(self, ch: int, onoff: bool):
        self.request_write_relay.emit(ch, onoff)

    def _on_di_result(self, ok: bool, bits: list | None, err: str | None):
        if ok and bits is not None:
            for i in range(min(8, len(bits))):
                self._di_leds[i].set_di_state(bool(bits[i]))  # 1=입력 있음(빨강), 0=없음(파랑)
            for i in range(len(bits), 8):
                self._di_leds[i].set_di_state(False)
            self._log.log("FC03", MAIN_DI_REG, 2, "OK")
        else:
            for led in self._di_leds:
                led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log("FC03", MAIN_DI_REG, 2, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요.")

    def _on_pc_led_result(self, ok: bool, state: bool | None, err: str | None):
        if ok and state is not None:
            # high=빨강, low=파랑
            self._pc_led_led.set_di_state(True if state else False)
            self._log.log("FC03", PC_LED_IN_REG, 1, "OK")
        else:
            self._pc_led_led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log("FC03", PC_LED_IN_REG, 1, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요.")

    def _on_env_result(self, ok: bool, val: tuple | None, err: str | None):
        if ok and val is not None:
            t, rh, flags = val
            invalid = (abs(t + 3276.8) < 0.05) or (abs(rh - 6553.5) < 0.05)
            try:
                if invalid:
                    self._lbl_temp.setText("Temp: --.- °C")
                    self._lbl_rh.setText("RH: --.- %")
                    self._lbl_env_status.setText("Status: SENSOR ERROR")
                else:
                    self._lbl_temp.setText(f"Temp: {t:.1f} °C")
                    self._lbl_rh.setText(f"RH: {rh:.1f} %")
                    self._lbl_env_status.setText("Status: OK")
                self._lbl_env_flags.setText(f"Flags: 0x{int(flags) & 0xFFFF:04X}")
            except Exception:
                pass
            self._log.log("FC03", 2110, 3, "OK")
        else:
            msg = err or "No response/timeout"
            try:
                self._lbl_env_status.setText("Status: (read fail)")
            except Exception:
                pass
            self._log.log("FC03", 2110, 3, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)

    def _on_write_result(self, ok: bool, err: str | None):
        if ok:
            self._log.log("Write", "FC06", "OK", "OK")  # Relay/PC_CTRL 쓰기 성공
        else:
            msg = err or "No response/timeout"
            self._log.log("Write", "", "", "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요. (0xAA 전용 모드면 Modbus 응답 없음)")

    def closeEvent(self, event):
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._thread.quit()
        self._thread.wait(1000)
        event.accept()
