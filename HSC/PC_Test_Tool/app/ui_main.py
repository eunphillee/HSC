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
from PyQt6.QtGui import QFont, QPixmap, QShowEvent
from pathlib import Path
from collections import deque
import time
from datetime import datetime

import pymodbus
from pymodbus.client.sync import ModbusSerialClient

from .app_icon import load_app_icon
from .modbus_client import ModbusClient, build_fc05_rtu_frame
from .worker import MainboardWorker, create_worker_and_thread
from .logger import LogHandler
from .doc_modbus_panel import DocModbusPanel
from .address_map import (
    MAINBOARD_SLAVE_ID_DEFAULT,
    MAIN_DI_REG,
    MAIN_DO_REG,
    PC_ON_EN_REG,
    PC_RESET_EN_REG,
    PC_LED_IN_REG,
    SUB_SENSE_REG,
    SUB_SENSE_COUNT,
    SUB_SENSE_BOARD_STRIDE,
    SUB_COIL_STATUS_START,
    SUB_VB_COIL_BASE,
    SUB_VB_COIL_COUNT,
    SUB_HPSB_COIL_BASE,
    SUB_LPSB_COIL_BASE,
    MAIN_ENV_REG,
)

from PyQt6.QtWidgets import QTabWidget


def _now_ts() -> str:
    """현재 시각 문자열 (YYYY-MM-DD HH:MM:SS)."""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _lpsb_sense_base_from_selection(sel_idx: int) -> int:
    """Mainboard FC04 routing 레이아웃(변환 후): LPSB1=9, LPSB2=18, LPSB3=27 (각 9워드 블록)."""
    return 9 + sel_idx * SUB_SENSE_BOARD_STRIDE


def _format_sense_channel(sense: list, base: int, ch: int) -> str:
    if len(sense) < base + 9 or ch < 0 or ch > 2:
        return "AVG:— PKPK:— I:—"
    avg = sense[base + ch]
    pk = sense[base + 3 + ch]
    cur = sense[base + 6 + ch]
    ion = "ON" if cur else "OFF"
    return f"AVG:{avg} PKPK:{pk} I:{ion}"


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
QPlainTextEdit { background-color: #1e1e1e; color: #e0e0e0; font-family: Menlo, Monaco, "Courier New", monospace; font-size: 12px; border: 1px solid #3a3a3a; border-radius: 4px; }
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
    request_read_direct_lpsb_adc = pyqtSignal()  # Direct LPSB: FC04 start=0,count=14 → ADC/PKPK/CUR
    request_read_direct_hpsb_adc = pyqtSignal()  # Direct HPSB: FC04 start=0,count=16 → HPSB v1.1 map
    # 문서 기반 Modbus 테스트 (Tab2, worker thread 경유)
    request_doc_fc01 = pyqtSignal(int, int, int)       # unit, start, count
    request_doc_fc02 = pyqtSignal(int, int, int)       # unit, start, count
    request_doc_fc04 = pyqtSignal(int, int, int)       # unit, start, count
    request_doc_fc05 = pyqtSignal(int, int, bool)      # unit, addr, value
    request_doc_fc15 = pyqtSignal(int, int, object)    # unit, start, values(list[bool])

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Mainboard 최소 테스트 툴 — Modbus RTU")
        _icon = load_app_icon()
        if not _icon.isNull():
            self.setWindowIcon(_icon)
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
        self._pending_hpsb_write: tuple[int, bool] | None = None
        self._pending_hpsb_ui_write: dict | None = None
        self._pending_lpsb_ui_write: dict | None = None
        self._pending_lpsb_verify: dict | None = None
        self._last_req_route: str = "mainboard-routing"
        self._simple_hpsb_mode: bool = True
        self._hpsb_probe_inflight: bool = False
        self._hpsb_probe_only: bool = False
        self._log_buffer: deque[str] = deque()
        self._client.set_request_logger(self._on_request_log)
        self._client.set_response_logger(self._on_response_log)
        self._client.set_tx_frame_hex_logger(self._on_tx_frame_hex)
        self._client.set_raw_exception_logger(lambda msg: self._log.log_info(msg))
        self._thread, self._worker = create_worker_and_thread(self._client)
        self._thread.start()
        self._connect_worker_signals()
        self._log_lines: list[str] = []
        self._log_max_lines = 400
        self._log_flush_timer = QTimer(self)
        self._log_flush_timer.setInterval(80)
        self._log_flush_timer.timeout.connect(self._flush_log_buffer)
        self._log_flush_timer.start()
        self._raw_poll_timer = QTimer(self)
        self._raw_poll_timer.setInterval(50)  # 50ms마다 raw RX 확인 (HPSB_TEST 1초 주기 수신 놓치지 않도록)
        self._raw_poll_timer.timeout.connect(self._poll_raw_rx)
        self._env_poll_timer = QTimer(self)
        self._env_poll_timer.setInterval(5000)
        self._env_poll_timer.timeout.connect(lambda: self.request_read_env.emit())
        self._sub_poll_timer = QTimer(self)
        # UI 라벨과 동일하게 Auto poll 기본 주기 = 2초
        self._sub_poll_timer.setInterval(2000)
        self._sub_poll_timer.timeout.connect(self._on_sub_poll_tick)
        self._sub_auto_poll = False
        # HPSB/LPSB 전류 로그를 1초 주기로 출력(값은 최근 FC04 응답 기반).
        self._current_log_timer = QTimer(self)
        self._current_log_timer.setInterval(1000)
        self._current_log_timer.timeout.connect(self._on_current_log_tick)
        self._current_log_timer.stop()
        self._last_sense: list | None = None
        # HPSB ADC 상태 로그(1초): enable(릴레이 ON 또는 Probe 성공) 동안 출력. 값은 최신 수신값 기반.
        self._hpsb_adc_state = {"avg": [0, 0, 0], "pkpk": [0, 0, 0], "cur": [0, 0, 0]}
        self._hpsb_adc_last_update_ms: int = 0
        self._hpsb_adc_last_ok: bool = False
        self._hpsb_probe_ok: bool = False   # Read Probe 성공 후 True
        self._hpsb_adc_log_timer = QTimer(self)
        self._hpsb_adc_log_timer.setInterval(2000)    # 2s CONTROL MONITORING 주기
        self._hpsb_adc_log_timer.timeout.connect(self._on_hpsb_adc_log_tick)
        self._hpsb_adc_log_timer.stop()
        # Direct LPSB: 상태값 ADC 자동 폴링 (500ms) + 별도 1초 로그 타이머
        self._lpsb_adc_poll_timer = QTimer(self)
        self._lpsb_adc_poll_timer.setInterval(500)    # 500ms 폴링 (화면 갱신)
        self._lpsb_adc_poll_timer.timeout.connect(self._on_lpsb_adc_poll_tick)
        self._lpsb_adc_poll_inflight = False
        self._lpsb_adc_poll_running = False
        # LPSB 상태 저장 (별도 1초 로그 타이머용)
        self._lpsb_adc_state = {"avg": [0, 0, 0], "pkpk": [0, 0, 0], "cur": [0, 0, 0]}
        self._lpsb_adc_last_ok: bool = False
        self._lpsb_probe_ok: bool = False   # LPSB FC04 Read 성공 후 True
        # 동작 상태: "IDLE" | "READ_ONCE" | "OUTPUT_MONITORING"
        self._op_state: str = "IDLE"
        self._monitor_target: str = "none"  # "hpsb" | "lpsb" | "none"
        self._monitor_retry_count: int = 0   # OUTPUT_MONITORING 재시도 횟수 (최대 2회)
        # LPSB 2s 모니터링 로그 타이머 (OUTPUT_MONITORING 시에만 활성)
        self._lpsb_log_timer = QTimer(self)
        self._lpsb_log_timer.setInterval(2000)        # 2s OUTPUT MONITORING 주기
        self._lpsb_log_timer.timeout.connect(self._on_lpsb_log_tick)
        self._lpsb_log_timer.stop()
        # Direct HPSB: 상태값 로그용 FC04 자동 폴링 (500ms)
        self._hpsb_adc_poll_timer = QTimer(self)
        self._hpsb_adc_poll_timer.setInterval(500)    # 500ms 폴링 (화면 갱신)
        self._hpsb_adc_poll_timer.timeout.connect(self._on_hpsb_adc_poll_tick)
        self._hpsb_adc_poll_timer.stop()
        self._hpsb_adc_poll_inflight = False
        self._seen_0xaa_since_connect = False
        self._modbus_fail_0xaa_hint_shown = False
        self._direct_hpsb_rx_total = 0  # 직접 HPSB 연결 후 수신 누적 바이트 (진단용)
        self._pc_rx_byte_count = 0      # USART raw RX 누적(필터 무관)
        # UI 버벅임 완화: Direct LPSB FC04 자동 폴링 로그를 시간 기반으로 스로틀링
        self._last_direct_lpsb_fc04_req_log_ts = 0.0
        self._last_direct_lpsb_fc04_rsp_log_ts = 0.0
        self._last_lpsb_fc04_payload_log_ts = 0.0
        self._last_lpsb_fc04_error_log_ts = 0.0
        self._last_lpsb_fc04_payload_sig: tuple[int, ...] | None = None
        self._last_lpsb_fc04_ok: bool | None = None
        self._direct_diag_timer = QTimer(self)
        self._direct_diag_timer.setInterval(3000)  # 3초마다 수신 0이면 안내
        self._direct_diag_timer.timeout.connect(self._on_direct_diag_tick)
        # direct mode: "none" / "hpsb" / "lpsb"
        self._direct_mode: str = "none"
        self._build_ui()
        self._refresh_ports()
        self._set_connected_ui(False)
        # 실행 시 열리는 크기: 1440×900 (MacBook Pro 13" 해상도 기준).
        self._default_width = 1440
        self._default_height = 900
        self.resize(self._default_width, self._default_height)
        # 세로는 자유롭게 리사이즈 가능, 가로 최소 폭만 제한
        self.setMinimumSize(1024, 640)
        self._first_show = True
        self._log.log_line.connect(self._on_log_line)

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
        self.request_read_direct_hpsb_adc.connect(self._worker.on_request_read_direct_hpsb_adc, Qt.ConnectionType.QueuedConnection)
        self._worker.hpsb_adc_result.connect(self._on_hpsb_adc_result)
        # Doc tab signals (Unified Rule v1.0: FC04 read / FC05 write only)
        self.request_doc_fc04.connect(self._worker.on_request_doc_fc04, Qt.ConnectionType.QueuedConnection)
        self.request_doc_fc05.connect(self._worker.on_request_doc_fc05, Qt.ConnectionType.QueuedConnection)
        self._worker.doc_fc04_result.connect(self._on_doc_fc04_result)
        self._worker.doc_fc05_result.connect(self._on_doc_fc05_result)

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
        self._op_state_label = QLabel("● IDLE")
        self._op_state_label.setStyleSheet(
            "color: #757575; font-weight: bold; padding: 4px 10px; "
            "border: 1px solid #3a3a3a; border-radius: 4px;"
        )
        top_lay.addWidget(self._op_state_label)
        top_lay.addStretch()
        main_layout.addWidget(top)

        # ---- Tabs: 기존 테스트 UI 유지 + 문서 기반 Modbus 테스트 추가 ----
        tabs = QTabWidget()
        tabs.setDocumentMode(True)
        tabs.setMovable(False)
        tabs.setTabsClosable(False)
        main_layout.addWidget(tabs, 1)

        # =========================
        # Tab 1) 기존 테스트 UI (그대로 유지)
        # =========================
        tab_existing = QWidget()
        tabs.addTab(tab_existing, "보드 기능 테스트")
        tab_existing_lay = QVBoxLayout(tab_existing)
        tab_existing_lay.setContentsMargins(0, 0, 0, 0)
        tab_existing_lay.setSpacing(0)

        # ---- Content: Left(Mainboard + HPSB/LPSB) + Right(Log sidebar) ----
        content_row = QHBoxLayout()
        content_row.setSpacing(12)

        # ----- Left: Mainboard 테스트 (기존 구조 유지) -----
        left_w = QWidget()
        left_layout = QVBoxLayout(left_w)
        # 13인치(실질 세로 ~900)에서 첫 번째 컬럼 카드들이 균형 있게 보이도록
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
        btn_read_di.clicked.connect(self._on_read_di_clicked)
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
        btn_read_env.setToolTip("수동 1회 읽기 (READ ONCE).")
        btn_read_env.clicked.connect(self._on_read_sensor_clicked)
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
        self._btn_main_minimal = QPushButton("Mainboard minimal test (FC04 0/14)")
        self._btn_main_minimal.clicked.connect(self._on_mainboard_minimal_test)
        lay_ctrl.addWidget(self._btn_main_minimal)
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
        hpsb_tools = QHBoxLayout()
        self._chk_hpsb_simple = QCheckBox("Simple mode (no read-first probe)")
        self._chk_hpsb_simple.setChecked(True)
        self._chk_hpsb_simple.stateChanged.connect(lambda s: setattr(self, "_simple_hpsb_mode", s == Qt.CheckState.Checked.value))
        self._btn_hpsb_probe = QPushButton("HPSB Read Probe")
        self._btn_hpsb_probe.clicked.connect(self._run_hpsb_probe_only)
        hpsb_tools.addWidget(self._chk_hpsb_simple)
        hpsb_tools.addWidget(self._btn_hpsb_probe)
        lay_hpsb.addLayout(hpsb_tools)
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
            cur_lbl = QLabel("AVG:0 PKPK:0 I:OFF")
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
        lpsb_names = ["LPSB 2", "LPSB 4", "LPSB 8"]
        self._lpsb_slave_ids = [2, 4, 8]  # UI 이름과 매핑되는 실제 LPSB slave ID
        # 초기값: LPSB2(slave=2)만 기본 활성, LPSB4/8(slave=4/8)는 탐색 성공 시 활성
        self._lpsb_present = [True, False, False]
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
        # PKPK 기반 CURRENT 판정 임계값
        # - ADC1/ADC2: 단일 임계값(>=30 ON, <30 OFF)
        # - ADC3: 히스테리시스(OFF->ON:50, ON->OFF:37)
        self._lpsb_pkpk_on_threshold = 30
        self._lpsb_pkpk_off_threshold = 30
        # CH3도 전체 규칙을 동일하게: pkpk >= 30 -> ON, pkpk < 30 -> OFF
        self._lpsb_pkpk_on_threshold_ch3 = 30
        self._lpsb_pkpk_off_threshold_ch3 = 30
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
        self._log_edit.setMaximumBlockCount(self._log_max_lines)
        row_log_opt = QHBoxLayout()
        row_log_opt.addWidget(QLabel("Level:"))
        self._cmb_log_level = QComboBox()
        self._cmb_log_level.addItems(["BASIC", "FLOW", "RAW"])
        self._cmb_log_level.setCurrentText("FLOW")
        row_log_opt.addWidget(self._cmb_log_level)
        self._chk_log_raw = QCheckBox("Show RAW frames")
        self._chk_log_raw.setChecked(False)
        row_log_opt.addWidget(self._chk_log_raw)
        row_log_opt.addStretch()
        lay_log.addLayout(row_log_opt)
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

        tab_existing_lay.addLayout(content_row)

        # =========================
        # Tab 2) 문서 기반 Modbus 테스트 (추가 패널)
        # =========================
        tab_doc = DocModbusPanel(self._log)
        tab_doc.set_request_callbacks(
            # FC01/FC02/FC15는 Unified Rule v1.0에서 사용하지 않음(호환 유지하지 않음)
            lambda unit, start, count: None,
            lambda unit, start, count: None,
            lambda unit, start, count: self.request_doc_fc04.emit(unit, start, count),
            lambda unit, addr, value: self.request_doc_fc05.emit(unit, addr, value),
            lambda unit, start, values: None,
        )
        self._doc_panel = tab_doc
        tabs.addTab(tab_doc, "문서 기반 Modbus 테스트")

        self.setCentralWidget(central)

    # ---- Doc tab result handlers (worker thread -> UI thread) ----
    def _on_doc_fc01_result(self, ok: bool, bits: list | None, err: str | None):
        if hasattr(self, "_doc_panel"):
            self._doc_panel.on_fc01_result(ok, bits, err)

    def _on_doc_fc02_result(self, ok: bool, bits: list | None, err: str | None):
        if hasattr(self, "_doc_panel"):
            self._doc_panel.on_fc02_result(ok, bits, err)

    def _on_doc_fc04_result(self, ok: bool, regs: list | None, err: str | None):
        if hasattr(self, "_doc_panel"):
            self._doc_panel.on_fc04_result(ok, regs, err)

    def _on_doc_fc05_result(self, ok: bool, err: str | None):
        if hasattr(self, "_doc_panel"):
            self._doc_panel.on_fc05_result(ok, err)

    def _on_doc_fc15_result(self, ok: bool, err: str | None):
        if hasattr(self, "_doc_panel"):
            self._doc_panel.on_fc15_result(ok, err)

    def showEvent(self, event: QShowEvent):
        """첫 표시 시 기본 너비·높이로 열림 (저장된 창 크기 무시)."""
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
        (LPSB2/3/4 선택 및 direct/mainboard 모드와 무관하게 연결만 되면 활성화)
        """
        sel = int(getattr(self, "_selected_lpsb_index", 0))
        sel_present = 0 <= sel < len(self._lpsb_present) and bool(self._lpsb_present[sel])
        return bool(self._client.connected and sel_present)

    def _apply_lpsb_presence_ui(self):
        """보드 탐색 결과에 따라 LPSB 선택 버튼 활성 제어(숨기지 않음)."""
        direct_mode_active = self._client.connected and (self._direct_mode in ("hpsb", "lpsb"))
        for i, b in enumerate(self._lpsb_select_btns):
            present = bool(self._lpsb_present[i]) if i < len(self._lpsb_present) else False
            b.setVisible(True)
            b.setEnabled(self._client.connected and (not direct_mode_active) and present)

        sel = int(getattr(self, "_selected_lpsb_index", 0))
        if not (0 <= sel < len(self._lpsb_present) and self._lpsb_present[sel]):
            # 현재 선택이 사라졌으면 첫 번째 존재 보드로 자동 이동
            fallback = next((i for i, p in enumerate(self._lpsb_present) if p), 0)
            self._selected_lpsb_index = fallback
            sel = fallback

        for i, b in enumerate(self._lpsb_select_btns):
            checked = bool(i == sel and i < len(self._lpsb_present) and self._lpsb_present[i])
            b.blockSignals(True)
            b.setChecked(checked)
            b.blockSignals(False)
            b.setStyleSheet("background-color: #1976D2; color: white;" if checked else "")

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
        if self._direct_mode != "lpsb":
            self._stop_lpsb_auto_adc_poll("direct_hpsb_changed")
        # Auto-poll 없음; 상태 초기화만 수행
        self._stop_hpsb_auto_adc_poll("direct_hpsb_changed")
        self._set_op_state("IDLE")
        self._sync_current_log_timer()

    def _on_direct_lpsb_changed(self, state: int):
        checked = state == Qt.CheckState.Checked.value
        if checked:
            # LPSB direct 선택 시 HPSB direct 해제
            if self._chk_direct_hpsb.isChecked():
                self._chk_direct_hpsb.blockSignals(True)
                self._chk_direct_hpsb.setChecked(False)
                self._chk_direct_hpsb.blockSignals(False)
            self._direct_mode = "lpsb"
            self._lpsb_probe_ok = False
            self._lpsb_adc_last_ok = False
            self._lpsb_adc_state = {"avg": [0, 0, 0], "pkpk": [0, 0, 0], "cur": [0, 0, 0]}
            self._log.log_info("→ Mode: Direct LPSB (Slave 2)")
        else:
            if not self._chk_direct_hpsb.isChecked():
                self._direct_mode = "none"
                self._log.log_info("→ Mode: Mainboard routing")
        self._update_mode_label()
        self._set_connected_ui(self._client.connected)
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("direct_lpsb_changed")
        # Auto-poll 없음; 상태 초기화만 수행
        self._stop_lpsb_auto_adc_poll("direct_lpsb_changed")
        self._set_op_state("IDLE")
        self._sync_current_log_timer()

    def _tag_for_request(self, func: str, addr: int | str, count_or_value: int | str) -> str:
        """보드 태그: [MAIN], [HPSB], [LPSB2], [LPSB4], [LPSB8]. PC는 메인보드와만 통신하며, 메인보드가 UART2로 HPSB/LPSB 폴링한 결과를 보여줌."""
        try:
            a = int(addr)
        except (TypeError, ValueError):
            a = -1
        if a == SUB_SENSE_REG or a == SUB_COIL_STATUS_START or (func == "FC02" and 868 <= a <= 879):
            return "[HPSB][LPSB2][LPSB4][LPSB8]"
        if func == "FC05" and 0 <= a <= 3:
            # Mainboard local relay control: FC05 coil0..3 (0-based)
            # Direct mode uses unit=1/2 and is tagged below via route logs.
            if self._direct_mode == "hpsb":
                return "[DIRECT HPSB]"
            if self._direct_mode == "lpsb":
                return "[DIRECT LPSB]"
            return "[MAIN]"
        if func == "FC05" and SUB_HPSB_COIL_BASE <= a < SUB_HPSB_COIL_BASE + 3:
            return "[HPSB]"
        if func == "FC05" and SUB_LPSB_COIL_BASE <= a < SUB_LPSB_COIL_BASE + 9:
            if a < SUB_LPSB_COIL_BASE + 3:
                return "[LPSB2]"
            if a < SUB_LPSB_COIL_BASE + 6:
                return "[LPSB4]"
            return "[LPSB8]"
        if func == "FC05" and SUB_VB_COIL_BASE <= a < SUB_VB_COIL_BASE + SUB_VB_COIL_COUNT:
            idx = a - SUB_VB_COIL_BASE
            if idx == 0:
                return "[LPSB2]"
            if 1 <= idx <= 3:
                return "[LPSB4]"
            return "[LPSB8]"
        return "[MAIN]"

    def _on_request_log(self, unit: int, func: str, addr: int | str, count_or_value: int | str):
        self._last_log_tag = self._tag_for_request(func, addr, count_or_value)
        # OUTPUT_MONITORING 중에는 상세 통신 로그를 모두 억제
        if getattr(self, "_op_state", "IDLE") == "OUTPUT_MONITORING":
            return
        now = time.monotonic()
        is_direct_lpsb_fc04_poll = bool(
            self._direct_mode == "lpsb"
            and unit == 2
            and func == "FC04"
            and self._lpsb_adc_poll_running
        )
        emit_detailed_log = True
        if is_direct_lpsb_fc04_poll:
            emit_detailed_log = (now - self._last_direct_lpsb_fc04_req_log_ts) >= 2.0
            if emit_detailed_log:
                self._last_direct_lpsb_fc04_req_log_ts = now
        try:
            a = int(addr)
        except Exception:
            a = -1

        if self._direct_mode == "hpsb" and unit == 1:
            self._last_req_route = "direct-hpsb"
            if emit_detailed_log:
                self._log.log_info(f"[PC->HPSB] tx unit=1 fc={func.replace('FC', '')} addr={a} val/count={count_or_value}")
        elif self._direct_mode == "lpsb" and unit == 2:
            self._last_req_route = "direct-lpsb"
            if emit_detailed_log:
                self._log.log_info(f"[PC->LPSB] tx unit=2 fc={func.replace('FC', '')} addr={a} val/count={count_or_value}")
        else:
            self._last_req_route = "mainboard-routing"
            if emit_detailed_log:
                self._log.log_info(f"[PC->MB] tx unit={unit} fc={func.replace('FC', '')} addr={a} val/count={count_or_value}")
            if emit_detailed_log and func == "FC05" and SUB_HPSB_COIL_BASE <= a < SUB_HPSB_COIL_BASE + 3:
                coil = a - SUB_HPSB_COIL_BASE
                try:
                    v_bool = bool(int(count_or_value))
                except Exception:
                    v_bool = False
                raw = build_fc05_rtu_frame(1, coil, v_bool)
                raw_hex = " ".join(f"{x:02X}" for x in raw)
                self._log.log_info(f"[MB] parsed request target=HPSB slave=1 coil={coil}")
                self._log.log_info(f"[MB->HPSB] tx raw len={len(raw)} data={raw_hex}")
                self._log.log_info(f"[MB->HPSB] tx slave=1 fc=05 coil={coil}")
        if emit_detailed_log:
            self._log.log_request(self._last_log_tag, unit, func, addr, count_or_value)

    def _on_response_log(self, ok: bool, exception_code: int | None):
        # OUTPUT_MONITORING 중에는 상세 통신 로그를 모두 억제
        if getattr(self, "_op_state", "IDLE") == "OUTPUT_MONITORING":
            return
        now = time.monotonic()
        throttle_direct_lpsb_fc04 = bool(
            self._last_req_route == "direct-lpsb" and self._lpsb_adc_poll_running
        )
        emit_detailed_log = True
        if throttle_direct_lpsb_fc04:
            emit_detailed_log = (now - self._last_direct_lpsb_fc04_rsp_log_ts) >= 2.0
            if emit_detailed_log:
                self._last_direct_lpsb_fc04_rsp_log_ts = now
        if self._last_req_route == "mainboard-routing" and emit_detailed_log:
            if ok:
                self._log.log_info("[PC<-MB] response received")
            else:
                if exception_code is not None:
                    self._log.log_info(f"[PC<-MB] exception 0x{exception_code:02X}")
                else:
                    self._log.log_info("[PC<-MB] timeout waiting response")
        if emit_detailed_log:
            self._log.log_response(self._last_log_tag, ok, exception_code)

    def _on_tx_frame_hex(self, msg: str):
        """Direct HPSB FC05 시 실제 전송 프레임 hex 로그 (01 05 00 00 FF 00 CRC_L CRC_H 형식)."""
        self._log.log_info(msg)

    # ------------------------------------------------------------------
    # 동작 상태 머신: IDLE / READ_ONCE / OUTPUT_MONITORING
    # ------------------------------------------------------------------
    def _set_op_state(self, state: str):
        """동작 상태 전환 및 라벨 업데이트.
        state: "IDLE" | "READ_ONCE" | "OUTPUT_MONITORING"
        """
        prev = getattr(self, "_op_state", "IDLE")
        self._op_state = state
        _styles = {
            "IDLE":               ("● IDLE",                   "#757575"),
            "READ_ONCE":          ("● READ ONCE",              "#2196F3"),
            "OUTPUT_MONITORING":  ("● OUTPUT MONITORING (2s)", "#FF9800"),
        }
        text, color = _styles.get(state, ("● UNKNOWN", "#888888"))
        try:
            self._op_state_label.setText(text)
            self._op_state_label.setStyleSheet(
                f"color: {color}; font-weight: bold; padding: 4px 10px; "
                "border: 1px solid #3a3a3a; border-radius: 4px;"
            )
        except Exception:
            pass
        if state == "IDLE":
            self._hpsb_adc_log_timer.stop()
            self._hpsb_adc_poll_timer.stop()
            self._lpsb_log_timer.stop()
            self._lpsb_adc_poll_timer.stop()
            self._monitor_target = "none"
            if prev != "IDLE":
                try:
                    self._log.log_info("[STATE] → IDLE")
                except Exception:
                    pass
        elif state == "READ_ONCE":
            pass
        elif state == "OUTPUT_MONITORING":
            pass  # _start_output_monitoring에서 별도 로그 출력

    def _start_output_monitoring(self, target: str):
        """Relay/SSR ON 상태 진입 시 OUTPUT_MONITORING 상태로 전환.
        target: "hpsb" | "lpsb"
        출력이 모두 OFF 될 때까지 2s 주기 상태 로그를 계속 출력.
        """
        self._monitor_target = target
        self._monitor_retry_count = 0
        self._set_op_state("OUTPUT_MONITORING")
        board = "HPSB" if target == "hpsb" else "LPSB"
        self._log.log_info(f"[STATE] → OUTPUT MONITORING ({board}, 출력 OFF 시 자동 중단)")
        if target == "hpsb":
            self._hpsb_adc_log_timer.start()
        elif target == "lpsb":
            self._lpsb_log_timer.start()

    def _log_hpsb_monitoring_state(self):
        """HPSB 상태를 규격 포맷으로 로그 출력 (OUTPUT_MONITORING 전용 단일 상태 줄)."""
        try:
            avg  = self._hpsb_adc_state.get("avg",  [0, 0, 0])
            pkpk = self._hpsb_adc_state.get("pkpk", [0, 0, 0])
            cur  = self._hpsb_adc_state.get("cur",  [0, 0, 0])
            def _io(v): return "ON" if int(v) else "OFF"
            msg = (
                f"[HPSB] ADC1 AVG={int(avg[0])} PKPK={int(pkpk[0])} I={_io(cur[0])} | "
                f"ADC2 AVG={int(avg[1])} PKPK={int(pkpk[1])} I={_io(cur[1])} | "
                f"ADC3 AVG={int(avg[2])} PKPK={int(pkpk[2])} I={_io(cur[2])}"
            )
            self._log.log_info(msg)
        except Exception:
            self._log.log_info("[HPSB] ADC1 AVG=0 PKPK=0 I=OFF | ADC2 AVG=0 PKPK=0 I=OFF | ADC3 AVG=0 PKPK=0 I=OFF")

    def _log_lpsb_monitoring_state(self):
        """LPSB 상태를 규격 포맷으로 로그 출력 (OUTPUT_MONITORING 전용 단일 상태 줄)."""
        try:
            avg  = self._lpsb_adc_state.get("avg",  [0, 0, 0])
            pkpk = self._lpsb_adc_state.get("pkpk", [0, 0, 0])
            cur  = self._lpsb_adc_state.get("cur",  [0, 0, 0])
            def _io(v): return "ON" if int(v) else "OFF"
            msg = (
                f"[LPSB] ADC1 AVG={int(avg[0])} PKPK={int(pkpk[0])} I={_io(cur[0])} | "
                f"ADC2 AVG={int(avg[1])} PKPK={int(pkpk[1])} I={_io(cur[1])} | "
                f"ADC3 AVG={int(avg[2])} PKPK={int(pkpk[2])} I={_io(cur[2])}"
            )
            self._log.log_info(msg)
        except Exception:
            self._log.log_info("[LPSB] ADC1 AVG=0 PKPK=0 I=OFF | ADC2 AVG=0 PKPK=0 I=OFF | ADC3 AVG=0 PKPK=0 I=OFF")

    def _set_hpsb_write_pending_ui(self, idx: int, desired_value: bool):
        if idx < 0 or idx >= len(self._hpsb_btns):
            return
        btn = self._hpsb_btns[idx]
        prev = bool(not desired_value)
        self._pending_hpsb_ui_write = {
            "idx": idx,
            "prev": prev,
            "target": bool(desired_value),
        }
        btn.setEnabled(False)
        btn.setStyleSheet("background-color: #616161; color: #eeeeee;")
        self._hpsb_strips[idx].set_state(False)
        # 통신/상태값은 항상 동일 포맷으로 표시 (값 없으면 0/OFF)
        self._hpsb_current_labels[idx].setText("AVG:0 PKPK:0 I:OFF")

    def _apply_hpsb_write_ui(self, idx: int, value: bool):
        if idx < 0 or idx >= len(self._hpsb_btns):
            return
        btn = self._hpsb_btns[idx]
        btn.blockSignals(True)
        btn.setChecked(bool(value))
        btn.blockSignals(False)
        btn.setEnabled(True)
        btn.setStyleSheet("")
        self._hpsb_strips[idx].set_state(bool(value))
        self._sync_hpsb_adc_log_timer()

    def _rollback_hpsb_write_ui(self):
        if not self._pending_hpsb_ui_write:
            return
        idx = int(self._pending_hpsb_ui_write.get("idx", -1))
        prev = bool(self._pending_hpsb_ui_write.get("prev", False))
        self._apply_hpsb_write_ui(idx, prev)
        self._pending_hpsb_ui_write = None
        self._sync_hpsb_adc_log_timer()

    def _set_hpsb_adc_state_from_values(self, avg: list[int], pkpk: list[int], cur: list[int]):
        try:
            self._hpsb_adc_state["avg"] = [(int(avg[i]) if i < len(avg) else 0) & 0xFFFF for i in range(3)]
            self._hpsb_adc_state["pkpk"] = [(int(pkpk[i]) if i < len(pkpk) else 0) & 0xFFFF for i in range(3)]
            self._hpsb_adc_state["cur"] = [1 if (i < len(cur) and int(cur[i])) else 0 for i in range(3)]
            self._hpsb_adc_last_update_ms = int(time.monotonic() * 1000)
            self._hpsb_adc_last_ok = True
        except Exception:
            self._hpsb_adc_state = {"avg": [0, 0, 0], "pkpk": [0, 0, 0], "cur": [0, 0, 0]}
            self._hpsb_adc_last_update_ms = int(time.monotonic() * 1000)
            self._hpsb_adc_last_ok = False

    def _reset_hpsb_adc_state(self):
        self._hpsb_adc_state = {"avg": [0, 0, 0], "pkpk": [0, 0, 0], "cur": [0, 0, 0]}
        self._hpsb_adc_last_update_ms = int(time.monotonic() * 1000)
        self._hpsb_adc_last_ok = False

    def _update_hpsb_adc_labels(self):
        """HPSB RELAY1~3 버튼 아래 상태 라벨을 최신 값으로 갱신."""
        try:
            avg = self._hpsb_adc_state.get("avg", [0, 0, 0])
            pkpk = self._hpsb_adc_state.get("pkpk", [0, 0, 0])
            cur = self._hpsb_adc_state.get("cur", [0, 0, 0])
            for i in range(3):
                ion = "ON" if (i < len(cur) and int(cur[i])) else "OFF"
                a = int(avg[i]) if i < len(avg) else 0
                p = int(pkpk[i]) if i < len(pkpk) else 0
                if i < len(self._hpsb_current_labels):
                    self._hpsb_current_labels[i].setText(f"AVG:{a} PKPK:{p} I:{ion}")
        except Exception:
            for i in range(min(3, len(self._hpsb_current_labels))):
                self._hpsb_current_labels[i].setText("AVG:0 PKPK:0 I:OFF")

    def _is_hpsb_enabled(self) -> bool:
        """HPSB enable 상태: RELAY1~3 중 하나라도 ON이거나 Probe 성공 상태면 enable로 간주."""
        try:
            if getattr(self, "_hpsb_probe_ok", False):
                return True
            return any(bool(b.isChecked()) for b in self._hpsb_btns)
        except Exception:
            return False

    def _sync_hpsb_adc_log_timer(self):
        """HPSB: OUTPUT_MONITORING 상태에서만 2s 주기 모니터링 타이머 동작."""
        should_run = bool(
            self._client.connected
            and self._op_state == "OUTPUT_MONITORING"
            and self._monitor_target == "hpsb"
        )
        if should_run and not self._hpsb_adc_log_timer.isActive():
            self._hpsb_adc_log_timer.start()
        elif (not should_run) and self._hpsb_adc_log_timer.isActive():
            self._hpsb_adc_log_timer.stop()

    def _request_hpsb_adc_refresh_if_needed(self):
        """HPSB enable 중 최신값을 얻기 위해 FC04 read를 1초 주기로 트리거.
        - Direct HPSB: request_read_direct_hpsb_adc
        - Mainboard routing: request_read_sub (HPSB/LPSB 함께)
        중복 요청은 inflight 플래그로 방지.
        """
        if not self._client.connected:
            return
        # Direct HPSB
        if self._direct_mode == "hpsb":
            if not getattr(self, "_hpsb_adc_poll_inflight", False):
                self._hpsb_adc_poll_inflight = True
                self.request_read_direct_hpsb_adc.emit()
            return
        # Mainboard routing: sub read inflight 보호
        if not getattr(self, "_hpsb_probe_inflight", False):
            self._hpsb_probe_inflight = True
            self.request_read_sub.emit()

    def _on_hpsb_adc_log_tick(self):
        """2s OUTPUT MONITORING: FC04 read 요청 → result 핸들러에서 상태 로그 출력.
        HPSB Relay 중 하나라도 ON 상태인 동안 계속 실행, 모두 OFF 되면 자동 IDLE 복귀.
        """
        if not (self._client.connected and self._op_state == "OUTPUT_MONITORING" and self._monitor_target == "hpsb"):
            self._hpsb_adc_log_timer.stop()
            return
        # 모든 Relay가 OFF 상태이면 모니터링 중단
        try:
            any_on = any(bool(b.isChecked()) for b in self._hpsb_btns)
        except Exception:
            any_on = False
        if not any_on:
            self._hpsb_adc_log_timer.stop()
            self._log.log_info("[STATE] → IDLE (HPSB: 모든 Relay OFF → 모니터링 중단)")
            self._set_op_state("IDLE")
            return
        self._request_hpsb_adc_refresh_if_needed()

    def _allow_log_line(self, line: str) -> bool:
        if "[PC-TOOL]" in line:
            return True
        # Firmware-side UART debug lines should always be visible for protocol debugging.
        if "[UART2-MB]" in line:
            return True
        if "[MB->HPSB]" in line:
            return True
        if "[HPSB-SLAVE]" in line or "[HPSB]" in line:
            return True
        level = self._cmb_log_level.currentText() if hasattr(self, "_cmb_log_level") else "FLOW"
        low = line.lower()
        is_raw = (" raw " in low) or ("data=" in line and "tx" in low) or ("tx frame (hex)" in low)
        if is_raw and hasattr(self, "_chk_log_raw") and not self._chk_log_raw.isChecked():
            return False
        if level == "RAW":
            return True
        if level == "FLOW":
            if " | [RX] " in line:
                return False
            return True
        # BASIC: 성공/실패/예외 중심
        keys = ("Response:", "RX OK", "RX EXC", "RX ERR", "Fail", "OK", "Connect", "Disconnect")
        return any(k in line for k in keys)

    def _flush_log_buffer(self):
        if not self._log_buffer:
            return
        batch = []
        while self._log_buffer and len(batch) < 40:
            batch.append(self._log_buffer.popleft())
        if not batch:
            return
        try:
            self._log_edit.appendPlainText("\n".join(batch))
        except Exception:
            pass

    def _on_log_line(self, line: str):
        self._log_lines.append(line)
        if len(self._log_lines) > self._log_max_lines:
            self._log_lines = self._log_lines[-self._log_max_lines :]
        if not self._allow_log_line(line):
            return
        try:
            self._log_buffer.append(line)
        except Exception:
            pass

    def _log_clear(self):
        try:
            self._log.clear()
            self._log_lines.clear()
            self._log_buffer.clear()
            self._log_edit.clear()
        except Exception:
            pass

    def _on_read_di_clicked(self):
        """Read DI 버튼: READ_ONCE 1회 수행 후 IDLE 복귀."""
        if not self._client.connected:
            self._log.log_info("[MAIN] Not connected")
            return
        self._set_op_state("READ_ONCE")
        self.request_read_di.emit()

    def _on_read_sensor_clicked(self):
        """Read Sensor 버튼: READ_ONCE 1회 수행 후 IDLE 복귀."""
        if not self._client.connected:
            self._log.log_info("[MAIN] Not connected")
            return
        self._set_op_state("READ_ONCE")
        self.request_read_env.emit()

    def _on_mainboard_minimal_test(self):
        """최소 모드: Mainboard FC04 (DI bitmap) 1회 요청 (READ_ONCE)."""
        if not self._client.connected:
            self._log.log_info("[MAIN] Not connected")
            return
        self._set_op_state("READ_ONCE")
        self._log.log_info("[MINIMAL] FC04 addr=0 cnt=14 unit=9 (single shot)")
        self.request_read_di.emit()

    def _run_hpsb_probe_only(self):
        """수동 HPSB probe 전용 버튼. write 없이 sub read만 수행 (READ_ONCE)."""
        if not self._client.connected:
            self._log.log_info("[HPSB] Not connected")
            return
        if self._hpsb_probe_inflight:
            self._log.log_info("[HPSB] probe already in progress")
            return
        self._hpsb_probe_inflight = True
        self._hpsb_probe_only = True
        self._set_op_state("READ_ONCE")
        self._log.log_info("[HPSB] Read Probe START (FC04 via Mainboard routing)")
        self.request_read_sub.emit()

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
        # - Direct LPSB: LPSB FC04 (ADC raw 포함) read
        self._btn_read_sub.setEnabled(connected)
        self._btn_main_minimal.setEnabled(connected)
        # Direct HPSB에서는 자동 폴링 없음; Direct LPSB·메인보드에서는 Auto poll 사용 가능
        self._chk_auto_poll.setEnabled(connected and (self._direct_mode != "hpsb"))
        for b in self._hpsb_btns:
            b.setEnabled(connected)
        self._btn_hpsb_probe.setEnabled(connected and (self._direct_mode != "hpsb"))
        for b in self._lpsb_select_btns:
            b.setEnabled(connected and not direct_mode_active)
        for b in self._lpsb_ssr_btns:
            b.setEnabled(connected and not direct_mode_active)
        for chk in self._relay_checks:
            chk.setEnabled(connected)
        self._btn_pc_on.setEnabled(connected and not direct_mode_active)
        self._btn_pc_reset.setEnabled(connected and not direct_mode_active)
        self._update_mode_label()
        self._apply_lpsb_presence_ui()
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("set_connected_ui")
        self._stop_lpsb_auto_adc_poll("set_connected_ui")
        if connected:
            self._pc_rx_byte_count = 0
            self._status_badge.setText("Connected | RX count: 0")
            self._status_badge.setStyleSheet("color: #00c853; font-weight: bold; padding: 4px 8px;")
            self._seen_0xaa_since_connect = False
            self._modbus_fail_0xaa_hint_shown = False
            is_direct_hpsb = self._direct_mode == "hpsb"
            self._log.set_raw_line_only(is_direct_hpsb)  # Direct HPSB: \r\n만 보고 한 줄씩 출력
            self._log.log_info("[PC-TOOL] serial read thread started")
            # Mainboard→PC 문자열/원바이트도 확인하기 위해 direct 모드와 무관하게 raw polling을 유지
            self._raw_poll_timer.start()
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
            self._pending_lpsb_ui_write = None

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
            self._log.log_info("[MAIN] Select a valid port.")
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
                self._set_op_state("IDLE")
                self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), msg or "OK")
                self._log.log_info("[STATE] → IDLE (Connect 완료 — 버튼 클릭 전까지 자동 polling 없음)")
                if raw_only:
                    self._log.log_info("→ 직접 HPSB(raw_only): 시리얼만 열었습니다. 2초 수신 테스트 중…")
                    QTimer.singleShot(300, lambda: self.request_sniff.emit())
                if self._slave_id.value() != MAINBOARD_SLAVE_ID_DEFAULT:
                    self._log.log_info(f"경고: 메인보드 Slave ID는 {MAINBOARD_SLAVE_ID_DEFAULT}입니다. 현재 {self._slave_id.value()}이면 Read DI/Relay 응답이 없을 수 있습니다.")
            else:
                self._log.log_info(f"[MAIN] Connect fail: {msg or 'No response/timeout'}")
                self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), "Fail", msg or "No response/timeout")
        except Exception as e:
            err = f"{type(e).__name__}: {e}"
            self._log.log_info(f"[MAIN] Connect exception: {err}")
            self._log.log_tagged("[MAIN]", "Connect", port, self._baud.value(), "Fail", err)

    def _do_disconnect(self):
        try:
            self._client.disconnect()
        except Exception:
            pass
        if self._pending_hpsb_ui_write is not None:
            self._rollback_hpsb_write_ui()
        self._hpsb_probe_inflight = False
        self._hpsb_probe_ok = False
        self._pending_hpsb_write = None
        self._set_op_state("IDLE")
        self._set_connected_ui(False)
        self._log.log_tagged("[MAIN]", "Disconnect", "", "", "OK")
        self._stop_hpsb_auto_adc_poll("disconnect")

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
        self._log.log_info(
            "[HINT] Modbus 응답 없음(0xAA 전용 모드 추정):\n"
            "지금 보드가 0xAA 전용 모드로 동작 중입니다.\n"
            "Read DI / Relay / PC Status를 쓰려면 메인보드를 Modbus 모드로 바꿔야 합니다.\n"
            "→ Guro_Mainboard/app_config.h에서 ENABLE_PC_TEST_AA_STREAM=0 설정 후 다운로드하세요.\n"
            "그 다음 PC 툴에서 Disconnect 후 다시 Connect 하세요."
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
        if not bytes_list:
            return
        self._pc_rx_byte_count += len(bytes_list)
        self._status_badge.setText(f"Connected | RX count: {self._pc_rx_byte_count}")
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
            self._log.log_tagged("[MAIN]", "FC04", 0, 14, "OK")
        else:
            for led in self._di_leds:
                led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log_tagged("[MAIN]", "FC04", 0, 14, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요.")
        if self._op_state == "READ_ONCE":
            self._set_op_state("IDLE")

    def _on_pc_led_result(self, ok: bool, state: bool | None, err: str | None):
        if ok and state is not None:
            # high=빨강, low=파랑
            self._pc_led_led.set_di_state(True if state else False)
            self._log.log_tagged("[MAIN]", "FC04", 10, 1, "OK")
        else:
            self._pc_led_led.set_di_state(None)  # 미확인 = 회색
            msg = err or "No response/timeout"
            self._log.log_tagged("[MAIN]", "FC04", 10, 1, "Fail", msg)
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
                    # 센서 미구현/미연결(N/A)도 invalid sentinel으로 들어올 수 있다.
                    self._lbl_env_status.setText("Status: N/A" if int(flags) == 0xFFFF else "Status: SENSOR ERROR")
                else:
                    self._lbl_temp.setText(f"Temp: {t:.1f} °C")
                    self._lbl_rh.setText(f"RH: {rh:.1f} %")
                    self._lbl_env_status.setText("Status: OK")
                self._lbl_env_flags.setText(f"Flags: 0x{int(flags) & 0xFFFF:04X}")
            except Exception:
                pass
            self._log.log_tagged("[MAIN]", "FC04", 0, 14, "OK")
        else:
            msg = err or "No response/timeout"
            try:
                self._lbl_env_status.setText("Status: (read fail)")
            except Exception:
                pass
            self._log.log_tagged("[MAIN]", "FC04", 0, 14, "Fail", msg)
            self._show_0xaa_mode_hint_if_needed(msg)
        if self._op_state == "READ_ONCE":
            self._set_op_state("IDLE")

    def _on_write_result(self, ok: bool, err: str | None):
        tag = self._last_log_tag
        if ok:
            if self._pending_hpsb_ui_write is not None:
                idx = int(self._pending_hpsb_ui_write.get("idx", -1))
                target = bool(self._pending_hpsb_ui_write.get("target", False))
                self._apply_hpsb_write_ui(idx, target)
                self._pending_hpsb_ui_write = None
                self._log.log_info(f"[UI] HPSB RELAY{idx + 1} write success -> {'ON' if target else 'OFF'}")
                # Relay ON → OUTPUT_MONITORING 시작, 모두 OFF → IDLE
                try:
                    any_relay_on = any(bool(b.isChecked()) for b in self._hpsb_btns)
                except Exception:
                    any_relay_on = target
                if any_relay_on:
                    self._start_output_monitoring("hpsb")
                else:
                    self._set_op_state("IDLE")
            if self._pending_lpsb_ui_write is not None:
                idx = int(self._pending_lpsb_ui_write.get("idx", -1))
                target = bool(self._pending_lpsb_ui_write.get("target", False))
                if 0 <= idx < len(self._lpsb_ssr_state):
                    self._lpsb_ssr_state[idx] = target
                self._pending_lpsb_ui_write = None
                self._log.log_info(f"[UI] LPSB SSR{idx + 1} write success -> {'ON' if target else 'OFF'}")
                # SSR ON → OUTPUT_MONITORING 시작, 모두 OFF → IDLE
                try:
                    any_ssr_on = any(bool(s) for s in self._lpsb_ssr_state)
                except Exception:
                    any_ssr_on = target
                if any_ssr_on:
                    self._start_output_monitoring("lpsb")
                else:
                    self._set_op_state("IDLE")
            self._log.log_tagged(tag, "Write", "FC05/06", "-", "Response: OK")
        else:
            was_hpsb_pending = self._pending_hpsb_ui_write is not None
            was_lpsb_pending = self._pending_lpsb_ui_write is not None
            if self._pending_hpsb_ui_write is not None:
                idx = int(self._pending_hpsb_ui_write.get("idx", -1))
                self._rollback_hpsb_write_ui()
                self._log.log_info(f"[UI] HPSB RELAY{idx + 1} write fail -> UI rollback")
            if self._pending_lpsb_ui_write is not None:
                idx = int(self._pending_lpsb_ui_write.get("idx", -1))
                prev = bool(self._pending_lpsb_ui_write.get("prev", False))
                target = bool(self._pending_lpsb_ui_write.get("target", False))
                sel = int(getattr(self, "_selected_lpsb_index", 0))
                if 0 <= idx < len(self._lpsb_ssr_btns):
                    # 즉시 롤백하지 않고 read-back으로 실제 상태를 확정
                    self._pending_lpsb_verify = {"idx": idx, "prev": prev, "target": target, "sel": sel}
                    self._log.log_info(f"[UI] LPSB SSR{idx + 1} write fail -> read-back verify")
                self._pending_lpsb_ui_write = None
                if not getattr(self, "_hpsb_probe_inflight", False):
                    self._hpsb_probe_inflight = True
                    self.request_read_sub.emit()
            msg = err or "No response/timeout"
            self._log.log_tagged(tag, "Write", "FC05/06", "-", "Response: Fail", msg)
            if was_hpsb_pending:
                self._log.log_info(f"[UI] 통신 실패: HPSB 릴레이 제어 실패: {msg}")
            if was_lpsb_pending:
                self._log.log_info(f"[UI] 통신 실패: LPSB SSR 제어 실패: {msg}")
            # FC05 fail 시 Mainboard diag(4x4000..)를 1회 읽어 실패 slave/reason/rx_len을 바로 표시
            try:
                ok_d, regs_d, err_d = self._client.read_input_registers(4000, 32, unit=None)
                if ok_d and regs_d and len(regs_d) >= 32:
                    def _reason_text(v: int) -> str:
                        v = int(v) & 0xFFFF
                        return {
                            0: "NONE",
                            1: "TIMEOUT",
                            2: "EXCEPTION",
                            3: "RX_TOO_SHORT",
                            4: "SLAVE_MISMATCH",
                            5: "FC_MISMATCH",
                            6: "CRC_FAIL",
                            7: "PARSE_FAIL",
                        }.get(v, f"UNKNOWN({v})")
                    names = {1: "HPSB", 2: "LPSB2", 4: "LPSB4", 8: "LPSB8"}
                    base = 16
                    for _ in range(4):
                        sid_i = int(regs_d[base + 0]) & 0xFFFF
                        fc_i = int(regs_d[base + 1]) & 0xFFFF
                        rsn_i = int(regs_d[base + 2]) & 0xFFFF
                        len_i = int(regs_d[base + 3]) & 0xFFFF
                        nm = names.get(sid_i, f"SID{sid_i}")
                        self._log.log_info(
                            f"[DIAG][FC05_FAIL] {nm}(slave={sid_i}) fc=0x{fc_i:02X} reason={_reason_text(rsn_i)}({rsn_i}) rx_len={len_i}"
                        )
                        base += 4
                else:
                    self._log.log_info(f"[DIAG][FC05_FAIL] diag read fail: {err_d or 'no regs'}")
            except Exception as e:
                self._log.log_info(f"[DIAG][FC05_FAIL] diag exception={type(e).__name__}: {e}")
            self._show_0xaa_mode_hint_if_needed(msg)
            if msg and ("No response" in msg or "0 received" in msg):
                if self._direct_mode == "hpsb":
                    self._log.log_info(
                        "힌트: Direct HPSB — PC가 HPSB와 연결된 직렬 포트인지 확인하세요. "
                        "HPSB 전원, RS485(DE/배선), 보드 펌웨어(Modbus 슬레이브)를 점검하세요."
                    )
                else:
                    self._log.log_info("힌트: 메인보드 빌드에서 ENABLE_PC_TEST_AA_STREAM=0, USE_PC_TEST_UART1_SLAVE=1 인지 확인하세요. (0xAA 전용 모드면 Modbus 응답 없음)")

    def _on_read_once_clicked(self):
        """Read once 버튼 (READ_ONCE):
        - Mainboard routing: HPSB/LPSB sub read 1회
        - Direct LPSB 모드: LPSB FC04(0,14) 1회
        - Direct HPSB 모드: HPSB FC04(0,16) 1회
        """
        if not self._client.connected:
            self._log.log_info("[MAIN] Not connected")
            return
        self._set_op_state("READ_ONCE")
        if self._direct_mode == "lpsb":
            self._log.log_info("[PC-TOOL] route=direct-lpsb slave=2")
            self._log.log_info("[Direct LPSB][FC04] Read addr=0 count=14")
            self.request_read_direct_lpsb_adc.emit()
        elif self._direct_mode == "hpsb":
            self._log.log_info("[PC-TOOL] route=direct-hpsb slave=1")
            self._log.log_info("[Direct HPSB][FC04] Read addr=0 count=16")
            self.request_read_direct_hpsb_adc.emit()
        else:
            self._log.log_info("[PC-TOOL] route=mainboard-routing target=HPSB/LPSB")
            if self._hpsb_probe_inflight:
                self._log.log_info("[MAIN] read request ignored (request in progress)")
                self._set_op_state("IDLE")
                return
            self._hpsb_probe_inflight = True
            self.request_read_sub.emit()

    def _on_hpsb_relay_click(self, idx: int):
        """HPSB RELAY 버튼: FC05 write → Relay ON 이면 OUTPUT_MONITORING 진입."""
        btn = self._hpsb_btns[idx]
        value = btn.isChecked()
        if self._pending_hpsb_ui_write is not None:
            self._log.log_info("[HPSB] write pending... wait response")
            btn.blockSignals(True)
            btn.setChecked(bool(self._pending_hpsb_ui_write.get("target", False)))
            btn.blockSignals(False)
            return
        # 이전 모니터링 중지 후 새 쓰기 시작
        self._set_op_state("IDLE")
        self._set_hpsb_write_pending_ui(idx, value)
        if self._direct_mode == "hpsb":
            self._log.log_info("[PC-TOOL] route=direct-hpsb slave=1")
            self._log.log_info(f"[DEBUG] Button: HPSB RELAY{idx + 1} EN (Direct HPSB) -> FC05 slave=1 coil={idx} val={1 if value else 0}")
            self.request_write_direct_hpsb_coil.emit(idx, value)
        else:
            addr = SUB_HPSB_COIL_BASE + idx
            self._log.log_info("[PC-TOOL] route=mainboard-routing target=HPSB slave=1")
            if self._simple_hpsb_mode:
                self._log.log_info(f"[HPSB] simple mode: single FC05 write relay{idx + 1}")
                self.request_write_sub_coil.emit(addr, value)
            else:
                if self._hpsb_probe_inflight:
                    self._log.log_info("[HPSB] read-first probe already in progress")
                    return
                self._log.log_info(f"[HPSB] read-first probe start: request sub read before FC05 (relay{idx + 1})")
                self._hpsb_probe_inflight = True
                self._pending_hpsb_write = (addr, value)
                self.request_read_sub.emit()

    def _on_lpsb_select(self, idx: int):
        """LPSB 2/3/4 선택. 하나만 선택되도록. 선택 시 해당 보드 데이터로 SSR/current 갱신."""
        if idx < 0 or idx >= len(self._lpsb_present) or not self._lpsb_present[idx]:
            return
        self._selected_lpsb_index = idx
        for i, b in enumerate(self._lpsb_select_btns):
            b.setChecked(i == idx)
            b.setStyleSheet("background-color: #1976D2; color: white;" if i == idx else "")
        sense = getattr(self, "_last_sense", None)
        coils = getattr(self, "_last_coils", None)
        if sense and coils and len(sense) >= SUB_SENSE_COUNT and len(coils) >= 12:
            base_s = _lpsb_sense_base_from_selection(idx)
            base_c = 3 + idx * 3
            for i in range(3):
                on = base_c + i < len(coils) and coils[base_c + i]
                self._lpsb_ssr_state[i] = bool(on)
                self._lpsb_strips[i].set_state(on)
                self._lpsb_ssr_btns[i].setChecked(on)
                self._lpsb_current_labels[i].setText(_format_sense_channel(sense, base_s, i))
        # LPSB 선택에 따라 SSR 버튼 활성/비활성도 갱신
        self._update_lpsb_ssr_button_state()
        self._log_lpsb_selection_state("lpsb_select")

    def _on_lpsb_ssr_click(self, ssr_idx: int):
        """LPSB SSR 버튼: 선택된 LPSB2/3/4 보드에 대해 Mainboard routing FC05로 토글 제어."""
        btn = self._lpsb_ssr_btns[ssr_idx]
        if not self._client.connected:
            self._log.log_info("[LPSB] Not connected")
            btn.blockSignals(True)
            btn.setChecked(self._lpsb_ssr_state[ssr_idx])
            btn.blockSignals(False)
            self._lpsb_strips[ssr_idx].set_state(self._lpsb_ssr_state[ssr_idx])
            return

        # 이전 모니터링 중지 후 내부 상태 토글
        self._set_op_state("IDLE")
        prev_state = bool(self._lpsb_ssr_state[ssr_idx])
        new_state = bool(btn.isChecked())
        self._lpsb_ssr_state[ssr_idx] = new_state
        btn.blockSignals(True)
        btn.setChecked(new_state)
        btn.blockSignals(False)
        self._lpsb_strips[ssr_idx].set_state(new_state)

        # FC05 write 1회 → 성공 시 OUTPUT_MONITORING 시작 (in _on_write_result)
        self._pending_lpsb_ui_write = {"idx": ssr_idx, "prev": prev_state, "target": new_state}
        onoff_str = "ON" if new_state else "OFF"
        board_idx = int(getattr(self, "_selected_lpsb_index", 0))
        _lpsb_board_ids = [2, 4, 8]
        board_no = _lpsb_board_ids[board_idx] if board_idx < len(_lpsb_board_ids) else board_idx + 2
        addr = SUB_LPSB_COIL_BASE + board_idx * 3 + ssr_idx
        self._log.log_info("[PC-TOOL] route=mainboard-routing target=LPSB")
        self._log.log_info(f"[LPSB{board_no}] FC05 write SSR{ssr_idx + 1} -> {onoff_str} (addr={addr})")
        self.request_write_sub_coil.emit(addr, new_state)

    def _on_lpsb_adc_result(self, ok: bool, regs: list | None, err: str | None):
        """Direct LPSB: FC04(0,14) unified map — reg5..7 AVG, reg8..10 PKPK, reg11..13 CURRENT."""
        self._lpsb_adc_poll_inflight = False
        if not ok or regs is None or len(regs) < 14:
            # OUTPUT_MONITORING 중 단일 실패: 마지막 정상값 유지 + WARN 1줄 (최대 2회 재시도)
            if self._op_state == "OUTPUT_MONITORING":
                self._monitor_retry_count += 1
                self._log.log_info(f"MONITOR WARN - no response (keep last value)")
                if self._monitor_retry_count <= 2:
                    self._lpsb_adc_poll_inflight = True
                    def _retry_lpsb():
                        self._lpsb_adc_poll_inflight = False
                        if getattr(self, "_op_state", "IDLE") == "OUTPUT_MONITORING" and self._client.connected:
                            self._lpsb_adc_poll_inflight = True
                            self.request_read_direct_lpsb_adc.emit()
                    QTimer.singleShot(75, _retry_lpsb)
                else:
                    self._lpsb_adc_poll_inflight = False
                return
            msg = err or "FC04 read fail"
            self._log.log_info(f"[Direct LPSB][FC04] ERROR: {msg}")
            self._lpsb_adc_last_ok = False
            self._lpsb_comm_label.setText("Comm: (read fail)")
            for lbl in self._lpsb_current_labels:
                lbl.setText("current")
            return
        sig = tuple(int(x) for x in regs[:14])
        # OUTPUT_MONITORING 중에는 상세 FC04 dump 로그를 억제
        if self._op_state != "OUTPUT_MONITORING":
            self._log.log_info(f"[Direct LPSB][FC04] RX: {regs}")
        adc1, adc2, adc3 = regs[5], regs[6], regs[7]
        pk1, pk2, pk3 = regs[8], regs[9], regs[10]
        # CURRENT는 PKPK 값 기반으로 ON/OFF 판단
        state1 = "ON" if int(pk1) >= self._lpsb_pkpk_on_threshold else "OFF"
        state2 = "ON" if int(pk2) >= self._lpsb_pkpk_on_threshold else "OFF"
        state3 = "ON" if int(pk3) >= self._lpsb_pkpk_on_threshold_ch3 else "OFF"

        # 최신 LPSB ADC 상태를 dict에 저장 (1초 로그 타이머에서 사용)
        self._lpsb_adc_state["avg"] = [int(adc1), int(adc2), int(adc3)]
        self._lpsb_adc_state["pkpk"] = [int(pk1), int(pk2), int(pk3)]
        self._lpsb_adc_state["cur"] = [1 if state1 == "ON" else 0, 1 if state2 == "ON" else 0, 1 if state3 == "ON" else 0]
        self._lpsb_adc_last_ok = True

        self._lpsb_current_state_ch1 = state1
        self._lpsb_current_state_ch2 = state2
        self._lpsb_current_state = state3
        # FC04: reg5..7=AVG, reg8..10=PKPK
        self._lpsb_current_labels[0].setText(f"AVG:{adc1} PKPK:{pk1} I:{state1}")
        self._lpsb_current_labels[1].setText(f"AVG:{adc2} PKPK:{pk2} I:{state2}")
        self._lpsb_current_labels[2].setText(f"AVG:{adc3} PKPK:{pk3} I:{state3}")
        self._lpsb_comm_label.setText("Comm: OK")
        self._last_lpsb_fc04_payload_sig = sig
        self._last_lpsb_fc04_ok = True
        if not self._lpsb_probe_ok:
            self._lpsb_probe_ok = True
        # OUTPUT_MONITORING 중이면 규격 포맷 상태 로그 1줄만 출력, retry count 리셋
        if self._op_state == "OUTPUT_MONITORING" and self._monitor_target == "lpsb":
            self._monitor_retry_count = 0
            self._log_lpsb_monitoring_state()
        elif self._op_state == "READ_ONCE":
            self._set_op_state("IDLE")

    def _on_hpsb_adc_result(self, ok: bool, regs: list | None, err: str | None):
        """Direct HPSB: FC04(0,16) Unified Rule v1.1 HPSB map."""
        self._hpsb_adc_poll_inflight = False
        if not ok or regs is None or len(regs) < 16:
            # OUTPUT_MONITORING 중 단일 실패: 마지막 정상값 유지 + WARN 1줄 (최대 2회 재시도)
            if self._op_state == "OUTPUT_MONITORING":
                self._monitor_retry_count += 1
                self._log.log_info(f"MONITOR WARN - no response (keep last value)")
                if self._monitor_retry_count <= 2:
                    self._hpsb_adc_poll_inflight = True
                    def _retry_hpsb():
                        self._hpsb_adc_poll_inflight = False
                        if getattr(self, "_op_state", "IDLE") == "OUTPUT_MONITORING" and self._client.connected:
                            self._request_hpsb_adc_refresh_if_needed()
                    QTimer.singleShot(75, _retry_hpsb)
                else:
                    self._hpsb_adc_poll_inflight = False
                return
            msg = err or "FC04 read fail"
            self._log.log_info(f"[Direct HPSB][FC04] ERROR: {msg}")
            self._hpsb_comm_label.setText("Comm: (read fail)")
            self._reset_hpsb_adc_state()
            self._update_hpsb_adc_labels()
            return

        regs = [int(x) for x in regs[:16]]
        avg = regs[6:9]
        pkpk = regs[9:12]
        cur = regs[12:15]

        # OUTPUT_MONITORING 중에는 상세 FC04 dump 로그를 억제
        if self._op_state != "OUTPUT_MONITORING":
            self._log.log_info(f"[Direct HPSB][FC04] RX: {regs}")
            try:
                self._log.log_info(
                    f"[HPSB][DUMP][DIRECT] reg0..15={regs[:16]} | avg={avg} pkpk={pkpk} cur={cur}"
                )
            except Exception:
                pass
            self._log.log_info(f"[HPSB] ADC_AVG  ADC1={avg[0]} ADC2={avg[1]} ADC3={avg[2]}")
            self._log.log_info(f"[HPSB] ADC_PKPK ADC1={pkpk[0]} ADC2={pkpk[1]} ADC3={pkpk[2]}")
            self._log.log_info(
                f"[HPSB] CURRENT(ON/OFF)  CH1={'ON' if cur[0] else 'OFF'} CH2={'ON' if cur[1] else 'OFF'} CH3={'ON' if cur[2] else 'OFF'}"
            )

        self._set_hpsb_adc_state_from_values(avg, pkpk, cur)
        self._update_hpsb_adc_labels()
        self._hpsb_comm_label.setText("Comm: OK")
        if not self._hpsb_probe_ok:
            self._hpsb_probe_ok = True
        # OUTPUT_MONITORING 중이면 규격 포맷 상태 로그 1줄만 출력, retry count 리셋
        if self._op_state == "OUTPUT_MONITORING" and self._monitor_target == "hpsb":
            self._monitor_retry_count = 0
            self._log_hpsb_monitoring_state()
        elif self._op_state == "READ_ONCE":
            self._set_op_state("IDLE")
        self._sync_hpsb_adc_log_timer()

    def _start_hpsb_auto_adc_poll(self, context: str):
        if self._hpsb_adc_poll_timer.isActive():
            return
        self._hpsb_adc_poll_inflight = False
        self._hpsb_adc_poll_timer.start()
        self._log.log_info("[HPSB] Auto FC04 poll started (1000ms)")
        self._on_hpsb_adc_poll_tick()

    def _stop_hpsb_auto_adc_poll(self, context: str):
        if not self._hpsb_adc_poll_timer.isActive():
            return
        self._hpsb_adc_poll_timer.stop()
        self._hpsb_adc_poll_inflight = False
        self._log.log_info("[HPSB] Auto FC04 poll stopped")

    def _on_hpsb_adc_poll_tick(self):
        if (not self._client.connected) or (self._direct_mode != "hpsb"):
            self._stop_hpsb_auto_adc_poll("hpsb_poll_tick_not_ready")
            return
        if self._hpsb_adc_poll_inflight:
            return
        self._hpsb_adc_poll_inflight = True
        self.request_read_direct_hpsb_adc.emit()

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
        self._log.log_info("[LPSB] Auto ADC poll started (1000ms)")
        self._on_lpsb_adc_poll_tick()

    def _stop_lpsb_auto_adc_poll(self, context: str):
        if not self._lpsb_adc_poll_running:
            return
        self._lpsb_adc_poll_running = False
        self._lpsb_adc_poll_inflight = False
        self._lpsb_adc_poll_timer.stop()
        self._lpsb_log_timer.stop()
        self._lpsb_probe_ok = False
        self._log.log_info("[LPSB] Auto ADC poll stopped")

    def _update_lpsb_auto_adc_poll(self, context: str):
        """Direct LPSB 모드이면 ADC 자동 폴링 시작, 아니면 중지."""
        if (not self._client.connected) or (self._direct_mode != "lpsb"):
            self._stop_lpsb_auto_adc_poll(context)
            return
        self._start_lpsb_auto_adc_poll(context)

    def _on_lpsb_adc_poll_tick(self):
        """500ms 주기 자동 ADC 폴링 (화면 갱신). 이전 요청 응답 전에는 중복 요청 금지."""
        if (not self._client.connected) or (self._direct_mode != "lpsb"):
            self._stop_lpsb_auto_adc_poll("adc_poll_tick_not_ready")
            return
        if self._lpsb_adc_poll_inflight:
            return
        self._lpsb_adc_poll_inflight = True
        self.request_read_direct_lpsb_adc.emit()

    def _sync_lpsb_log_timer(self):
        """LPSB: OUTPUT_MONITORING 상태에서만 2s 주기 모니터링 타이머 동작."""
        should_run = bool(
            self._client.connected
            and self._op_state == "OUTPUT_MONITORING"
            and self._monitor_target == "lpsb"
        )
        if should_run and not self._lpsb_log_timer.isActive():
            self._lpsb_log_timer.start()
        elif (not should_run) and self._lpsb_log_timer.isActive():
            self._lpsb_log_timer.stop()

    def _on_lpsb_log_tick(self):
        """2s OUTPUT MONITORING: FC04 read 요청 → result 핸들러에서 상태 로그 출력.
        LPSB SSR 중 하나라도 ON 상태인 동안 계속 실행, 모두 OFF 되면 자동 IDLE 복귀.
        """
        if not (self._client.connected and self._op_state == "OUTPUT_MONITORING" and self._monitor_target == "lpsb"):
            self._lpsb_log_timer.stop()
            return
        # 모든 SSR이 OFF 상태이면 모니터링 중단
        # HPSB와 동일하게 버튼 체크 상태 기준으로 판단 (FC04 stale 데이터에 의한 오중단 방지)
        try:
            any_on = any(bool(b.isChecked()) for b in self._lpsb_ssr_btns)
        except Exception:
            any_on = any(bool(s) for s in self._lpsb_ssr_state)
        if not any_on:
            self._lpsb_log_timer.stop()
            self._log.log_info("[STATE] → IDLE (LPSB: 모든 SSR OFF → 모니터링 중단)")
            self._set_op_state("IDLE")
            return
        if not self._lpsb_adc_poll_inflight:
            self._lpsb_adc_poll_inflight = True
            if self._direct_mode == "lpsb":
                self.request_read_direct_lpsb_adc.emit()
            else:
                # Mainboard routing 모드: 선택 LPSB2/3/4 상태도 sub read 결과에서 추출
                if not getattr(self, "_hpsb_probe_inflight", False):
                    self._hpsb_probe_inflight = True
                    self.request_read_sub.emit()
                else:
                    self._lpsb_adc_poll_inflight = False

    def _on_sub_poll_tick(self):
        """2초 주기: Direct LPSB면 FC04(ADC) 읽기, 아니면 기존 HPSB/LPSB sub read."""
        if not self._client.connected:
            return
        if self._direct_mode == "lpsb":
            self.request_read_direct_lpsb_adc.emit()
        else:
            if self._hpsb_probe_inflight:
                return
            self._hpsb_probe_inflight = True
            self.request_read_sub.emit()

    def _sync_current_log_timer(self):
        """
        - Auto poll이 켜져 있고
        - Direct LPSB 모드가 아닐 때(=FC04 sub read 기반 데이터가 갱신되는 구간)
        1초마다 최근 FC04 응답 기반 HPSB/LPSB 전류 상태를 로그로 출력한다.
        """
        # Direct HPSB는 UI에서 Auto poll을 사용하지 않도록 되어 있으므로,
        # 1초 전류 로그도 Mainboard routing("none") 구간에서만 출력한다.
        should_run = bool(self._sub_auto_poll and self._client.connected and self._direct_mode == "none")
        if should_run and not self._current_log_timer.isActive():
            self._current_log_timer.start()
        elif (not should_run) and self._current_log_timer.isActive():
            self._current_log_timer.stop()

    def _on_current_log_tick(self):
        if (not self._client.connected) or self._direct_mode == "lpsb":
            return
        sense = self._last_sense
        if sense is None or len(sense) < SUB_SENSE_COUNT:
            return

        def _io(v: int) -> str:
            return "ON" if int(v) else "OFF"

        # HPSB block(변환 후): base=0, current=base+6..8
        h1 = _io(sense[6])
        h2 = _io(sense[7])
        h3 = _io(sense[8])

        # LPSB blocks(변환 후): base=9/18/27, current=base+6..8
        lpsb_bases = [9, 18, 27]
        lpsb_names = ["LPSB2", "LPSB4", "LPSB8"]
        parts = []
        for base, name in zip(lpsb_bases, lpsb_names):
            s1 = _io(sense[base + 6])
            s2 = _io(sense[base + 7])
            s3 = _io(sense[base + 8])
            parts.append(f"{name}:{s1}/{s2}/{s3}")

        self._log.log_info(f"[1s][CURRENT] HPSB:{h1}/{h2}/{h3} | " + " ".join(parts))

    def _on_auto_poll_changed(self, state):
        # QCheckBox.stateChanged는 int를 전달하므로 .value로 비교해야 한다.
        self._sub_auto_poll = state == Qt.CheckState.Checked.value
        self._log.log_info(
            f"[PC-TOOL] Auto poll={'ON' if self._sub_auto_poll else 'OFF'} | connected={self._client.connected} | direct_mode={self._direct_mode}"
        )
        if self._sub_auto_poll and self._client.connected:
            self._sub_poll_timer.start()
            self._on_sub_poll_tick()
            self._sync_current_log_timer()
        else:
            self._sub_poll_timer.stop()
            self._sync_current_log_timer()

    def _on_sub_data_result(self, ok: bool, sense: list | None, coils: list | None, flags: int | None, raw: dict | None, err: str | None):
        AGG_ERR_COMM_HPSB = 1
        AGG_ERR_COMM_LPSB = 2
        self._hpsb_probe_inflight = False
        self._lpsb_adc_poll_inflight = False
        # comm bad 시 진단(4000..)을 너무 자주 읽지 않도록 레이트리밋
        if not hasattr(self, "_diag_last_ms"):
            self._diag_last_ms = 0
        if not ok:
            if self._pending_hpsb_write is not None:
                self._log.log_info(f"[MB->HPSB] read-first probe fail -> write skipped ({err or 'sub read fail'})")
                self._pending_hpsb_write = None
                self._rollback_hpsb_write_ui()
            if getattr(self, "_hpsb_probe_only", False):
                self._log.log_info(f"[HPSB] Read Probe FAIL ({err or 'sub read fail'})")
                self._hpsb_probe_only = False
            # OUTPUT_MONITORING 중 단일 실패: 마지막 정상값 유지 + WARN 1줄
            # 최대 2회까지 재시도, 초과 시 다음 2s 틱까지 대기 (무한 루프 방지)
            if self._op_state == "OUTPUT_MONITORING":
                self._monitor_retry_count += 1
                self._log.log_info(f"MONITOR WARN - no response (keep last value)")
                if self._monitor_retry_count <= 2:
                    self._hpsb_probe_inflight = True
                    def _retry_sub():
                        self._hpsb_probe_inflight = False
                        if getattr(self, "_op_state", "IDLE") == "OUTPUT_MONITORING" and self._client.connected:
                            self._request_hpsb_adc_refresh_if_needed()
                    QTimer.singleShot(75, _retry_sub)
                else:
                    # 재시도 소진: 다음 2s 타이머 틱까지 대기 (inflight 해제)
                    self._hpsb_probe_inflight = False
                return
            # 일반 경로: 상태 리셋 + 상세 fail 로그
            self._hpsb_comm_label.setText("Comm: (read fail)")
            self._lpsb_comm_label.setText("Comm: (read fail)")
            self._reset_hpsb_adc_state()
            self._update_hpsb_adc_labels()
            self._sync_hpsb_adc_log_timer()
            if err:
                self._log.log_tagged("[HPSB][LPSB2][LPSB4][LPSB8]", "FC04", SUB_SENSE_REG, "sub", "Fail", err)
            if self._op_state == "READ_ONCE":
                self._set_op_state("IDLE")
            return
        # 성공 시에도 Auto poll이 "살아있음"을 로그로 확인 가능하게 1줄 남긴다.
        # (OUTPUT_MONITORING 중에는 이 상세 로그를 억제하고 상태 줄만 출력)
        if self._op_state != "OUTPUT_MONITORING":
            self._log.log_tagged("[HPSB][LPSB2][LPSB4][LPSB8]", "FC04", SUB_SENSE_REG, "sub", "OK")
        sense = sense or [0] * SUB_SENSE_COUNT
        coils = coils or [False] * 14
        flags = flags if flags is not None else 0
        # HPSB: RELAY1~3 상태 → 왼쪽 색상(빨강/파랑), 버튼 체크, AVG/PKPK/CURRENT 표시
        pending_idx = -1
        if self._pending_hpsb_ui_write is not None:
            pending_idx = int(self._pending_hpsb_ui_write.get("idx", -1))
        for i in range(3):
            on = i < len(coils) and coils[i]
            if i != pending_idx:
                self._hpsb_strips[i].set_state(on)
                self._hpsb_btns[i].setChecked(on)
        # HPSB v1.1: sense[0..2]=AVG, [3..5]=PKPK, [6..8]=CUR
        try:
            avg = [int(sense[0]), int(sense[1]), int(sense[2])]
            pkpk = [int(sense[3]), int(sense[4]), int(sense[5])]
            cur = [int(sense[6]), int(sense[7]), int(sense[8])]
        except Exception:
            avg, pkpk, cur = [0, 0, 0], [0, 0, 0], [0, 0, 0]
        self._set_hpsb_adc_state_from_values(avg, pkpk, cur)
        self._update_hpsb_adc_labels()
        hpsb_comm_bad = bool(flags & AGG_ERR_COMM_HPSB)
        self._hpsb_comm_label.setText("Comm: OK" if not hpsb_comm_bad else "Comm: Timeout/CRC")
        # raw dump (Mainboard routing HPSB block 100..115) - 진단용 (OUTPUT_MONITORING 중 억제)
        if self._op_state != "OUTPUT_MONITORING" and raw and isinstance(raw, dict) and "hpsb_100_115" in raw:
            try:
                r = raw.get("hpsb_100_115") or []
                if isinstance(r, list) and len(r) >= 16:
                    self._log.log_info(
                        f"[HPSB][DUMP][MB] flags=0x{int(flags) & 0xFFFF:04X} comm={'BAD' if hpsb_comm_bad else 'OK'} | reg100..115={r[:16]} | "
                        f"avg={r[6:9]} pkpk={r[9:12]} cur={r[12:15]}"
                    )
            except Exception:
                pass
        if getattr(self, "_hpsb_probe_only", False):
            self._hpsb_probe_only = False
            if hpsb_comm_bad:
                # 추가 진단: Mainboard FC04 diag(4000..)에서 마지막 서브폴링 실패 원인 조회
                try:
                    ok_d, regs_d, err_d = self._client.read_input_registers(4000, 32, unit=None)
                    if ok_d and regs_d and len(regs_d) >= 32:
                        def _reason_text(v: int) -> str:
                            v = int(v) & 0xFFFF
                            return {
                                0: "NONE",
                                1: "TIMEOUT",
                                2: "EXCEPTION",
                                3: "RX_TOO_SHORT",
                                4: "SLAVE_MISMATCH",
                                5: "FC_MISMATCH",
                                6: "CRC_FAIL",
                                7: "PARSE_FAIL",
                            }.get(v, f"UNKNOWN({v})")

                        # UART2 ORE(overrun) counter (4x4009)
                        try:
                            ore = int(regs_d[9]) & 0xFFFF
                            self._log.log_info(f"[DIAG][UART2] ORE_COUNT(4x4009)={ore}")
                        except Exception:
                            pass

                        # legacy last snapshot (regs[12..15])는 그대로 유지
                        sid = int(regs_d[12]) & 0xFFFF
                        fc = int(regs_d[13]) & 0xFFFF
                        reason = int(regs_d[14]) & 0xFFFF
                        rxlen = int(regs_d[15]) & 0xFFFF
                        self._log.log_info(
                            f"[HPSB] Read Probe FAIL (HPSB comm bad: flags indicate Timeout/CRC) | "
                            f"last_sub_fail: slave={sid} fc=0x{fc:02X} reason={_reason_text(reason)}({reason}) rx_len={rxlen}"
                        )

                        # per-slave table (regs[16..31] = 4 rows)
                        names = {1: "HPSB", 2: "LPSB2", 4: "LPSB4", 8: "LPSB8"}
                        base = 16
                        for _ in range(4):
                            sid_i = int(regs_d[base + 0]) & 0xFFFF
                            fc_i = int(regs_d[base + 1]) & 0xFFFF
                            rsn_i = int(regs_d[base + 2]) & 0xFFFF
                            len_i = int(regs_d[base + 3]) & 0xFFFF
                            nm = names.get(sid_i, f"SID{sid_i}")
                            self._log.log_info(
                                f"[DIAG][SUBFAIL] {nm}(slave={sid_i}) fc=0x{fc_i:02X} reason={_reason_text(rsn_i)}({rsn_i}) rx_len={len_i}"
                            )
                            base += 4
                    else:
                        self._log.log_info(
                            f"[HPSB] Read Probe FAIL (HPSB comm bad: flags indicate Timeout/CRC) | "
                            f"diag read fail: {err_d or 'no regs'}"
                        )
                except Exception as e:
                    self._log.log_info(
                        f"[HPSB] Read Probe FAIL (HPSB comm bad: flags indicate Timeout/CRC) | "
                        f"diag exception={type(e).__name__}: {e}"
                    )
            else:
                def _io(v: int) -> str:
                    return "ON" if int(v) else "OFF"
                self._log.log_info(
                    f"[HPSB] Read Probe OK | "
                    f"ADC1 avg:{avg[0]} pkpk:{pkpk[0]} on/off:{_io(cur[0])} : "
                    f"ADC2 avg:{avg[1]} pkpk:{pkpk[1]} on/off:{_io(cur[1])} : "
                    f"ADC3 avg:{avg[2]} pkpk:{pkpk[2]} on/off:{_io(cur[2])}"
                )
                if not self._hpsb_probe_ok:
                    self._hpsb_probe_ok = True
                # Read Probe (READ_ONCE): 로그 출력 후 IDLE 복귀
                if self._op_state == "READ_ONCE":
                    self._set_op_state("IDLE")
        else:
            # 일반 Auto poll/Read 경로에서도 comm bad이면 진단을 1회 자동 출력
            # (OUTPUT_MONITORING 중에는 DIAG 블록 억제)
            lpsb_comm_bad = bool(flags & AGG_ERR_COMM_LPSB)
            if self._op_state != "OUTPUT_MONITORING" and (hpsb_comm_bad or lpsb_comm_bad):
                try:
                    from time import time
                    now_ms = int(time() * 1000)
                except Exception:
                    now_ms = 0
                if now_ms == 0 or (now_ms - int(getattr(self, "_diag_last_ms", 0))) >= 2000:
                    self._diag_last_ms = now_ms
                    try:
                        ok_d, regs_d, err_d = self._client.read_input_registers(4000, 32, unit=None)
                        if ok_d and regs_d and len(regs_d) >= 32:
                            def _reason_text(v: int) -> str:
                                v = int(v) & 0xFFFF
                                return {
                                    0: "NONE",
                                    1: "TIMEOUT",
                                    2: "EXCEPTION",
                                    3: "RX_TOO_SHORT",
                                    4: "SLAVE_MISMATCH",
                                    5: "FC_MISMATCH",
                                    6: "CRC_FAIL",
                                    7: "PARSE_FAIL",
                                }.get(v, f"UNKNOWN({v})")

                            # UART2 ORE(overrun) counter (4x4009)
                            try:
                                ore = int(regs_d[9]) & 0xFFFF
                                self._log.log_info(f"[DIAG][UART2] ORE_COUNT(4x4009)={ore}")
                            except Exception:
                                pass

                            names = {1: "HPSB", 2: "LPSB2", 4: "LPSB4", 8: "LPSB8"}
                            base = 16
                            for _ in range(4):
                                sid_i = int(regs_d[base + 0]) & 0xFFFF
                                fc_i = int(regs_d[base + 1]) & 0xFFFF
                                rsn_i = int(regs_d[base + 2]) & 0xFFFF
                                len_i = int(regs_d[base + 3]) & 0xFFFF
                                nm = names.get(sid_i, f"SID{sid_i}")
                                self._log.log_info(
                                    f"[DIAG][SUBFAIL] {nm}(slave={sid_i}) fc=0x{fc_i:02X} reason={_reason_text(rsn_i)}({rsn_i}) rx_len={len_i}"
                                )
                                base += 4
                        else:
                            self._log.log_info(f"[DIAG][SUBFAIL] diag read fail: {err_d or 'no regs'}")
                    except Exception as e:
                        self._log.log_info(f"[DIAG][SUBFAIL] diag exception={type(e).__name__}: {e}")
        # LPSB: 선택된 보드(LPSB2/3/4)에 대해 SSR1~3 상태·전류 블록 표시
        sel = getattr(self, "_selected_lpsb_index", 0)
        base_s = _lpsb_sense_base_from_selection(sel)
        base_c = 3 + sel * 3
        for i in range(3):
            on = base_c + i < len(coils) and coils[base_c + i]
            self._lpsb_ssr_state[i] = bool(on)
            self._lpsb_strips[i].set_state(on)
            self._lpsb_ssr_btns[i].setChecked(on)
            self._lpsb_current_labels[i].setText(_format_sense_channel(sense, base_s, i))
        # 항상 최신 ADC 값을 캐시 (OUTPUT_MONITORING 시 첫 사이클에도 실제 값 출력)
        try:
            self._lpsb_adc_state["avg"]  = [int(sense[base_s + 0]), int(sense[base_s + 1]), int(sense[base_s + 2])]
            self._lpsb_adc_state["pkpk"] = [int(sense[base_s + 3]), int(sense[base_s + 4]), int(sense[base_s + 5])]
            self._lpsb_adc_state["cur"]  = [int(sense[base_s + 6]), int(sense[base_s + 7]), int(sense[base_s + 8])]
        except Exception:
            pass
        # FC05 fail 후 read-back 검증: 실제 coil 상태와 목표가 같으면 성공으로 확정
        if self._pending_lpsb_verify is not None:
            try:
                v_idx = int(self._pending_lpsb_verify.get("idx", -1))
                v_sel = int(self._pending_lpsb_verify.get("sel", sel))
                v_target = bool(self._pending_lpsb_verify.get("target", False))
                v_prev = bool(self._pending_lpsb_verify.get("prev", False))
                v_base_c = 3 + v_sel * 3
                actual = bool(v_base_c + v_idx < len(coils) and coils[v_base_c + v_idx])
                if actual == v_target:
                    self._log.log_info(f"[UI] LPSB SSR{v_idx + 1} write verified by read-back -> {'ON' if actual else 'OFF'}")
                    if v_target:
                        self._start_output_monitoring("lpsb")
                else:
                    self._lpsb_ssr_state[v_idx] = v_prev
                    if 0 <= v_idx < len(self._lpsb_ssr_btns):
                        self._lpsb_ssr_btns[v_idx].blockSignals(True)
                        self._lpsb_ssr_btns[v_idx].setChecked(v_prev)
                        self._lpsb_ssr_btns[v_idx].blockSignals(False)
                        self._lpsb_strips[v_idx].set_state(v_prev)
                    self._log.log_info(f"[UI] LPSB SSR{v_idx + 1} write verify mismatch -> rollback")
            except Exception:
                pass
            self._pending_lpsb_verify = None
        self._lpsb_comm_label.setText("Comm: OK" if not (flags & AGG_ERR_COMM_LPSB) else "Comm: Timeout/CRC")
        self._last_sense = sense
        self._last_coils = coils
        # raw 블록의 alive(reg0) 기반으로 LPSB 존재 보드 자동 탐색
        try:
            if isinstance(raw, dict):
                # LPSB2(slave=2)는 기본 사용 보드로 항상 존재 취급.
                p2 = True
                p4 = bool((raw.get("lpsb2_300_313") or [0])[0] == 1)
                p8 = bool((raw.get("lpsb3_400_413") or [0])[0] == 1)
                new_present = [p2, p4, p8]
                if new_present != self._lpsb_present:
                    self._lpsb_present = new_present
                    self._apply_lpsb_presence_ui()
                    self._update_lpsb_ssr_button_state()
        except Exception:
            pass
        if self._op_state == "OUTPUT_MONITORING":
            # OUTPUT_MONITORING 중: 규격 포맷 상태 로그 1줄만 출력, retry count 리셋
            self._monitor_retry_count = 0
            if self._monitor_target == "hpsb":
                self._log_hpsb_monitoring_state()
            elif self._monitor_target == "lpsb":
                # _lpsb_adc_state는 위에서 이미 업데이트됨 → 바로 로그 출력
                self._log_lpsb_monitoring_state()
        else:
            self._log.log_tagged("[HPSB][LPSB2][LPSB4][LPSB8]", "FC04", SUB_SENSE_REG, "sub", "OK")
        # READ_ONCE (Read once 버튼) 경로: 완료 후 IDLE 복귀
        if self._op_state == "READ_ONCE":
            self._set_op_state("IDLE")
        self._sync_hpsb_adc_log_timer()
        if self._pending_hpsb_write is not None:
            addr, value = self._pending_hpsb_write
            self._pending_hpsb_write = None
            self._log.log_info("[MB->HPSB] read-first probe OK (sub read success)")
            self._log.log_info(f"[DEBUG] continue FC05 after probe: addr={addr} val={1 if value else 0}")
            self.request_write_sub_coil.emit(addr, value)

    def closeEvent(self, event):
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._thread.quit()
        self._thread.wait(1000)
        event.accept()
