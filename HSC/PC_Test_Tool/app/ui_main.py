"""
Main window: field test utility — MAIN I/O, HPSB, LPSB, Door, Alarms, Currents.
Layout: top bar, left (MAIN I/O, ON/OFF, Door, Alarms), right (HPSB, LPSB tabs, Currents), bottom (Control Actions, Log).
Serial/worker/H2TECH addressing unchanged for existing reads/writes.
"""
from datetime import datetime
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QComboBox, QPushButton, QLabel, QSpinBox, QCheckBox, QTableWidget,
    QTableWidgetItem, QFrame, QMessageBox, QLineEdit, QPlainTextEdit,
    QFileDialog, QScrollArea, QSizePolicy, QTabWidget, QGroupBox, QApplication,
)
from PyQt6.QtCore import QTimer, pyqtSignal, Qt
from PyQt6.QtGui import QFont
from itertools import chain

from .modbus_client import ModbusClient
from .worker import PollWorker, create_worker_and_thread
from .logger import LogHandler
from .h2tech_map import (
    ONOFF_START, ONOFF_COUNT, DOOR_START, DOOR_COUNT,
    ALARMS_START, ALARMS_COUNT, CMD_ONOFF_START, CMD_ONOFF_COUNT,
    CURRENT_START, CURRENT_COUNT,
    DOOR_OPEN_1_COIL, DOOR_OPEN_2_COIL,
    VB_ONOFF_8_COIL, VB_ONOFF_12_COIL,
    INVALID_COIL_899, INVALID_COIL_900,
    MAIN_IO_ENABLED,
)

# ---- Industrial dark theme QSS ----
# Background: #1e1e1e | Cards: #2a2a2a | Text: #e0e0e0 | Accent: #2d8cf0
# ON LED: #00c853 | OFF LED: #555555 | Alarm LED: #ff1744 (blinking)
DARK_QSS = """
QMainWindow, QWidget { background-color: #1e1e1e; }
QFrame#card { background-color: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 6px; padding: 8px; }
QGroupBox { font-weight: bold; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; margin-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; }
QLabel { color: #e0e0e0; }
QComboBox, QSpinBox, QLineEdit { background-color: #252525; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; padding: 4px; min-height: 20px; }
QComboBox:hover, QSpinBox:hover, QLineEdit:hover { border-color: #2d8cf0; }
QComboBox::drop-down, QSpinBox::down-arrow { border: none; }
QPushButton { background-color: #252525; color: #e0e0e0; border: 1px solid #3a3a3a; border-radius: 4px; padding: 6px 12px; min-height: 20px; }
QPushButton:hover { background-color: #2a2a2a; border-color: #2d8cf0; }
QPushButton:pressed { background-color: #2d8cf0; color: white; }
QPushButton:disabled { background-color: #252525; color: #555555; border-color: #2a2a2a; }
QPushButton#primary { background-color: #2d8cf0; color: white; border-color: #2574d0; }
QPushButton#primary:hover { background-color: #4da3f2; }
QPushButton#danger { background-color: #4a2a2a; }
QPushButton#danger:hover { background-color: #ff1744; color: white; }
QCheckBox { color: #e0e0e0; spacing: 6px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 3px; border: 2px solid #3a3a3a; background: #252525; }
QCheckBox::indicator:checked { background: #2d8cf0; border-color: #2d8cf0; }
QTableWidget { background-color: #2a2a2a; color: #e0e0e0; gridline-color: #3a3a3a; }
QTableWidget::item { padding: 4px; }
QHeaderView::section { background-color: #252525; color: #e0e0e0; padding: 6px; }
QPlainTextEdit { background-color: #1e1e1e; color: #e0e0e0; font-family: Consolas, Monaco, monospace; font-size: 12px; border: 1px solid #3a3a3a; border-radius: 4px; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: #2a2a2a; width: 12px; border-radius: 6px; margin: 0; }
QScrollBar::handle:vertical { background: #555555; border-radius: 6px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #2d8cf0; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
"""


def list_serial_ports():
    try:
        import serial.tools.list_ports
        return [p.device for p in serial.tools.list_ports.comports()]
    except Exception:
        return []


class LedIndicator(QFrame):
    """Small LED: on=#00c853, off=#555555; alarm mode = #ff1744 red, blinking every 400ms."""
    def __init__(self, parent=None, alarm_mode: bool = False):
        super().__init__(parent)
        self.setFixedSize(14, 14)
        self.setStyleSheet("border-radius: 7px; background-color: #555555;")
        self._on = False
        self._alarm_mode = alarm_mode
        self._blink_visible = False
        self._blink_timer = QTimer(self)
        self._blink_timer.timeout.connect(self._blink_toggle)
        if alarm_mode:
            self._blink_timer.setInterval(400)

    def _blink_toggle(self):
        if not self._alarm_mode or not self._on:
            return
        self._blink_visible = not self._blink_visible
        if self._blink_visible:
            self.setStyleSheet("border-radius: 7px; background-color: #ff1744;")
        else:
            self.setStyleSheet("border-radius: 7px; background-color: #c41040;")

    def set_state(self, on: bool):
        self._on = on
        if self._alarm_mode:
            if on:
                self._blink_timer.start(400)
            else:
                self._blink_timer.stop()
        self._update_style()

    def _update_style(self):
        if self._alarm_mode and self._on and not self._blink_timer.isActive():
            self._blink_timer.start(400)
        if self._alarm_mode and self._on:
            self.setStyleSheet("border-radius: 7px; background-color: #c41040;")
        elif self._on:
            self.setStyleSheet("border-radius: 7px; background-color: #00c853;")
        else:
            if self._alarm_mode:
                self._blink_timer.stop()
            self.setStyleSheet("border-radius: 7px; background-color: #555555;")

    def set_alarm_mode(self, alarm: bool):
        self._alarm_mode = alarm
        if alarm and self._on:
            self._blink_timer.start(400)
        else:
            self._blink_timer.stop()
        self._update_style()


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
    request_poll = pyqtSignal()

    ONOFF_LABELS = [
        "Door1", "Door2", "HPSB Fan", "Heated Bench", "HPSB Spare",
        "LPSB1 Internal1", "LPSB1 Internal2", "LPSB1 Charger",
        "LPSB2 External1", "LPSB2 External2", "LPSB2 Spare",
        "LPSB3 Spare1", "LPSB3 Spare2", "LPSB3 Spare3",
        "Reserved", "Reserved",
    ]
    DOOR_LABELS = ["MAG1", "MAG2", "BTN1", "BTN2"]
    CURRENT_NAMES = [
        "HPSB P1", "HPSB P2", "HPSB P3",
        "LPSB1 P1", "LPSB1 P2", "LPSB1 P3",
        "LPSB2 P1", "LPSB2 P2", "LPSB2 P3",
        "LPSB3 P1", "LPSB3 P2", "LPSB3 P3",
        "Door1", "Door2",
    ]

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HSC PC Test Tool — MAIN Modbus RTU")
        self.setStyleSheet(DARK_QSS)
        self._client = ModbusClient()
        self._log = LogHandler()
        self._poll_timer = QTimer(self)
        self._poll_timer.timeout.connect(self._on_timer)
        self._thread, self._worker = create_worker_and_thread(self._client)
        self._thread.start()
        self.request_poll.connect(self._worker.on_request_poll, Qt.ConnectionType.QueuedConnection)
        self._connect_worker_signals()
        self._last_poll_ts: str = "—"
        self._current_min: list[int | None] = [None] * 14
        self._current_max: list[int | None] = [None] * 14
        self._log_lines: list[str] = []
        self._build_ui()
        self._refresh_ports()
        self._set_connected_ui(False)

    def _connect_worker_signals(self):
        self._worker.onoff_result.connect(self._on_onoff)
        self._worker.door_result.connect(self._on_door)
        self._worker.alarms_result.connect(self._on_alarms)
        self._worker.cmd_onoff_result.connect(self._on_cmd_onoff)
        self._worker.currents_result.connect(self._on_currents)

    def _build_ui(self):
        central = QWidget()
        central.setStyleSheet("background-color: #1e1e1e;")
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(12)
        main_layout.setContentsMargins(12, 12, 12, 12)

        # ---- Top bar ----
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
        self._slave_id.setValue(1)
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
        self._auto_poll = QCheckBox("Auto poll")
        self._auto_poll.stateChanged.connect(self._on_auto_poll_changed)
        top_lay.addWidget(self._auto_poll)
        top_lay.addWidget(QLabel("Interval (ms):"))
        self._poll_interval = QSpinBox()
        self._poll_interval.setRange(100, 5000)
        self._poll_interval.setValue(500)
        self._poll_interval.valueChanged.connect(self._set_poll_interval)
        self._poll_interval.setMinimumWidth(70)
        top_lay.addWidget(self._poll_interval)
        self._last_poll_label = QLabel("Last poll: —")
        self._last_poll_label.setStyleSheet("color: #888888;")
        top_lay.addWidget(self._last_poll_label)
        main_layout.addWidget(top)

        # ---- Main 2-column ----
        content = QWidget()
        content.setMinimumHeight(380)
        content_lay = QHBoxLayout(content)
        content_lay.setSpacing(12)

        # LEFT column: MAIN I/O, Status Monitor (3 cards)
        left_col = QVBoxLayout()
        left_col.setSpacing(12)

        # Card: MAIN Board I/O (Option 2: placeholders; enable read/write when MAIN_IO_ENABLED)
        card_main_io, lay_main_io = card_frame("MAIN Board I/O")
        main_di_labels = [
            "DI1: PC_RESET_BTN", "DI2: PC_ON_BTN", "DI3: PC_RESET_EN", "DI4: PC_ON_EN",
            "DI5: RESERVED", "DI6: RESERVED", "DI7: RESERVED", "DI8: RESERVED",
        ]
        main_do_labels = ["DO1: PC_RESET_EN", "DO2: PC_ON_EN", "DO3: PC_LED", "DO4: RESERVED"]
        grid_di = QGridLayout()
        self._main_di_leds = []
        for i in range(8):
            led = LedIndicator(alarm_mode=False)
            lbl = QLabel(main_di_labels[i])
            lbl.setStyleSheet("color: #e0e0e0;")
            grid_di.addWidget(led, i // 4, (i % 4) * 2)
            grid_di.addWidget(lbl, i // 4, (i % 4) * 2 + 1)
            self._main_di_leds.append(led)
        lay_main_io.addLayout(grid_di)
        lay_main_io.addWidget(QLabel("DO (outputs):"))
        grid_do = QGridLayout()
        self._main_do_leds = []
        self._main_do_buttons = []
        for i in range(4):
            led = LedIndicator(alarm_mode=False)
            btn = QPushButton("ON/OFF")
            btn.setEnabled(MAIN_IO_ENABLED)
            btn.setToolTip("Write mapping not assigned yet" if not MAIN_IO_ENABLED else "")
            self._main_do_leds.append(led)
            self._main_do_buttons.append(btn)
            grid_do.addWidget(led, i // 2, (i % 2) * 3)
            grid_do.addWidget(QLabel(main_do_labels[i]), i // 2, (i % 2) * 3 + 1)
            grid_do.addWidget(btn, i // 2, (i % 2) * 3 + 2)
        lay_main_io.addLayout(grid_do)
        left_col.addWidget(card_main_io)

        # Card A: ON/OFF 1~16
        card_a, lay_a = card_frame("ON/OFF 1~16")
        grid_a = QGridLayout()
        self._onoff_leds = []
        self._onoff_labels = []
        for i in range(16):
            led = LedIndicator(alarm_mode=False)
            lbl = QLabel(f"{i+1} {self.ONOFF_LABELS[i]}")
            lbl.setStyleSheet("color: #e0e0e0;")
            row, col = i // 4, i % 4
            grid_a.addWidget(led, row, col * 2)
            grid_a.addWidget(lbl, row, col * 2 + 1)
            self._onoff_leds.append(led)
            self._onoff_labels.append(lbl)
        lay_a.addLayout(grid_a)
        btn_read_onoff = QPushButton("Read once")
        btn_read_onoff.clicked.connect(self._read_onoff_once)
        lay_a.addWidget(btn_read_onoff)
        self._read_buttons = [btn_read_onoff]
        left_col.addWidget(card_a)

        # Card B: Door sensors
        card_b, lay_b = card_frame("Door Sensors")
        grid_b = QGridLayout()
        self._door_leds = []
        for i, name in enumerate(self.DOOR_LABELS):
            led = LedIndicator(alarm_mode=False)
            lbl = QLabel(name)
            lbl.setStyleSheet("color: #e0e0e0;")
            grid_b.addWidget(led, i // 2, (i % 2) * 2)
            grid_b.addWidget(lbl, i // 2, (i % 2) * 2 + 1)
            self._door_leds.append(led)
        lay_b.addLayout(grid_b)
        btn_door = QPushButton("Read once")
        btn_door.clicked.connect(self._read_door_once)
        lay_b.addWidget(btn_door)
        self._read_buttons.append(btn_door)
        left_col.addWidget(card_b)

        # Card C: Alarms 1~12
        card_c, lay_c = card_frame("Alarms 1~12")
        grid_c = QGridLayout()
        self._alarm_leds = []
        for i in range(12):
            led = LedIndicator(alarm_mode=True)
            lbl = QLabel(f"ALM{i+1}")
            lbl.setStyleSheet("color: #e0e0e0;")
            row, col = i // 3, i % 3
            grid_c.addWidget(led, row, col * 2)
            grid_c.addWidget(lbl, row, col * 2 + 1)
            self._alarm_leds.append(led)
        lay_c.addLayout(grid_c)
        btn_alarms = QPushButton("Read once")
        btn_alarms.clicked.connect(self._read_alarms_once)
        lay_c.addWidget(btn_alarms)
        self._read_buttons.append(btn_alarms)
        left_col.addWidget(card_c)

        left_w = QWidget()
        left_w.setLayout(left_col)
        content_lay.addWidget(left_w, stretch=1)

        # RIGHT column: HPSB, LPSB tabs, Current Monitor
        right_col = QVBoxLayout()
        right_col.setSpacing(12)

        # HPSB (Slave 1) — Ports
        card_hpsb, lay_hpsb = card_frame("HPSB (Slave 1) — Ports")
        hpsb_port_labels = ["Port1: 냉온풍기", "Port2: 온열벤치", "Port3: spare"]
        self._hpsb_leds = []
        self._hpsb_toggles = []
        self._hpsb_current_labels = []
        for i in range(3):
            row = QHBoxLayout()
            led = LedIndicator(alarm_mode=False)
            lbl = QLabel(hpsb_port_labels[i])
            lbl.setStyleSheet("color: #e0e0e0;")
            btn = QPushButton("ON/OFF")
            btn.setEnabled(False)
            btn.setToolTip("Write mapping not assigned yet")
            cur_lbl = QLabel("Raw: —  Min/Max: —")
            cur_lbl.setStyleSheet("color: #888888; font-size: 11px;")
            row.addWidget(led)
            row.addWidget(lbl)
            row.addWidget(btn)
            row.addWidget(cur_lbl)
            lay_hpsb.addLayout(row)
            self._hpsb_leds.append(led)
            self._hpsb_toggles.append(btn)
            self._hpsb_current_labels.append(cur_lbl)
        right_col.addWidget(card_hpsb)

        # LPSB Units (3) — Ports (tabs)
        card_lpsb, lay_lpsb = card_frame("LPSB Units (3) — Ports")
        self._lpsb_tabs = QTabWidget()
        lpsb1_labels = ["P1 내부조명1", "P2 내부조명2", "P3 충전기"]
        lpsb2_labels = ["P1 외부조명1", "P2 외부조명2", "P3 spare"]
        lpsb3_labels = ["P1 spare", "P2 spare", "P3 spare"]
        self._lpsb_leds = []   # [LPSB1_leds, LPSB2_leds, LPSB3_leds]
        self._lpsb_buttons = []
        self._lpsb_current_labels = []
        for board_idx, labels in enumerate([lpsb1_labels, lpsb2_labels, lpsb3_labels]):
            page = QWidget()
            page_lay = QVBoxLayout(page)
            leds = []
            btns = []
            cur_lbls = []
            for i in range(3):
                row = QHBoxLayout()
                led = LedIndicator(alarm_mode=False)
                lbl = QLabel(labels[i])
                lbl.setStyleSheet("color: #e0e0e0;")
                b = QPushButton("ON/OFF")
                b.setToolTip("FC05 coil (VB 8~12)" if (board_idx < 2 and i < (3 if board_idx == 0 else 2)) else "Not mapped")
                vb_idx = (8 + board_idx * 3 + i) if (board_idx == 0 and i < 3) or (board_idx == 1 and i < 2) else None
                if vb_idx is not None:
                    b.clicked.connect(lambda checked=False, x=vb_idx: self._write_vb(x))
                else:
                    b.setEnabled(False)
                cur_lbl = QLabel("Raw: —  Min/Max: —")
                cur_lbl.setStyleSheet("color: #888888; font-size: 11px;")
                row.addWidget(led)
                row.addWidget(lbl)
                row.addWidget(b)
                row.addWidget(cur_lbl)
                page_lay.addLayout(row)
                leds.append(led)
                btns.append(b)
                cur_lbls.append(cur_lbl)
            self._lpsb_leds.append(leds)
            self._lpsb_buttons.append(btns)
            self._lpsb_current_labels.append(cur_lbls)
            self._lpsb_tabs.addTab(page, f"LPSB{board_idx + 1}")
        lay_lpsb.addWidget(self._lpsb_tabs)
        right_col.addWidget(card_lpsb)

        # Current Monitor
        card_cur, lay_cur = card_frame("Current Monitor")
        self._current_table = QTableWidget(14, 3)
        self._current_table.setHorizontalHeaderLabels(["Name", "Raw Value", "Min / Max"])
        self._current_table.horizontalHeader().setStretchLastSection(True)
        for r in range(14):
            self._current_table.setItem(r, 0, QTableWidgetItem(self.CURRENT_NAMES[r]))
            self._current_table.setItem(r, 1, QTableWidgetItem("—"))
            self._current_table.setItem(r, 2, QTableWidgetItem("—"))
            self._current_table.item(r, 1).setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            self._current_table.item(r, 2).setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self._current_table.setColumnWidth(0, 120)
        self._current_table.setColumnWidth(1, 80)
        lay_cur.addWidget(self._current_table)
        btn_cur = QPushButton("Read once")
        btn_cur.clicked.connect(self._read_currents_once)
        lay_cur.addWidget(btn_cur)
        self._read_buttons.append(btn_cur)
        right_col.addWidget(card_cur)

        right_w = QWidget()
        right_w.setLayout(right_col)
        content_lay.addWidget(right_w, stretch=1)

        main_layout.addWidget(content)

        # ---- Bottom: Controls + Log ----
        bottom = QWidget()
        bottom.setMinimumHeight(200)
        bottom_lay = QHBoxLayout(bottom)
        bottom_lay.setSpacing(12)

        # Left: Control Actions (Commands)
        card_ctrl, lay_ctrl = card_frame("Control Actions (Commands)")
        lay_ctrl.addWidget(QLabel("Door actions:"))
        row1 = QHBoxLayout()
        self._btn_door1 = QPushButton("Open Door 1")
        self._btn_door1.setObjectName("primary")
        self._btn_door1.setToolTip("FC05 coil 896 (1x0897)")
        self._btn_door1.clicked.connect(self._write_door1)
        self._btn_door2 = QPushButton("Open Door 2")
        self._btn_door2.setObjectName("primary")
        self._btn_door2.setToolTip("FC05 coil 897 (1x0898)")
        self._btn_door2.clicked.connect(self._write_door2)
        row1.addWidget(self._btn_door1)
        row1.addWidget(self._btn_door2)
        lay_ctrl.addLayout(row1)
        lay_ctrl.addWidget(QLabel("LPSB port toggles (FC05 VB 8~12):"))
        row_vb = QHBoxLayout()
        vb_labels = ["VB8 → LPSB1 P1", "VB9 → LPSB1 P2", "VB10 → LPSB1 P3", "VB11 → LPSB2 P1", "VB12 → LPSB2 P2"]
        self._vb_buttons = []
        for i, label in enumerate(vb_labels):
            b = QPushButton(label)
            b.setToolTip(f"FC05 coil {891 + i} (1x0{892 + i})")
            b.clicked.connect(lambda checked=False, x=8 + i: self._write_vb(x))
            row_vb.addWidget(b)
            self._vb_buttons.append(b)
        lay_ctrl.addLayout(row_vb)
        lay_ctrl.addWidget(QLabel("HPSB port toggles: Not mapped yet."))
        lay_ctrl.addWidget(QLabel("Exception test (expect 0x02):"))
        row2 = QHBoxLayout()
        b899 = QPushButton("0899")
        b899.setObjectName("danger")
        b899.setToolTip("FC05 invalid coil 898 — expect Illegal Data Address")
        b899.clicked.connect(self._write_invalid_899)
        b900 = QPushButton("0900")
        b900.setObjectName("danger")
        b900.setToolTip("FC05 invalid coil 899 — expect Illegal Data Address")
        b900.clicked.connect(self._write_invalid_900)
        row2.addWidget(b899)
        row2.addWidget(b900)
        row2.addStretch()
        lay_ctrl.addLayout(row2)
        help_text = (
            "Open Door 1/2: FC05 write single coil (896, 897). "
            "VB 8~12: FC05 coils 891~895 toggle output. "
            "0899/0900: invalid addresses to verify exception 0x02 (Illegal Data Address)."
        )
        lay_ctrl.addWidget(QLabel(help_text))
        help_lbl = lay_ctrl.itemAt(lay_ctrl.count() - 1).widget()
        help_lbl.setStyleSheet("color: #888888; font-size: 11px;")
        help_lbl.setWordWrap(True)
        self._control_buttons = (
            [self._btn_door1, self._btn_door2] + self._vb_buttons +
            self._hpsb_toggles + self._main_do_buttons +
            list(chain.from_iterable(self._lpsb_buttons)) + [b899, b900]
        )
        bottom_lay.addWidget(card_ctrl, stretch=1)

        # Right: Log card
        card_log, lay_log = card_frame("Log")
        self._log_filter = QLineEdit()
        self._log_filter.setPlaceholderText("Filter (search)...")
        self._log_filter.textChanged.connect(self._apply_log_filter)
        lay_log.addWidget(self._log_filter)
        self._log_edit = QPlainTextEdit()
        self._log_edit.setReadOnly(True)
        self._log_edit.setMinimumHeight(140)
        self._log_edit.setFont(QFont("Consolas", 10))
        lay_log.addWidget(self._log_edit)
        log_btns = QHBoxLayout()
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._log_clear)
        btn_copy = QPushButton("Copy selected")
        btn_copy.clicked.connect(self._log_copy_selected)
        btn_save = QPushButton("Save CSV")
        btn_save.clicked.connect(self._log_save_csv)
        log_btns.addWidget(btn_clear)
        log_btns.addWidget(btn_copy)
        log_btns.addWidget(btn_save)
        log_btns.addStretch()
        lay_log.addLayout(log_btns)
        bottom_lay.addWidget(card_log, stretch=1)

        main_layout.addWidget(bottom)

        self.setCentralWidget(central)
        self.resize(920, 720)
        self.setMinimumSize(880, 700)

        self._log.log_line.connect(self._on_log_line)

    def showEvent(self, event):
        super().showEvent(event)
        QTimer.singleShot(200, self._print_layout_summary)

    def _print_layout_summary(self):
        w, h = self.width(), self.height()
        print("[PC Test Tool] --- Layout verification ---")
        print(f"  Main window size: {w} x {h}")
        print("  Layout tree:")
        print("    - Top bar (Port, Refresh, Baud 9600, Slave ID, Connect/Disconnect, status, Auto poll, Last poll)")
        print("    - Left: MAIN Board I/O, ON/OFF 1~16, Door Sensors, Alarms 1~12")
        print("    - Right: HPSB (Slave 1) Ports, LPSB Units (3) tabs, Current Monitor")
        print("    - Bottom: Control Actions (Commands), Log (filter, Copy selected, Clear, Save CSV)")
        print("[PC Test Tool] ------------------------------")

    def _on_log_line(self, line: str):
        self._log_lines.append(line)
        self._apply_log_filter()

    def _apply_log_filter(self):
        filt = self._log_filter.text().strip().lower()
        if not filt:
            self._log_edit.setPlainText("\n".join(self._log_lines))
            return
        self._log_edit.setPlainText("\n".join(l for l in self._log_lines if filt in l.lower()))

    def _log_clear(self):
        self._log.clear()
        self._log_lines.clear()
        self._log_edit.clear()
        self._log_filter.clear()

    def _log_save_csv(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save log", "", "CSV (*.csv)")
        if not path:
            return
        from pathlib import Path
        content = self._log.get_csv_content()
        if not content:
            return
        Path(path).write_text(content, encoding="utf-8")

    def _log_copy_selected(self):
        text = self._log_edit.textCursor().selectedText()
        if text:
            QApplication.clipboard().setText(text)

    def _set_connected_ui(self, connected: bool):
        self._btn_connect.setEnabled(not connected)
        self._btn_disconnect.setEnabled(connected)
        self._port_combo.setEnabled(not connected)
        self._slave_id.setEnabled(not connected)
        for b in self._read_buttons:
            b.setEnabled(connected)
        for b in self._control_buttons:
            b.setEnabled(connected)
        if connected:
            self._status_badge.setText("Connected")
            self._status_badge.setStyleSheet("color: #00c853; font-weight: bold; padding: 4px 8px;")
        else:
            self._status_badge.setText("Disconnected")
            self._status_badge.setStyleSheet("color: #555555; font-weight: bold; padding: 4px 8px;")

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
        ok, msg = self._client.connect(port, baudrate=self._baud.value(), slave_id=self._slave_id.value())
        if ok:
            self._set_connected_ui(True)
            self._worker.set_running(True)
            self._current_min = [None] * 14
            self._current_max = [None] * 14
            self._log.log("Connect", port, self._baud.value(), "OK")
        else:
            QMessageBox.warning(self, "Connection", msg)
            self._log.log("Connect", port, self._baud.value(), "Fail", msg)

    def _do_disconnect(self):
        self._worker.set_running(False)
        self._poll_timer.stop()
        self._auto_poll.setChecked(False)
        self._client.disconnect()
        self._set_connected_ui(False)
        self._log.log("Disconnect", "", "", "OK")

    def _on_auto_poll_changed(self, state):
        if state:
            self._worker.set_running(self._client.connected)
            self._poll_timer.start(self._poll_interval.value())
        else:
            self._poll_timer.stop()

    def _set_poll_interval(self, ms: int):
        if self._poll_timer.isActive():
            self._poll_timer.start(ms)

    def _on_timer(self):
        self.request_poll.emit()

    def _update_last_poll(self):
        self._last_poll_ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self._last_poll_label.setText(f"Last poll: {self._last_poll_ts}")

    def _read_onoff_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_onoff()
        self._log.log("FC02", ONOFF_START, ONOFF_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_onoff(ok, data, err)
        self._update_last_poll()

    def _read_door_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_door_sensors()
        self._log.log("FC02", DOOR_START, DOOR_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_door(ok, data, err)
        self._update_last_poll()

    def _read_alarms_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_alarms()
        self._log.log("FC02", ALARMS_START, ALARMS_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_alarms(ok, data, err)
        self._update_last_poll()

    def _read_currents_once(self):
        if not self._client.connected:
            return
        ok, data, err = self._client.read_currents()
        self._log.log("FC03", CURRENT_START, CURRENT_COUNT, "OK" if ok else "Fail", err or "")
        self._apply_currents(ok, data, err)
        self._update_last_poll()

    def _apply_onoff(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            for i in range(min(16, len(data))):
                self._onoff_leds[i].set_state(bool(data[i]))
            for i in range(len(data), 16):
                self._onoff_leds[i].set_state(False)
            # HPSB (indices 2,3,4) and LPSB (5..13)
            for j in range(3):
                if len(data) > 2 + j:
                    self._hpsb_leds[j].set_state(bool(data[2 + j]))
            for board in range(3):
                for port in range(3):
                    idx = 5 + board * 3 + port
                    if len(data) > idx:
                        self._lpsb_leds[board][port].set_state(bool(data[idx]))
        else:
            for led in self._onoff_leds:
                led.set_state(False)
            for led in self._hpsb_leds:
                led.set_state(False)
            for leds in self._lpsb_leds:
                for led in leds:
                    led.set_state(False)

    def _apply_door(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            for i in range(min(4, len(data))):
                self._door_leds[i].set_state(bool(data[i]))
            for i in range(len(data), 4):
                self._door_leds[i].set_state(False)
        else:
            for led in self._door_leds:
                led.set_state(False)

    def _apply_alarms(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            for i in range(min(12, len(data))):
                self._alarm_leds[i].set_state(bool(data[i]))
            for i in range(len(data), 12):
                self._alarm_leds[i].set_state(False)
        else:
            for led in self._alarm_leds:
                led.set_state(False)

    def _apply_currents(self, ok: bool, data: list | None, err: str | None):
        if ok and data is not None:
            for r in range(min(14, len(data))):
                val = data[r]
                self._current_table.item(r, 1).setText(str(val))
                if isinstance(val, int):
                    if self._current_min[r] is None or val < self._current_min[r]:
                        self._current_min[r] = val
                    if self._current_max[r] is None or val > self._current_max[r]:
                        self._current_max[r] = val
                    mn, mx = self._current_min[r], self._current_max[r]
                    self._current_table.item(r, 2).setText(f"{mn} / {mx}")
                else:
                    self._current_table.item(r, 2).setText("—")
            for r in range(len(data), 14):
                self._current_table.item(r, 1).setText("—")
                self._current_table.item(r, 2).setText("—")
            # HPSB current labels (indices 0,1,2)
            for j in range(3):
                if len(data) > j and isinstance(data[j], int):
                    mn = self._current_min[j] if self._current_min[j] is not None else "—"
                    mx = self._current_max[j] if self._current_max[j] is not None else "—"
                    self._hpsb_current_labels[j].setText(f"Raw: {data[j]}  Min/Max: {mn} / {mx}")
                else:
                    self._hpsb_current_labels[j].setText("Raw: —  Min/Max: —")
            # LPSB current labels (indices 3..11)
            for board in range(3):
                for port in range(3):
                    r = 3 + board * 3 + port
                    if len(data) > r and isinstance(data[r], int):
                        mn = self._current_min[r] if self._current_min[r] is not None else "—"
                        mx = self._current_max[r] if self._current_max[r] is not None else "—"
                        self._lpsb_current_labels[board][port].setText(f"Raw: {data[r]}  Min/Max: {mn} / {mx}")
                    else:
                        self._lpsb_current_labels[board][port].setText("Raw: —  Min/Max: —")
        else:
            for r in range(14):
                self._current_table.item(r, 1).setText(err or "—")
                self._current_table.item(r, 2).setText("—")
            for lbl in self._hpsb_current_labels:
                lbl.setText("Raw: —  Min/Max: —")
            for board_lbls in self._lpsb_current_labels:
                for lbl in board_lbls:
                    lbl.setText("Raw: —  Min/Max: —")

    def _on_onoff(self, ok: bool, data, err):
        self._apply_onoff(ok, data, err)
        self._update_last_poll()

    def _on_door(self, ok: bool, data, err):
        self._apply_door(ok, data, err)
        self._update_last_poll()

    def _on_alarms(self, ok: bool, data, err):
        self._apply_alarms(ok, data, err)
        self._update_last_poll()

    def _on_cmd_onoff(self, ok: bool, data, err):
        pass  # CMD ON/OFF 1~7 not shown as separate LEDs; same as ON/OFF for display

    def _on_currents(self, ok: bool, data, err):
        self._apply_currents(ok, data, err)
        self._update_last_poll()

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
