"""
Main window: Mainboard 최소 테스트 툴 — Connection, Mainboard I/O (4 out, 8 in), PC Status (2 out, 1 in), Log.
All Modbus RTU to single Mainboard slave. Exceptions handled safely; no crash on timeout.
"""
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QComboBox, QPushButton, QLabel, QSpinBox, QFrame, QMessageBox,
    QPlainTextEdit, QGroupBox, QCheckBox, QApplication, QSizePolicy,
)
from PyQt6.QtCore import pyqtSignal, Qt, QTimer
from PyQt6.QtGui import QFont, QShowEvent, QPixmap
from pathlib import Path

import pymodbus
from pymodbus.client.sync import ModbusSerialClient

from .modbus_client import ModbusClient
from .worker import MainboardWorker, create_worker_and_thread
from .logger import LogHandler
from .address_map import (
    MAINBOARD_SLAVE_ID_DEFAULT,
    MAIN_DI_REG,
    MAIN_DO_REG,
    PC_ON_EN_REG,
    PC_RESET_EN_REG,
    PC_LED_IN_REG,
    SUB_SENSE_REG,
    SUB_COIL_STATUS_START,
    SUB_VB_COIL_BASE,
    SUB_VB_COIL_COUNT,
    SUB_HPSB_COIL_BASE,
    SUB_LPSB_COIL_BASE,
    MAIN_ENV_REG,
)


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


class ColorStrip(QFrame):
    """왼쪽 색상 표시: ON=빨강, OFF=파랑. set_state(True)=빨강, False=파랑."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(8, 36)
        self.set_state(False)

    def set_state(self, on: bool):
        if on:
            self.setStyleSheet("background-color: #e53935; border-radius: 2px;")
        else:
            self.setStyleSheet("background-color: #2196F3; border-radius: 2px;")


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
    request_pc_on_pulse = pyqtSignal()
    request_pc_reset_pulse = pyqtSignal()
    request_read_env = pyqtSignal()
    request_read_raw = pyqtSignal()
    request_read_sub = pyqtSignal()           # HPSB/LPSB sense + coil + error_flags
    request_sub_pulse = pyqtSignal(int)       # LPSB VB pulse index 0..4
    request_write_sub_coil = pyqtSignal(int, bool)  # addr 898..909, value (FC05 to Mainboard)
    request_write_direct_hpsb_coil = pyqtSignal(int, bool)  # coil_index 0..2, value (HPSB 다이렉트, Slave 1)
    request_write_direct_lpsb_coil = pyqtSignal(int, bool)  # coil_index 0..2, value (LPSB 다이렉트, Slave 2)
    request_diagnostic_sequence = pyqtSignal()  # run HPSB/LPSB LED diagnostic sequence
    request_sniff = pyqtSignal()  # 연결 직후 2초 수신 테스트 (Direct HPSB)
    request_read_direct_lpsb_adc = pyqtSignal()  # Direct LPSB: FC03 start=0,count=9 → ADC raw 등

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
        self._last_log_tag = "[MAIN]"
        self._client.set_request_logger(self._on_request_log)
        self._client.set_response_logger(self._on_response_log)
        self._client.set_tx_frame_hex_logger(self._on_tx_frame_hex)
        self._thread, self._worker = create_worker_and_thread(self._client)
        self._thread.start()
        self._connect_worker_signals()
        self._log_lines: list[str] = []
        self._raw_poll_timer = QTimer(self)
        self._raw_poll_timer.setInterval(50)  # 50ms마다 raw RX 확인 (HPSB_TEST 1초 주기 수신 놓치지 않도록)
        self._raw_poll_timer.timeout.connect(self._poll_raw_rx)
        self._env_poll_timer = QTimer(self)
        self._env_poll_timer.setInterval(5000)
        self._env_poll_timer.timeout.connect(lambda: self.request_read_env.emit())
        self._sub_poll_timer = QTimer(self)
        self._sub_poll_timer.setInterval(2000)  # HPSB/LPSB 2초 주기 (Direct LPSB면 FC03 ADC read)
        self._sub_poll_timer.timeout.connect(self._on_sub_poll_tick)
        self._sub_auto_poll = False
        # Direct LPSB: SSR ON 동안 ADC 자동 폴링 (500ms)
        self._lpsb_adc_poll_timer = QTimer(self)
        self._lpsb_adc_poll_timer.setInterval(500)
        self._lpsb_adc_poll_timer.timeout.connect(self._on_lpsb_adc_poll_tick)
        self._lpsb_adc_poll_inflight = False
        self._lpsb_adc_poll_running = False
        self._seen_0xaa_since_connect = False
        self._modbus_fail_0xaa_hint_shown = False
        self._direct_hpsb_rx_total = 0  # 직접 HPSB 연결 후 수신 누적 바이트 (진단용)
        self._direct_diag_timer = QTimer(self)
        self._direct_diag_timer.setInterval(3000)  # 3초마다 수신 0이면 안내
        self._direct_diag_timer.timeout.connect(self._on_direct_diag_tick)
        # direct mode: "none" / "hpsb" / "lpsb"
        self._direct_mode: str = "none"
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
        self.request_read_sub.connect(self._worker.on_request_read_sub, Qt.ConnectionType.QueuedConnection)
        self.request_sub_pulse.connect(self._worker.on_request_sub_pulse, Qt.ConnectionType.QueuedConnection)
        self.request_write_sub_coil.connect(self._worker.on_request_write_sub_coil, Qt.ConnectionType.QueuedConnection)
        self.request_write_direct_hpsb_coil.connect(self._worker.on_request_write_direct_hpsb_coil, Qt.ConnectionType.QueuedConnection)
        self.request_write_direct_lpsb_coil.connect(self._worker.on_request_write_direct_lpsb_coil, Qt.ConnectionType.QueuedConnection)
        self.request_diagnostic_sequence.connect(self._worker.on_request_diagnostic_sequence, Qt.ConnectionType.QueuedConnection)
        self.request_sniff.connect(self._worker.on_request_sniff, Qt.ConnectionType.QueuedConnection)
        self._worker.di_result.connect(self._on_di_result)
        self._worker.sniff_result.connect(self._on_sniff_result)
        self._worker.sub_data_result.connect(self._on_sub_data_result)
        self._worker.pc_led_result.connect(self._on_pc_led_result)
        self._worker.env_result.connect(self._on_env_result)
        self._worker.write_result.connect(self._on_write_result)
        self._worker.raw_bytes_received.connect(self._on_raw_bytes)
        self.request_read_direct_lpsb_adc.connect(self._worker.on_request_read_direct_lpsb_adc, Qt.ConnectionType.QueuedConnection)
        self._worker.lpsb_adc_result.connect(self._on_lpsb_adc_result)

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
        lbl_slave = QLabel("Slave ID:")
        self._lbl_slave_id = lbl_slave
        top_lay.addWidget(lbl_slave)
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

        # ---- Content: Left(Mainboard + HPSB/LPSB) + Right(Log sidebar) ----
        content_row = QHBoxLayout()
        content_row.setSpacing(12)

        # ----- Left: Mainboard 테스트 (기존 구조 유지) -----
        left_w = QWidget()
        left_layout = QVBoxLayout(left_w)
        # 맥북 13인치(실질 세로 ~900)에서 첫 번째 컬럼 카드들이 균형 있게 보이도록
        # 세로 간격을 조금 줄여서 상/하 여백을 최적화한다.
        left_layout.setSpacing(8)
        left_layout.setContentsMargins(0, 0, 0, 0)

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
        left_layout.addWidget(card_out)

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
        left_layout.addWidget(card_in)

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
        self._pc_led_led = DiLedIndicator()
        row_pc_led.addWidget(self._pc_led_led)
        row_pc_led.addWidget(QLabel("PC_LED_IN"))
        row_pc_led.addStretch()
        lay_pc.addLayout(row_pc_led)
        btn_read_pc_led = QPushButton("Read PC LED")
        btn_read_pc_led.clicked.connect(lambda: self.request_read_pc_led.emit())
        lay_pc.addWidget(btn_read_pc_led)
        self._btn_read_pc_led = btn_read_pc_led
        left_layout.addWidget(card_pc)

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
        btn_read_env = QPushButton("Read Sensor")
        btn_read_env.setToolTip("수동 1회 읽기. 5초 자동 읽기는 비활성화됨.")
        btn_read_env.clicked.connect(lambda: self.request_read_env.emit())
        lay_env.addWidget(btn_read_env)
        self._btn_read_env = btn_read_env
        # 첫 번째 컬럼 카드들은 내용 높이에 맞게 배치하고,
        # 아래쪽 여유 공간은 컬럼 전체가 아니라 하단 여백으로 남기기 위해 Env 카드는 Expanding 대신 Preferred 사용.
        card_env.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Preferred)
        left_layout.addWidget(card_env)
        left_layout.addStretch()
        # 전체 가로 3등분: left_w (Mainboard), right_w (HPSB/LPSB), log_w (Log)
        # col1: 초기에는 col2/col3와 비슷한 폭, 이후 가로 리사이즈 시 폭 고정
        left_w.setMinimumWidth(460)
        left_w.setMaximumWidth(460)
        left_w.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)
        content_row.addWidget(left_w, 0)

        # ----- Middle: 제어 / HPSB / LPSB -----
        right_w = QWidget()
        # col2: 초기에는 col1/col3와 비슷한 폭, 이후 가로 리사이즈 시 폭 고정
        right_w.setMinimumWidth(460)
        right_w.setMaximumWidth(460)
        right_layout = QVBoxLayout(right_w)
        right_layout.setSpacing(12)
        right_layout.setContentsMargins(0, 0, 0, 0)

        # ---- Direct mode block: HPSB / LPSB 선택 ----
        gb_direct = QGroupBox("Direct Mode")
        lay_direct = QVBoxLayout(gb_direct)
        row_direct = QHBoxLayout()
        # 체크박스 2개를 세로로 배치 (이미지처럼 단순하게)
        col_direct = QVBoxLayout()
        self._chk_direct_hpsb = QCheckBox("Direct HPSB (Slave 1)")
        self._chk_direct_hpsb.stateChanged.connect(self._on_direct_hpsb_changed)
        self._chk_direct_lpsb = QCheckBox("Direct LPSB (Slave 2)")
        self._chk_direct_lpsb.stateChanged.connect(self._on_direct_lpsb_changed)
        col_direct.addWidget(self._chk_direct_hpsb)
        col_direct.addWidget(self._chk_direct_lpsb)
        row_direct.addLayout(col_direct)
        row_direct.addStretch()
        lay_direct.addLayout(row_direct)
        self._mode_label = QLabel("Mode: Mainboard routing")
        self._mode_label.setStyleSheet("color: #cccccc; font-size: 11px;")
        lay_direct.addWidget(self._mode_label)
        right_layout.addWidget(gb_direct)

        gb_ctrl = QGroupBox("제어")
        lay_ctrl = QVBoxLayout(gb_ctrl)
        btn_read_once = QPushButton("Read once (HPSB/LPSB)")
        btn_read_once.clicked.connect(self._on_read_once_clicked)
        lay_ctrl.addWidget(btn_read_once)
        self._btn_read_sub = btn_read_once
        btn_diag_seq = QPushButton("LED diagnostic sequence")
        btn_diag_seq.setToolTip("HPSB RELAY1/2/3 then LPSB1 SSR1/2/3 ON→OFF. Watch LED2/3/4 on boards.")
        btn_diag_seq.clicked.connect(lambda: self.request_diagnostic_sequence.emit())
        lay_ctrl.addWidget(btn_diag_seq)
        self._btn_diag_seq = btn_diag_seq
        self._chk_auto_poll = QCheckBox("Auto poll (2s)")
        self._chk_auto_poll.stateChanged.connect(self._on_auto_poll_changed)
        lay_ctrl.addWidget(self._chk_auto_poll)
        right_layout.addWidget(gb_ctrl)

        gb_hpsb = QGroupBox("HPSB (Slave 1)")
        lay_hpsb = QVBoxLayout(gb_hpsb)
        self._hpsb_strips = []
        self._hpsb_btns = []
        self._hpsb_current_labels = []
        hpsb_row = QHBoxLayout()
        for i, name in enumerate(["RELAY1 EN", "RELAY2 EN", "RELAY3 EN"]):
            col = QVBoxLayout()
            strip = ColorStrip()
            self._hpsb_strips.append(strip)
            btn = QPushButton(name)
            btn.setCheckable(True)
            btn.clicked.connect(lambda checked, idx=i: self._on_hpsb_relay_click(idx))
            self._hpsb_btns.append(btn)
            row1 = QHBoxLayout()
            row1.addWidget(strip)
            row1.addWidget(btn)
            col.addLayout(row1)
            cur_lbl = QLabel("current")
            cur_lbl.setStyleSheet("color: #888; font-size: 11px;")
            self._hpsb_current_labels.append(cur_lbl)
            col.addWidget(cur_lbl)
            hpsb_row.addLayout(col)
        lay_hpsb.addLayout(hpsb_row)
        lay_hpsb.addSpacing(6)
        self._hpsb_comm_label = QLabel("Comm: -")
        lay_hpsb.addWidget(self._hpsb_comm_label)
        right_layout.addWidget(gb_hpsb)

        gb_lpsb = QGroupBox("LPSB")
        lay_lpsb = QVBoxLayout(gb_lpsb)
        self._lpsb_select_btns = []
        lpsb_sel_row = QHBoxLayout()
        lpsb_names = ["LPSB 2", "LPSB 3", "LPSB 4"]
        self._lpsb_slave_ids = [2, 4, 8]  # UI 이름과 매핑되는 실제 LPSB slave ID
        for i, name in enumerate(lpsb_names):
            b = QPushButton(name)
            b.setCheckable(True)
            b.clicked.connect(lambda checked, idx=i: self._on_lpsb_select(idx))
            self._lpsb_select_btns.append(b)
            lpsb_sel_row.addWidget(b)
        self._lpsb_select_btns[0].setChecked(True)
        self._lpsb_select_btns[0].setStyleSheet("background-color: #1976D2; color: white;")
        self._selected_lpsb_index = 0
        lay_lpsb.addLayout(lpsb_sel_row)
        self._lpsb_strips = []
        self._lpsb_ssr_btns = []
        self._lpsb_current_labels = []
        lpsb_ssr_row = QHBoxLayout()
        for i, name in enumerate(["SSR1 EN", "SSR2 EN", "SSR3 EN"]):
            col = QVBoxLayout()
            strip = ColorStrip()
            self._lpsb_strips.append(strip)
            btn = QPushButton(name)
            btn.setCheckable(True)
            btn.clicked.connect(lambda checked, idx=i: self._on_lpsb_ssr_click(idx))
            self._lpsb_ssr_btns.append(btn)
            row1 = QHBoxLayout()
            row1.addWidget(strip)
            row1.addWidget(btn)
            col.addLayout(row1)
            cur_lbl = QLabel("current")
            cur_lbl.setStyleSheet("color: #888; font-size: 11px;")
            self._lpsb_current_labels.append(cur_lbl)
            col.addWidget(cur_lbl)
            lpsb_ssr_row.addLayout(col)
        lay_lpsb.addLayout(lpsb_ssr_row)
        self._lpsb_comm_label = QLabel("Comm: -")
        lay_lpsb.addWidget(self._lpsb_comm_label)
        # Guro mulsan 로고는 LPSB 박스 밖, 두 번째 컬럼 하단에 배치 (아래에서 right_layout 에 추가)
        # LPSB SSR 현재 상태 (Direct LPSB 모드에서 토글 기준)
        self._lpsb_ssr_state = [False, False, False]
        # ADC3 전류 유무 히스테리시스: 현재 표시 상태 / 마지막 안정 상태
        self._lpsb_current_state = "OFF"       # "OFF" | "ON" | "UNSTABLE"
        self._lpsb_last_stable_state = "OFF"  # "OFF" | "ON"
        # ADC1/ADC2 전류 유무 히스테리시스 상태 (ADC3와 동일 기준 확장)
        self._lpsb_current_state_ch1 = "OFF"
        self._lpsb_current_state_ch2 = "OFF"
        # HPSB와 동일 크기 느낌을 위해 LPSB는 확장 대신 기본 Preferred 사용
        gb_lpsb.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Preferred)
        right_layout.addWidget(gb_lpsb)
        # Guro mulsan 로고 (LPSB 박스 라인 밖, 바로 아래 10px 고정)
        logo_label = QLabel()
        for base in [Path(__file__).resolve().parent, Path(__file__).resolve().parent.parent]:
            logo_path = base / "guro_logo.png"
            if not logo_path.exists():
                logo_path = base / "resources" / "guro_logo.png"
            if logo_path.exists():
                pixmap = QPixmap(str(logo_path))
                if not pixmap.isNull():
                    logo_label.setPixmap(
                        pixmap.scaled(
                            135,
                            80,
                            Qt.AspectRatioMode.KeepAspectRatio,
                            Qt.TransformationMode.SmoothTransformation,
                        )
                    )
                break
        logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo_label.setMaximumWidth(150)
        logo_label.setMaximumHeight(80)
        logo_label.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        right_layout.addSpacing(0)
        # 로고는 "LPSB 바로 아래" 위치 고정. 창 확대 시 남는 공간은 로고 아래로만 늘어남.
        row_logo = QHBoxLayout()
        row_logo.addStretch()
        row_logo.addWidget(logo_label)
        right_layout.addLayout(row_logo)
        right_layout.addStretch()

        # Middle 컬럼 추가 (가로 폭 고정, 세로 확장)
        right_w.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Expanding)
        content_row.addWidget(right_w, 0)

        # ---- 오른쪽: Log 전용 세로 패널 ----
        log_w = QWidget()
        log_lay = QVBoxLayout(log_w)
        log_lay.setContentsMargins(0, 0, 0, 0)
        # Log 패널은 GroupBox 스타일로 (HPSB/LPSB와 동일 테두리)
        card_log = QGroupBox("Log")
        lay_log = QVBoxLayout(card_log)
        self._log_edit = QPlainTextEdit()
        self._log_edit.setReadOnly(True)
        self._log_edit.setMinimumHeight(200)
        self._log_edit.setFont(QFont("Consolas", 10))
        lay_log.addWidget(self._log_edit)
        h2 = QHBoxLayout()
        btn_clear2 = QPushButton("Clear")
        btn_clear2.clicked.connect(self._log_clear)
        h2.addWidget(btn_clear2)
        h2.addStretch()
        lay_log.addLayout(h2)
        # Log 컬럼은 가로/세로 모두 확장
        card_log.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        log_lay.addWidget(card_log)
        log_w.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        content_row.addWidget(log_w, 1)

        main_layout.addLayout(content_row)

        self.setCentralWidget(central)
        # 실행 시 열리는 크기: 1440×900 (MacBook Pro 13\" 해상도 기준).
        self._default_width = 1440
        self._default_height = 900
        self.resize(self._default_width, self._default_height)
        # 세로는 자유롭게 리사이즈 가능, 가로 최소 폭만 제한
        self.setMinimumSize(1024, 640)
        self._first_show = True

        self._log.log_line.connect(self._on_log_line)

    def showEvent(self, event: QShowEvent):
        """첫 표시 시 880×640으로 열리도록 함 (저장된 창 크기 무시)."""
        super().showEvent(event)
        if self._first_show:
            self._first_show = False
            self.resize(self._default_width, self._default_height)

    def _update_mode_label(self):
        if self._direct_mode == "hpsb":
            text = "Mode: Direct HPSB (Slave 1)"
        elif self._direct_mode == "lpsb":
            text = "Mode: Direct LPSB (Slave 2)"
        else:
            text = "Mode: Mainboard routing"
        self._mode_label.setText(text)

    def _current_lpsb_slave_id(self) -> int | None:
        try:
            return self._lpsb_slave_ids[self._selected_lpsb_index]
        except Exception:
            return None

    def _compute_lpsb_ssr_enable(self) -> bool:
        """
        LPSB SSR 버튼 활성/비활성 조건:
        - 연결됨
        - Direct Mode == lpsb
        - 선택된 보드의 실제 slave ID == 2 (현재 Direct LPSB 대상)
        """
        slave_id = self._current_lpsb_slave_id()
        return bool(
            self._client.connected
            and self._direct_mode == "lpsb"
            and slave_id == 2
        )

    def _update_lpsb_ssr_button_state(self):
        enable = self._compute_lpsb_ssr_enable()
        for btn in self._lpsb_ssr_btns:
            btn.setEnabled(enable)

    def _log_lpsb_selection_state(self, context: str):
        slave_id = self._current_lpsb_slave_id()
        idx = getattr(self, "_selected_lpsb_index", 0)
        name = self._lpsb_select_btns[idx].text() if 0 <= idx < len(self._lpsb_select_btns) else "N/A"
        enabled = self._compute_lpsb_ssr_enable()
        self._log.log_info(f"[LPSB] ({context}) select idx={idx} name={name} slave={slave_id}")
        self._log.log_info(f"[LPSB] ({context}) direct_mode={self._direct_mode} connected={self._client.connected}")
        self._log.log_info(f"[LPSB] ({context}) SSR buttons enabled={enabled}")

    def _on_direct_hpsb_changed(self, state: int):
        checked = state == Qt.CheckState.Checked.value
        if checked:
            # HPSB direct 선택 시 LPSB direct 해제
            if self._chk_direct_lpsb.isChecked():
                self._chk_direct_lpsb.blockSignals(True)
                self._chk_direct_lpsb.setChecked(False)
                self._chk_direct_lpsb.blockSignals(False)
            self._direct_mode = "hpsb"
            self._log.log_info("→ Mode: Direct HPSB (Slave 1)")
        else:
            # 둘 다 해제된 경우만 none 으로
            if not self._chk_direct_lpsb.isChecked():
                self._direct_mode = "none"
                self._log.log_info("→ Mode: Mainboard routing")
        self._update_mode_label()
        self._set_connected_ui(self._client.connected)
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("direct_hpsb_changed")
        self._update_lpsb_auto_adc_poll("direct_hpsb_changed")

    def _on_direct_lpsb_changed(self, state: int):
        checked = state == Qt.CheckState.Checked.value
        if checked:
            # LPSB direct 선택 시 HPSB direct 해제
            if self._chk_direct_hpsb.isChecked():
                self._chk_direct_hpsb.blockSignals(True)
                self._chk_direct_hpsb.setChecked(False)
                self._chk_direct_hpsb.blockSignals(False)
            self._direct_mode = "lpsb"
            self._log.log_info("→ Mode: Direct LPSB (Slave 2)")
        else:
            if not self._chk_direct_hpsb.isChecked():
                self._direct_mode = "none"
                self._log.log_info("→ Mode: Mainboard routing")
        self._update_mode_label()
        self._set_connected_ui(self._client.connected)
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("direct_lpsb_changed")
        self._update_lpsb_auto_adc_poll("direct_lpsb_changed")

    def _tag_for_request(self, func: str, addr: int | str, count_or_value: int | str) -> str:
        """보드 태그: [MAIN], [HPSB], [LPSB1], [LPSB2], [LPSB3]. PC는 메인보드와만 통신하며, 메인보드가 UART2로 HPSB/LPSB 폴링한 결과를 보여줌."""
        try:
            a = int(addr)
        except (TypeError, ValueError):
            a = -1
        if a == SUB_SENSE_REG or a == SUB_COIL_STATUS_START or (func == "FC02" and 868 <= a <= 879):
            return "[HPSB][LPSB1][LPSB2][LPSB3]"
        if func == "FC05" and 0 <= a <= 2:
            # Direct HPSB/LPSB: slave 1/2 coil 0..2
            if self._direct_mode == "hpsb":
                return "[DIRECT HPSB]"
            if self._direct_mode == "lpsb":
                return "[DIRECT LPSB]"
            return "[HPSB]"
        if func == "FC05" and SUB_HPSB_COIL_BASE <= a < SUB_HPSB_COIL_BASE + 3:
            return "[HPSB]"
        if func == "FC05" and SUB_LPSB_COIL_BASE <= a < SUB_LPSB_COIL_BASE + 9:
            if a < SUB_LPSB_COIL_BASE + 3:
                return "[LPSB1]"
            if a < SUB_LPSB_COIL_BASE + 6:
                return "[LPSB2]"
            return "[LPSB3]"
        if func == "FC05" and SUB_VB_COIL_BASE <= a < SUB_VB_COIL_BASE + SUB_VB_COIL_COUNT:
            idx = a - SUB_VB_COIL_BASE
            if idx == 0:
                return "[LPSB1]"
            if 1 <= idx <= 3:
                return "[LPSB2]"
            return "[LPSB3]"
        return "[MAIN]"

    def _on_request_log(self, unit: int, func: str, addr: int | str, count_or_value: int | str):
        self._last_log_tag = self._tag_for_request(func, addr, count_or_value)
        self._log.log_request(self._last_log_tag, unit, func, addr, count_or_value)

    def _on_response_log(self, ok: bool, exception_code: int | None):
        self._log.log_response(self._last_log_tag, ok, exception_code)

    def _on_tx_frame_hex(self, msg: str):
        """Direct HPSB FC05 시 실제 전송 프레임 hex 로그 (01 05 00 00 FF 00 CRC_L CRC_H 형식)."""
        self._log.log_info(msg)

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
        # Slave ID는 direct mode가 아닐 때만 보이도록 (direct=HPSB/LPSB 는 slave 1/2 고정)
        show_slave = self._direct_mode == "none"
        self._lbl_slave_id.setVisible(show_slave)
        self._slave_id.setVisible(show_slave)
        self._slave_id.setEnabled(not connected and show_slave)
        # direct 모드에 따라 일부 버튼 제어 (Mainboard 경유 HPSB/LPSB 읽기/제어만 제한)
        direct_mode_active = connected and (self._direct_mode in ("hpsb", "lpsb"))
        self._btn_read_di.setEnabled(connected)          # Mainboard DI는 항상 가능
        self._btn_read_pc_led.setEnabled(connected)      # PC LED도 항상 가능
        self._btn_read_env.setEnabled(connected)         # Env도 항상 가능
        # Read once 버튼은 Direct LPSB 모드에서도 사용:
        # - Mainboard routing: HPSB/LPSB sub read
        # - Direct LPSB: LPSB FC03 (ADC raw 포함) read
        self._btn_read_sub.setEnabled(connected)
        # Direct HPSB에서는 자동 폴링 없음; Direct LPSB·메인보드에서는 Auto poll 사용 가능
        self._chk_auto_poll.setEnabled(connected and (self._direct_mode != "hpsb"))
        for b in self._hpsb_btns:
            b.setEnabled(connected)
        for b in self._lpsb_select_btns:
            b.setEnabled(connected and not direct_mode_active)
        for b in self._lpsb_ssr_btns:
            b.setEnabled(connected and not direct_mode_active)
        for chk in self._relay_checks:
            chk.setEnabled(connected)
        self._btn_pc_on.setEnabled(connected and not direct_mode_active)
        self._btn_pc_reset.setEnabled(connected and not direct_mode_active)
        self._update_mode_label()
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("set_connected_ui")
        self._update_lpsb_auto_adc_poll("set_connected_ui")
        if connected:
            self._status_badge.setText("Connected")
            self._status_badge.setStyleSheet("color: #00c853; font-weight: bold; padding: 4px 8px;")
            self._seen_0xaa_since_connect = False
            self._modbus_fail_0xaa_hint_shown = False
            is_direct_hpsb = self._direct_mode == "hpsb"
            self._log.set_raw_line_only(is_direct_hpsb)  # Direct HPSB: \r\n만 보고 한 줄씩 출력
            if is_direct_hpsb:
                # Direct HPSB: no Modbus 자동 폴링. HPSB_TEST 등 보드→PC raw 수신 표시 시도
                self._direct_hpsb_rx_total = 0
                self._raw_poll_timer.start()
                self._direct_diag_timer.start()
                self._env_poll_timer.stop()
                self._sub_poll_timer.stop()
                self._sub_auto_poll = False
                self._chk_auto_poll.setChecked(False)
                self._log.log_info("→ [DIRECT] 모드: 자동 폴링 없음. RELAY1/2/3 클릭 시 FC05 1회만 전송.")
                port = self._port_combo.currentText() if self._port_combo.currentIndex() >= 0 else ""
                if self._client.can_read_raw():
                    self._log.log_info("→ [DIRECT] 보드→PC 수신: 사용 가능. HPSB_TEST 문자열이 1초마다 로그에 표시됩니다.")
                    self._log.log_info("→ 연결 후 약 3초(오프셋 수집) 뒤부터 '시각 | [RX] <MSG>MS=ms,HPSB_TEST,...' 한 줄씩 나옵니다.")
                    self._log.log_info(f"→ 연결 포트: {port}")
                else:
                    self._log.log_info("→ [DIRECT] 보드→PC 수신: 이 툴에서 raw 수신 미지원. 터미널에서 screen ... 9600 로 확인하세요.")
            else:
                self._raw_poll_timer.start()
                # self._env_poll_timer.start()
                self._log.log_info("→ Read DI 버튼을 눌러 보드 응답을 확인하세요. (보드는 요청 받을 때만 응답 전송)")
        else:
            self._log.set_raw_line_only(False)
            self._raw_poll_timer.stop()
            self._direct_diag_timer.stop()
            self._env_poll_timer.stop()
            self._sub_poll_timer.stop()
            self._sub_auto_poll = False
            self._chk_auto_poll.setChecked(False)
            self._status_badge.setText("Disconnected")
            self._status_badge.setStyleSheet("color: #555555; font-weight: bold; padding: 4px 8px;")
            self._stop_lpsb_auto_adc_poll("disconnect")

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
            # Direct HPSB여도 기존처럼 ModbusSerialClient로 포트 열기 (아까 수신 됐을 때와 동일). raw_only는 수신 안 될 때만 시도.
            raw_only = False
            ok, msg = self._client.connect(
                port,
                baudrate=self._baud.value(),
                slave_id=self._slave_id.value(),
                raw_only=raw_only,
            )
            if ok:
                self._set_connected_ui(True)
                self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), msg or "OK")
                if raw_only:
                    self._log.log_info("→ 직접 HPSB(raw_only): 시리얼만 열었습니다. 2초 수신 테스트 중…")
                    QTimer.singleShot(300, lambda: self.request_sniff.emit())
                if self._slave_id.value() != MAINBOARD_SLAVE_ID_DEFAULT:
                    self._log.log_info(f"경고: 메인보드 Slave ID는 {MAINBOARD_SLAVE_ID_DEFAULT}입니다. 현재 {self._slave_id.value()}이면 Read DI/Relay 응답이 없을 수 있습니다.")
            else:
                QMessageBox.warning(self, "Connection", msg or "No response/timeout")
                self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), "Fail", msg or "No response/timeout")
        except Exception as e:
            err = f"{type(e).__name__}: {e}"
            QMessageBox.warning(self, "Connection", err)
            self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), "Fail", err)

    def _do_disconnect(self):
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._set_connected_ui(False)
        self._log.log_tagged("[MAIN]", "Disconnect", "", "", "OK")

    def _poll_raw_rx(self):
        """주기적으로 시리얼 버퍼에서 raw 수신 확인 (보드 0xAA, HPSB TEST 등)."""
        if self._client.connected:
            self.request_read_raw.emit()

    def _on_direct_diag_tick(self):
        """직접 HPSB 연결 시 3초마다: 수신이 한 번도 없으면 원인 분리용 안내."""
        if not self._client.connected or self._direct_mode != "hpsb":
            return
        if self._direct_hpsb_rx_total > 0:
            return
        port = self._port_combo.currentText() if self._port_combo.currentIndex() >= 0 else ""
        self._log.log_info(
            f"[직접 HPSB] 수신 0바이트. 포트={port} | "
            "보드에 HPSB_TEST 펌웨어가 올라가 있고, 이 포트로 연결했는지 확인하세요. "
            "다른 터미널(screen/cu)에서 같은 포트 9600으로 수신 테스트 권장."
        )

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

    def _on_sniff_result(self, bytes_list: list):
        """연결 직후 2초 수신 테스트 결과. 0바이트면 포트/배선 점검 안내."""
        n = len(bytes_list) if bytes_list else 0
        if n > 0:
            self._direct_hpsb_rx_total += n
            try:
                text = bytes(bytes_list[:200]).decode("ascii", errors="replace")
                preview = repr(text[:80]) + ("…" if len(text) > 80 else "")
            except Exception:
                preview = " ".join(f"{b:02X}" for b in bytes_list[:40])
            self._log.log_info(f"→ 2초 수신 테스트: {n}바이트 수신. 미리보기: {preview}")
            self._log.log_raw_rx(bytes_list)
        else:
            port = self._port_combo.currentText() if self._port_combo.currentIndex() >= 0 else ""
            self._log.log_info(
                f"→ 2초 수신 테스트: 0바이트. 이 포트({port})로 HPSB 보드가 직접 연결되었는지 확인하세요. "
                "터미널에서: python scripts/serial_receive_test.py <포트>"
            )

    def _on_raw_bytes(self, bytes_list: list):
        """보드에서 보낸 raw 바이트를 로그에 표시."""
        if bytes_list and 0xAA in bytes_list:
            self._seen_0xaa_since_connect = True
        if bytes_list and self._direct_mode == "hpsb":
            self._direct_hpsb_rx_total += len(bytes_list)
        self._log.log_raw_rx(bytes_list)

    def _on_relay_toggle(self, ch: int, onoff: bool):
        self.request_write_relay.emit(ch, onoff)

    def _on_di_result(self, ok: bool, bits: list | None, err: str | None):
        if ok and bits is not None:
            for i in range(min(8, len(bits))):
                self._di_leds[i].set_di_state(bool(bits[i]))  # 1=입력 있음(빨강), 0=없음(파랑)
            for i in range(len(bits), 8):
                self._di_leds[i].set_di_state(False)
            self._log.log_tagged("[MAIN]", "FC03", MAIN_DI_REG, 2, "OK")
        else:
            for led in self._di_leds:
                led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log_tagged("[MAIN]", "FC03", MAIN_DI_REG, 2, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요.")

    def _on_pc_led_result(self, ok: bool, state: bool | None, err: str | None):
        if ok and state is not None:
            # high=빨강, low=파랑
            self._pc_led_led.set_di_state(True if state else False)
            self._log.log_tagged("[MAIN]", "FC03", PC_LED_IN_REG, 1, "OK")
        else:
            self._pc_led_led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log_tagged("[MAIN]", "FC03", PC_LED_IN_REG, 1, "Fail", msg)
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
            self._log.log_tagged("[MAIN]", "FC03", MAIN_ENV_REG, 3, "OK")
        else:
            msg = err or "No response/timeout"
            try:
                self._lbl_env_status.setText("Status: (read fail)")
            except Exception:
                pass
            self._log.log_tagged("[MAIN]", "FC03", MAIN_ENV_REG, 3, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)

    def _on_write_result(self, ok: bool, err: str | None):
        tag = self._last_log_tag
        if ok:
            self._log.log_tagged(tag, "Write", "FC05/06", "-", "Response: OK")
        else:
            msg = err or "No response/timeout"
            self._log.log_tagged(tag, "Write", "FC05/06", "-", "Response: Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                if self._chk_direct_hpsb.isChecked() or tag in ("[HPSB]", "[DIRECT]"):
                    self._log.log_info(
                        "힌트: Direct HPSB — PC가 HPSB와 연결된 직렬 포트인지 확인하세요. "
                        "HPSB 전원, RS485(DE/배선), 보드 펌웨어(Modbus 슬레이브)를 점검하세요."
                    )
                else:
                    self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요. (0xAA 전용 모드면 Modbus 응답 없음)")

    def _on_read_once_clicked(self):
        """Read once 버튼:
        - Mainboard routing: 기존 HPSB/LPSB sub read
        - Direct LPSB 모드: LPSB 보드 FC03(0,9)로 ADC raw 포함 상태를 1회 읽기
        """
        if not self._client.connected:
            self._log.log_info("[MAIN] Not connected")
            return
        if self._direct_mode == "lpsb":
            self._log.log_info("[LPSB] Direct FC03 (start=0,count=12) read 요청")
            self.request_read_direct_lpsb_adc.emit()
        elif self._direct_mode == "hpsb":
            # 아직 Direct HPSB read once 동작은 정의하지 않음. 힌트만 출력.
            self._log.log_info("[HPSB] Direct HPSB 모드에서는 현재 Read once 동작이 정의되어 있지 않습니다.")
        else:
            self.request_read_sub.emit()

    def _on_hpsb_relay_click(self, idx: int):
        """HPSB RELAY 버튼: Direct HPSB면 FC05 slave=1 coil=idx(0,1,2). 아니면 FC05 addr 898+idx → Mainboard 경유."""
        btn = self._hpsb_btns[idx]
        value = btn.isChecked()
        self._hpsb_strips[idx].set_state(value)
        if self._direct_mode == "hpsb":
            self._log.log_info(f"[DEBUG] Button: HPSB RELAY{idx + 1} EN (Direct HPSB) -> FC05 slave=1 coil={idx} val={1 if value else 0}")
            self.request_write_direct_hpsb_coil.emit(idx, value)
        else:
            addr = SUB_HPSB_COIL_BASE + idx
            self._log.log_info(f"[DEBUG] Button: HPSB RELAY{idx + 1} EN -> FC05 addr={addr} val={1 if value else 0}")
            self.request_write_sub_coil.emit(addr, value)

    def _on_lpsb_select(self, idx: int):
        """LPSB 2/3/4 선택. 하나만 선택되도록. 선택 시 해당 보드 데이터로 SSR/current 갱신."""
        self._selected_lpsb_index = idx
        for i, b in enumerate(self._lpsb_select_btns):
            b.setChecked(i == idx)
            b.setStyleSheet("background-color: #1976D2; color: white;" if i == idx else "")
        sense = getattr(self, "_last_sense", None)
        coils = getattr(self, "_last_coils", None)
        if sense and coils and len(sense) >= 12 and len(coils) >= 12:
            base_s = 3 + idx * 3
            base_c = 3 + idx * 3
            for i in range(3):
                on = base_c + i < len(coils) and coils[base_c + i]
                self._lpsb_strips[i].set_state(on)
                self._lpsb_ssr_btns[i].setChecked(on)
                v = sense[base_s + i] if base_s + i < len(sense) else 0
                self._lpsb_current_labels[i].setText(f"current: {v}")
        # LPSB 선택에 따라 SSR 버튼 활성/비활성도 갱신
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("lpsb_select")

    def _on_lpsb_ssr_click(self, ssr_idx: int):
        """LPSB SSR 버튼: Direct LPSB 모드에서만 선택된 보드 slave ID 기준으로 토글 제어.
        현재 Direct 제어는 slave=2 보드만 지원한다.
        """
        btn = self._lpsb_ssr_btns[ssr_idx]
        slave_id = self._current_lpsb_slave_id()
        # Direct LPSB 모드가 아니면 토글을 되돌리고 안내만 남김
        if self._direct_mode != "lpsb":
            self._log.log_info("[LPSB] Direct LPSB (Slave 2) 모드에서만 SSR 제어가 가능합니다.")
            btn.blockSignals(True)
            btn.setChecked(self._lpsb_ssr_state[ssr_idx])
            btn.blockSignals(False)
            self._lpsb_strips[ssr_idx].set_state(self._lpsb_ssr_state[ssr_idx])
            return
        # 선택된 LPSB 보드가 slave 2가 아니면 Direct 제어 미지원
        if slave_id != 2:
            self._log.log_info(f"[LPSB] 현재 선택 보드 slave={slave_id} (Direct 제어는 slave=2만 지원). SSR 버튼 비활성 대상.")
            btn.blockSignals(True)
            btn.setChecked(self._lpsb_ssr_state[ssr_idx])
            btn.blockSignals(False)
            self._lpsb_strips[ssr_idx].set_state(self._lpsb_ssr_state[ssr_idx])
            return
        # 연결 안 된 경우
        if not self._client.connected:
            self._log.log_info("[LPSB] Not connected")
            btn.blockSignals(True)
            btn.setChecked(self._lpsb_ssr_state[ssr_idx])
            btn.blockSignals(False)
            self._lpsb_strips[ssr_idx].set_state(self._lpsb_ssr_state[ssr_idx])
            return

        # 내부 상태 토글
        new_state = not self._lpsb_ssr_state[ssr_idx]
        self._lpsb_ssr_state[ssr_idx] = new_state
        btn.blockSignals(True)
        btn.setChecked(new_state)
        btn.blockSignals(False)
        self._lpsb_strips[ssr_idx].set_state(new_state)

        # Modbus FC05: Slave 2, coil = ssr_idx, value = ON/OFF
        onoff_str = "ON" if new_state else "OFF"
        self._log.log_info(f"[LPSB] Write Coil {ssr_idx} \u2192 {onoff_str}")
        self._log.log_info(f"[LPSB] SSR{ssr_idx + 1} {onoff_str}")
        self.request_write_direct_lpsb_coil.emit(ssr_idx, new_state)
        # 디버깅: SSR1/2/3 ON 직후 ADC raw를 바로 읽어 Log에 출력 (전류 흐름/채널 매핑 확인용)
        if new_state and ssr_idx in (0, 1, 2):
            self._log.log_info(f"[LPSB] SSR{ssr_idx + 1} ON -> read ADC AVG/PKPK (FC03 start=0,count=12)")
            QTimer.singleShot(200, lambda: self.request_read_direct_lpsb_adc.emit())
        # SSR ON/OFF 상태에 따라 자동 ADC 폴링 시작/중지
        self._update_lpsb_auto_adc_poll("lpsb_ssr_click")

    def _on_lpsb_adc_result(self, ok: bool, regs: list | None, err: str | None):
        """Direct LPSB 모드에서 FC03(0,12) 결과 처리: ADC AVG + PKPK를 Log에 출력하고 current 라벨에 표시."""
        # inflight 해제 (성공/실패 모두)
        self._lpsb_adc_poll_inflight = False
        if not ok or regs is None or len(regs) < 12:
            msg = err or "FC03 read fail"
            self._log.log_info(f"[LPSB] ADC raw read fail: {msg}")
            self._lpsb_comm_label.setText("Comm: (read fail)")
            for lbl in self._lpsb_current_labels:
                lbl.setText("current")
            self._update_lpsb_auto_adc_poll("lpsb_adc_result_fail")
            return
        try:
            adc1, adc2, adc3 = regs[3], regs[4], regs[5]
            pk1, pk2, pk3 = regs[9], regs[10], regs[11]
        except Exception:
            self._log.log_info("[LPSB] ADC raw read fail: invalid regs")
            self._lpsb_comm_label.setText("Comm: (read fail)")
            return
        self._log.log_info("[LPSB] ADC RAW READ")
        self._log.log_info(f"[LPSB] ADC_AVG  ADC1={adc1} ADC2={adc2} ADC3={adc3}")
        self._log.log_info(f"[LPSB] ADC_PKPK ADC1={pk1} ADC2={pk2} ADC3={pk3}")
        # ADC1 전류 유무 히스테리시스 (ADC3와 동일 기준)
        prev1 = self._lpsb_current_state_ch1
        if prev1 == "OFF" and pk1 >= 50:
            state1 = "ON"
        elif prev1 == "ON" and pk1 <= 37:
            state1 = "OFF"
        else:
            state1 = prev1 if prev1 in ("ON", "OFF") else "OFF"
        self._lpsb_current_state_ch1 = state1
        # ADC2 전류 유무 히스테리시스 (ADC3와 동일 기준)
        prev2 = self._lpsb_current_state_ch2
        if prev2 == "OFF" and pk2 >= 50:
            state2 = "ON"
        elif prev2 == "ON" and pk2 <= 37:
            state2 = "OFF"
        else:
            state2 = prev2 if prev2 in ("ON", "OFF") else "OFF"
        self._lpsb_current_state_ch2 = state2
        # ADC3 전류 유무 히스테리시스 (ON/OFF만): OFF→ON pk3>=50, ON→OFF pk3<=37, 그 외 현재 상태 유지
        prev = self._lpsb_current_state
        if prev == "OFF" and pk3 >= 50:
            state = "ON"
        elif prev == "ON" and pk3 <= 37:
            state = "OFF"
        else:
            state = prev if prev in ("ON", "OFF") else "OFF"
        self._lpsb_current_state = state
        self._log.log_info(f"[LPSB] CURRENT ADC1={state1} ADC2={state2} ADC3={state}")
        # 추가 메타: slave id / heartbeat / fw version (새 펌웨어 반영 여부 확인용)
        if len(regs) >= 9:
            try:
                sid = regs[6]
                hb = regs[7]
                fw = regs[8]
                self._log.log_info(f"[LPSB] META slave={sid} heartbeat={hb} fw=0x{fw:04X}")
            except Exception:
                pass
        # LPSB current 칸에 ADC raw 표시
        self._lpsb_current_labels[0].setText(f"ADC1: {adc1} (pkpk {pk1})")
        self._lpsb_current_labels[1].setText(f"ADC2: {adc2} (pkpk {pk2})")
        self._lpsb_current_labels[2].setText(f"ADC3: {adc3} (pkpk {pk3})")
        self._lpsb_comm_label.setText("Comm: OK")
        # reg0~2 = SSR1~3 상태 → 스트립/버튼/내부 상태 동기화
        for i in range(3):
            on = (regs[i] & 1) != 0 if i < len(regs) else False
            self._lpsb_ssr_state[i] = on
            self._lpsb_strips[i].set_state(on)
            self._lpsb_ssr_btns[i].blockSignals(True)
            self._lpsb_ssr_btns[i].setChecked(on)
            self._lpsb_ssr_btns[i].blockSignals(False)
        self._update_lpsb_auto_adc_poll("lpsb_adc_result_ok")

    def _any_lpsb_ssr_on(self) -> bool:
        try:
            return any(bool(x) for x in self._lpsb_ssr_state)
        except Exception:
            return False

    def _start_lpsb_auto_adc_poll(self, context: str):
        if self._lpsb_adc_poll_running:
            return
        self._lpsb_adc_poll_running = True
        self._lpsb_adc_poll_inflight = False
        self._lpsb_adc_poll_timer.start()
        self._log.log_info("[LPSB] Auto ADC poll started (500ms)")
        self._on_lpsb_adc_poll_tick()

    def _stop_lpsb_auto_adc_poll(self, context: str):
        if not self._lpsb_adc_poll_running:
            return
        self._lpsb_adc_poll_running = False
        self._lpsb_adc_poll_inflight = False
        self._lpsb_adc_poll_timer.stop()
        self._log.log_info("[LPSB] Auto ADC poll stopped")

    def _update_lpsb_auto_adc_poll(self, context: str):
        """Direct LPSB 모드에서 SSR이 하나라도 ON이면 ADC 자동 폴링 시작, 모두 OFF면 중지."""
        if (not self._client.connected) or (self._direct_mode != "lpsb"):
            self._stop_lpsb_auto_adc_poll(context)
            return
        if self._any_lpsb_ssr_on():
            self._start_lpsb_auto_adc_poll(context)
        else:
            self._stop_lpsb_auto_adc_poll(context)

    def _on_lpsb_adc_poll_tick(self):
        """500ms 주기 자동 ADC 폴링. 이전 요청 응답 전에는 중복 요청 금지."""
        if (not self._client.connected) or (self._direct_mode != "lpsb"):
            self._stop_lpsb_auto_adc_poll("adc_poll_tick_not_ready")
            return
        if not self._any_lpsb_ssr_on():
            self._stop_lpsb_auto_adc_poll("adc_poll_tick_all_off")
            return
        if self._lpsb_adc_poll_inflight:
            return
        self._lpsb_adc_poll_inflight = True
        self.request_read_direct_lpsb_adc.emit()

    def _on_sub_poll_tick(self):
        """2초 주기: Direct LPSB면 FC03(ADC) 읽기, 아니면 기존 HPSB/LPSB sub read."""
        if not self._client.connected:
            return
        if self._direct_mode == "lpsb":
            self.request_read_direct_lpsb_adc.emit()
        else:
            self.request_read_sub.emit()

    def _on_auto_poll_changed(self, state):
        self._sub_auto_poll = state == Qt.CheckState.Checked
        if self._sub_auto_poll and self._client.connected:
            self._sub_poll_timer.start()
            self._on_sub_poll_tick()
        else:
            self._sub_poll_timer.stop()

    def _on_sub_data_result(self, ok: bool, sense: list | None, coils: list | None, flags: int | None, err: str | None):
        AGG_ERR_COMM_HPSB = 1
        AGG_ERR_COMM_LPSB = 2
        if not ok:
            self._hpsb_comm_label.setText("Comm: (read fail)")
            self._lpsb_comm_label.setText("Comm: (read fail)")
            if err:
                self._log.log_tagged("[HPSB][LPSB1][LPSB2][LPSB3]", "FC03/FC02", SUB_SENSE_REG, "sub", "Fail", err)
            return
        sense = sense or [0] * 14
        coils = coils or [False] * 14
        flags = flags if flags is not None else 0
        # HPSB: RELAY1~3 상태 → 왼쪽 색상(빨강/파랑), 버튼 체크, current 표시
        for i in range(3):
            on = i < len(coils) and coils[i]
            self._hpsb_strips[i].set_state(on)
            self._hpsb_btns[i].setChecked(on)
            v = sense[i] if i < len(sense) else 0
            self._hpsb_current_labels[i].setText(f"current: {v}")
        self._hpsb_comm_label.setText("Comm: OK" if not (flags & AGG_ERR_COMM_HPSB) else "Comm: Timeout/CRC")
        # LPSB: 선택된 보드(LPSB2/3/4)에 대해 SSR1~3 상태·current 표시
        sel = getattr(self, "_selected_lpsb_index", 0)
        base_s = 3 + sel * 3
        base_c = 3 + sel * 3
        for i in range(3):
            on = base_c + i < len(coils) and coils[base_c + i]
            self._lpsb_strips[i].set_state(on)
            self._lpsb_ssr_btns[i].setChecked(on)
            v = sense[base_s + i] if base_s + i < len(sense) else 0
            self._lpsb_current_labels[i].setText(f"current: {v}")
        self._lpsb_comm_label.setText("Comm: OK" if not (flags & AGG_ERR_COMM_LPSB) else "Comm: Timeout/CRC")
        self._last_sense = sense
        self._last_coils = coils
        self._log.log_tagged("[HPSB][LPSB1][LPSB2][LPSB3]", "FC03/FC02", SUB_SENSE_REG, "sub", "OK")

    def closeEvent(self, event):
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._thread.quit()
        self._thread.wait(1000)
        event.accept()
