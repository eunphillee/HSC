from __future__ import annotations

from dataclasses import dataclass
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QBrush
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QGridLayout,
    QGroupBox,
    QPushButton,
    QLabel,
    QPlainTextEdit,
    QSpinBox,
    QCheckBox,
    QSizePolicy,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
)

from .logger import LogHandler
from .address_map import doc1x_to_offset, MAINBOARD_SLAVE_ID_DEFAULT


@dataclass(frozen=True)
class DocBitPoint:
    doc_addr: int
    name: str
    kind: str  # "DI" or "COIL"


@dataclass(frozen=True)
class DocRegPoint:
    doc_addr: int
    name: str
    kind: str  # "IR"


class DocModbusPanel(QWidget):
    """
    문서 기반(Mainboard only) Modbus 검증 탭.
    - 기존 UI를 대체하지 않고 별도 탭으로 추가되는 패널
    - 요청/응답 결과와 간이 raw(hex) 로그를 제공
    """

    def __init__(self, log: LogHandler, parent=None):
        super().__init__(parent)
        self._log = log
        self._req_fc01 = None
        self._req_fc02 = None
        self._req_fc04 = None
        self._req_fc05 = None
        self._req_fc15 = None
        self._last_fc01 = (1, 0, 0)
        self._last_fc02 = (1, 0, 0)
        self._last_fc04 = (1, 0, 0)
        self._last_fc05 = (1, 0, False)
        self._last_fc15 = (1, 0, [])

        self._raw = QPlainTextEdit()
        self._raw.setReadOnly(True)
        self._raw.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        self._out = QPlainTextEdit()
        self._out.setReadOnly(True)
        self._out.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        self._slave = QSpinBox()
        self._slave.setRange(1, 247)
        self._slave.setValue(MAINBOARD_SLAVE_ID_DEFAULT)
        self._slave.setMinimumWidth(60)
        self._slave.setToolTip(
            "메인보드 EEPROM slave_id와 동일하게. 기본값은 미설정 펌웨어와 맞춘 권장값일 뿐 고정 아님."
        )

        self._pulse_ms = QSpinBox()
        self._pulse_ms.setRange(0, 5000)
        self._pulse_ms.setValue(300)
        self._pulse_ms.setSuffix(" ms")
        self._pulse_ms.setMinimumWidth(90)

        self._coil_doc = QSpinBox()
        self._coil_doc.setRange(1, 9999)
        self._coil_doc.setValue(899)
        self._coil_doc.setMinimumWidth(80)

        self._coil_value = QCheckBox("ON")
        self._coil_value.setChecked(True)

        self._table = QTableWidget(0, 5)
        self._table.setHorizontalHeaderLabels(["문서주소", "이름", "타입", "RW", "값"])
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._table.verticalHeader().setVisible(False)
        self._table.setAlternatingRowColors(True)
        self._table.setMinimumHeight(220)
        self._table.setStyleSheet(
            "QTableWidget {"
            "  background-color: #2b3038;"
            "  alternate-background-color: #323945;"
            "  color: #f1f5f9;"
            "  gridline-color: #566172;"
            "  selection-background-color: #1d4ed8;"
            "  selection-color: #ffffff;"
            "  font-size: 13px;"
            "  border: 1px solid #64748b;"
            "  border-radius: 6px;"
            "}"
            "QHeaderView::section {"
            "  background-color: #475569;"
            "  color: #f8fafc;"
            "  font-weight: 700;"
            "  font-size: 13px;"
            "  padding: 6px;"
            "  border: 1px solid #64748b;"
            "}"
            "QTableWidget::item {"
            "  color: #f8fafc;"
            "  padding: 6px;"
            "}"
        )
        self._raw.setStyleSheet(
            "QPlainTextEdit { background-color: #111827; color: #e5e7eb; border: 1px solid #4b5563; font-size: 12px; padding: 6px; }"
        )
        self._out.setStyleSheet(
            "QPlainTextEdit { background-color: #0f172a; color: #f8fafc; border: 1px solid #64748b; font-size: 13px; padding: 8px; }"
        )
        self._doc_row_map: dict[int, int] = {}
        self._fc04_cards: dict[str, QLabel] = {}
        self._build_ui()
        self._init_points()

    def set_request_callbacks(self, fc01_cb, fc02_cb, fc04_cb, fc05_cb, fc15_cb):
        self._req_fc01 = fc01_cb
        self._req_fc02 = fc02_cb
        self._req_fc04 = fc04_cb
        self._req_fc05 = fc05_cb
        self._req_fc15 = fc15_cb

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(12)

        gb = QGroupBox("H2Tech 문서 기반 Modbus 테스트 (Mainboard only)")
        lay = QVBoxLayout(gb)

        row = QHBoxLayout()
        row.addWidget(QLabel("Slave ID:"))
        row.addWidget(self._slave)
        row.addSpacing(16)
        row.addWidget(QLabel("Pulse:"))
        row.addWidget(self._pulse_ms)
        row.addStretch()
        lay.addLayout(row)

        # ---- buttons ----
        # Unified Rule v1.0 강제: PC는 FC04(read) / FC05(write single coil)만 사용
        row_btn = QHBoxLayout()
        btn_fc02 = QPushButton("FC02 읽기 (disabled)")
        btn_fc02.setEnabled(False)
        btn_fc02.setVisible(False)
        btn_fc01 = QPushButton("FC01 읽기 (disabled)")
        btn_fc01.setEnabled(False)
        btn_fc01.setVisible(False)
        btn_fc04 = QPushButton("FC04 진단 읽기 (4x4000..)")
        row_btn.addWidget(btn_fc02)
        row_btn.addWidget(btn_fc01)
        row_btn.addWidget(btn_fc04)
        lay.addLayout(row_btn)

        row_wr = QHBoxLayout()
        row_wr.addWidget(QLabel("FC05 문서주소(1x):"))
        row_wr.addWidget(self._coil_doc)
        row_wr.addWidget(self._coil_value)
        btn_fc05 = QPushButton("FC05 Write Single Coil")
        btn_fc15 = QPushButton("FC15 Write Multiple Coils(disabled)")
        btn_fc15.setEnabled(False)
        btn_fc15.setVisible(False)
        row_wr.addWidget(btn_fc05)
        row_wr.addWidget(btn_fc15)
        row_wr.addStretch()
        lay.addLayout(row_wr)

        root.addWidget(gb)

        # ---- content area ----
        mid = QHBoxLayout()
        gb_left = QGroupBox("")
        lay_left = QVBoxLayout(gb_left)
        lay_left.setSpacing(10)
        row_left_hdr = QHBoxLayout()
        row_left_hdr.addWidget(QLabel("상태/결과"))
        row_left_hdr.addStretch()
        btn_clear_left = QPushButton("Clear")
        btn_clear_left.setFixedHeight(24)
        btn_clear_left.setFixedWidth(60)
        row_left_hdr.addWidget(btn_clear_left)
        lay_left.addLayout(row_left_hdr)
        table_box = QGroupBox("문서 주소 상태 테이블")
        table_box.setStyleSheet("QGroupBox { background-color: #20262e; border: 1px solid #4b5563; border-radius: 6px; }")
        table_lay = QVBoxLayout(table_box)
        table_lay.setContentsMargins(8, 8, 8, 8)
        table_lay.addWidget(self._table)
        lay_left.addWidget(table_box)

        cards = QGroupBox("FC04 진단 카드")
        cards.setStyleSheet("QGroupBox { background-color: #1b2430; border: 1px solid #4b5563; border-radius: 6px; }")
        cards_lay = QGridLayout(cards)
        cards_lay.setContentsMargins(10, 10, 10, 10)
        cards_lay.setHorizontalSpacing(16)
        cards_lay.setVerticalSpacing(10)
        keys = [
            ("main_status", "Mainboard 상태코드"),
            ("hpsb_online", "HPSB Online"),
            ("lpsb_online", "LPSB Online"),
            ("hpsb_cnt", "HPSB 응답/상태"),
            ("lpsb_cnt", "LPSB 응답/상태"),
            ("adc1", "ADC1"),
            ("adc2", "ADC2"),
            ("adc3", "ADC3"),
            ("alarm", "Alarm Bitmask"),
            ("fw", "FW Ver"),
        ]
        for i, (k, title) in enumerate(keys):
            lbl_t = QLabel(title)
            lbl_t.setStyleSheet("color:#cbd5e1; font-size:12px;")
            lbl_v = QLabel("N/A")
            lbl_v.setStyleSheet(
                "background:#0b1220; color:#f8fafc; border:1px solid #64748b; border-radius:6px; padding:6px; font-weight:700;"
            )
            cards_lay.addWidget(lbl_t, i // 5, (i % 5) * 2)
            cards_lay.addWidget(lbl_v, i // 5, (i % 5) * 2 + 1)
            self._fc04_cards[k] = lbl_v
        lay_left.addWidget(cards)

        out_box = QGroupBox("결과 로그")
        out_box.setStyleSheet("QGroupBox { background-color: #151e2e; border: 1px solid #4b5563; border-radius: 6px; }")
        out_lay = QVBoxLayout(out_box)
        out_lay.setContentsMargins(8, 8, 8, 8)
        out_lay.addWidget(self._out)
        lay_left.addWidget(out_box)
        gb_right = QGroupBox("")
        lay_right = QVBoxLayout(gb_right)
        row_right_hdr = QHBoxLayout()
        row_right_hdr.addWidget(QLabel("Raw frame log (요약)"))
        row_right_hdr.addStretch()
        btn_clear_right = QPushButton("Clear")
        btn_clear_right.setFixedHeight(24)
        btn_clear_right.setFixedWidth(60)
        row_right_hdr.addWidget(btn_clear_right)
        lay_right.addLayout(row_right_hdr)
        lay_right.addWidget(self._raw)
        mid.addWidget(gb_left, 2)
        mid.addWidget(gb_right, 2)
        root.addLayout(mid, 1)

        # signals
        btn_fc02.clicked.connect(self._do_fc02)
        btn_fc01.clicked.connect(self._do_fc01)
        btn_fc04.clicked.connect(self._do_fc04)
        btn_fc05.clicked.connect(self._do_fc05)
        btn_fc15.clicked.connect(self._do_fc15)
        btn_clear_left.clicked.connect(self._clear_left)
        btn_clear_right.clicked.connect(self._clear_right)

    def _init_points(self):
        points = []
        for d in range(821, 837):
            points.append((f"1x{d:04d}", f"ON/OFF 상태 {d-820}", "DI", "R", "N/A"))
        for d in range(853, 861):
            points.append((f"1x{d:04d}", f"자동문 센서/스위치 {d-852}", "DI", "R", "N/A"))
        for d in range(869, 881):
            points.append((f"1x{d:04d}", f"정상/이상 상태 {d-868}", "DI", "R", "N/A"))
        for d in range(885, 892):
            points.append((f"1x{d:04d}", f"CMD ON/OFF {d-884}", "DI", "R", "N/A"))
        for d in range(899, 911):
            points.append((f"1x{d:04d}", f"HPSB/LPSB SSR 쓰기 (doc→coil addr {d - 1})", "COIL", "RW", "N/A"))
        for d in range(4000, 4032):
            points.append((f"4x{d:04d}", f"진단레지스터 {d}", "IR", "R", "N/A"))

        self._table.setRowCount(len(points))
        for r, p in enumerate(points):
            for c, v in enumerate(p):
                it = QTableWidgetItem(str(v))
                # 테이블 글자색은 흰색 계열로 고정
                it.setForeground(QBrush(QColor("#f8fafc")))
                if c == 4:
                    # 값 컬럼 강조
                    it.setBackground(QBrush(QColor("#fde68a")))
                    f = it.font()
                    f.setBold(True)
                    it.setFont(f)
                    it.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self._table.setItem(r, c, it)
            try:
                addr_text = p[0]
                if addr_text.startswith("1x"):
                    self._doc_row_map[int(addr_text[2:])] = r
            except Exception:
                pass
        self._table.verticalHeader().setDefaultSectionSize(30)
        hh = self._table.horizontalHeader()
        hh.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)  # 문서주소
        hh.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)           # 이름
        hh.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)  # 타입
        hh.setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)  # RW
        hh.setSectionResizeMode(4, QHeaderView.ResizeMode.ResizeToContents)  # 값
        # 가독성 조정: 타입/RW/값 컬럼을 조금 더 넓게
        self._table.setColumnWidth(2, 120)  # 타입
        self._table.setColumnWidth(3, 110)  # RW
        self._table.setColumnWidth(4, 110)  # 값

    def _append_raw(self, line: str):
        self._raw.appendPlainText(line)

    def _append_out(self, line: str):
        self._out.appendPlainText(line)

    def _unit(self) -> int:
        return int(self._slave.value())

    def _clear_left(self):
        self._out.clear()
        for row in range(self._table.rowCount()):
            item = self._table.item(row, 4)
            if item:
                item.setText("N/A")
        for lbl in self._fc04_cards.values():
            lbl.setText("N/A")

    def _clear_right(self):
        self._raw.clear()

    @staticmethod
    def _crc16_modbus(data: bytes) -> int:
        crc = 0xFFFF
        for b in data:
            crc ^= b
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc & 0xFFFF

    def _frame_hex(self, payload: bytes) -> str:
        crc = self._crc16_modbus(payload)
        frame = payload + bytes([crc & 0xFF, (crc >> 8) & 0xFF])
        return " ".join(f"{b:02X}" for b in frame)

    def _do_fc02(self):
        self._append_out("[FC02] Unified Rule v1.0: disabled (FC03/FC02 호환 유지하지 않음)")
        return
        # 1차: 1x0821부터 16bit 읽기(예시)
        unit = self._unit()
        start_doc = 821
        count = 32
        start = start_doc - 1  # 현재 FW는 h2_dec = start_addr+1 규칙을 사용 (즉 1x821 -> addr 820)
        self._last_fc02 = (unit, start, count)
        tx = bytes([unit, 0x02, (start >> 8) & 0xFF, start & 0xFF, (count >> 8) & 0xFF, count & 0xFF])
        self._append_raw(f"TX FC02 unit={unit} addr={start} (doc 1x{start_doc}) cnt={count}")
        self._append_raw(f"TX HEX+CRC: {self._frame_hex(tx)}")
        if self._req_fc02:
            self._req_fc02(unit, start, count)
        else:
            self._append_out("[FC02] FAIL: request callback not set")

    def _do_fc01(self):
        self._append_out("[FC01] Unified Rule v1.0: disabled (FC03/FC02 호환 유지하지 않음)")
        return
        unit = self._unit()
        start_doc = 892
        count = 24
        start = start_doc - 1
        self._last_fc01 = (unit, start, count)
        tx = bytes([unit, 0x01, (start >> 8) & 0xFF, start & 0xFF, (count >> 8) & 0xFF, count & 0xFF])
        self._append_raw(f"TX FC01 unit={unit} addr={start} (doc 1x{start_doc}) cnt={count}")
        self._append_raw(f"TX HEX+CRC: {self._frame_hex(tx)}")
        if self._req_fc01:
            self._req_fc01(unit, start, count)
        else:
            self._append_out("[FC01] FAIL: request callback not set")

    def _do_fc04(self):
        unit = self._unit()
        start_doc = 4000
        count = 12
        start = start_doc
        self._last_fc04 = (unit, start, count)
        tx = bytes([unit, 0x04, (start >> 8) & 0xFF, start & 0xFF, (count >> 8) & 0xFF, count & 0xFF])
        self._append_raw(f"TX FC04 unit={unit} addr={start} (doc 4x{start_doc}) cnt={count}")
        self._append_raw(f"TX HEX+CRC: {self._frame_hex(tx)}")
        if self._req_fc04:
            self._req_fc04(unit, start, count)
        else:
            self._append_out("[FC04] FAIL: request callback not set")

    def _do_fc05(self):
        unit = self._unit()
        doc = int(self._coil_doc.value())
        addr = doc - 1
        value = bool(self._coil_value.isChecked())
        self._last_fc05 = (unit, addr, value)
        v_hi = 0xFF if value else 0x00
        tx = bytes([unit, 0x05, (addr >> 8) & 0xFF, addr & 0xFF, v_hi, 0x00])
        self._append_raw(f"TX FC05 unit={unit} addr={addr} (doc 1x{doc}) val={'ON' if value else 'OFF'}")
        self._append_raw(f"TX HEX+CRC: {self._frame_hex(tx)}")
        if self._req_fc05:
            self._req_fc05(unit, addr, value)
        else:
            self._append_out("[FC05] FAIL: request callback not set")

    def _do_fc15(self):
        self._append_out("[FC15] Unified Rule v1.0: disabled (FC05 write only)")
        return
        unit = self._unit()
        start_doc = 892
        start = start_doc - 1
        # 예시: 8개 coil 패턴(LSB-first 시각화 확인용)
        values = [True, False, True, True, False, False, True, False]
        self._last_fc15 = (unit, start, values[:])
        byte0 = 0
        for i, v in enumerate(values[:8]):
            if v:
                byte0 |= (1 << i)  # LSB-first packing
        self._append_raw(f"TX FC15 unit={unit} addr={start} (doc 1x{start_doc}) cnt={len(values)}")
        self._append_raw(f"LSB-first packing: values={values} -> byte0=0x{byte0:02X} ({byte0:08b})")
        tx = bytes([unit, 0x0F, (start >> 8) & 0xFF, start & 0xFF, 0x00, len(values), 0x01, byte0 & 0xFF])
        self._append_raw(f"TX HEX+CRC: {self._frame_hex(tx)}")
        if self._req_fc15:
            self._req_fc15(unit, start, values)
        else:
            self._append_out("[FC15] FAIL: request callback not set")

    # ---- results from worker thread ----
    def on_fc02_result(self, ok: bool, bits, err):
        if not ok or bits is None:
            self._append_out(f"[FC02] FAIL: {err}")
            return
        start_doc = 821
        self._append_out(f"[FC02] OK: doc 1x{start_doc}.. len={len(bits)}")
        for i, b in enumerate(bits[:32]):
            self._append_out(f"  1x{start_doc + i:04d} = {'ON' if b else 'OFF'}")
        unit, _, count = self._last_fc02
        byte_count = (count + 7) // 8
        data = bytearray(byte_count)
        for i, b in enumerate(bits[:count]):
            if b:
                data[i // 8] |= (1 << (i % 8))
        rx = bytes([unit, 0x02, byte_count]) + bytes(data)
        self._append_raw(f"RX HEX+CRC: {self._frame_hex(rx)}")
        for i, b in enumerate(bits[:count]):
            doc = start_doc + i
            row = self._doc_row_map.get(doc)
            if row is not None:
                self._table.item(row, 4).setText("ON" if b else "OFF")

    def on_fc01_result(self, ok: bool, bits, err):
        if not ok or bits is None:
            self._append_out(f"[FC01] FAIL: {err}")
            return
        start_doc = 892
        self._append_out(f"[FC01] OK: doc 1x{start_doc}.. len={len(bits)}")
        for i, b in enumerate(bits):
            self._append_out(f"  1x{start_doc + i:04d} = {'ON' if b else 'OFF'}")
        unit, _, count = self._last_fc01
        byte_count = (count + 7) // 8
        data = bytearray(byte_count)
        for i, b in enumerate(bits[:count]):
            if b:
                data[i // 8] |= (1 << (i % 8))
        rx = bytes([unit, 0x01, byte_count]) + bytes(data)
        self._append_raw(f"RX HEX+CRC: {self._frame_hex(rx)}")
        for i, b in enumerate(bits[:count]):
            doc = start_doc + i
            row = self._doc_row_map.get(doc)
            if row is not None:
                self._table.item(row, 4).setText("ON" if b else "OFF")

    def on_fc04_result(self, ok: bool, regs, err):
        if not ok or regs is None:
            self._append_out(f"[FC04] FAIL: {err}")
            return
        start_doc = 4000
        self._append_out(f"[FC04] OK: 4x{start_doc}.. count={len(regs)}")
        for i, r in enumerate(regs):
            self._append_out(f"  4x{start_doc + i:04d} = {r} (0x{r:04X})")
        unit, _, count = self._last_fc04
        data = bytearray()
        for r in regs[:count]:
            data.extend([(r >> 8) & 0xFF, r & 0xFF])
        rx = bytes([unit, 0x04, len(data)]) + bytes(data)
        self._append_raw(f"RX HEX+CRC: {self._frame_hex(rx)}")
        # cards
        map_idx = {
            "main_status": 0,
            "hpsb_online": 1,
            "lpsb_online": 2,
            "hpsb_cnt": 3,
            "lpsb_cnt": 4,
            "adc1": 5,
            "adc2": 6,
            "adc3": 7,
            "alarm": 8,
            "fw": 9,
        }
        for key, idx in map_idx.items():
            if key in self._fc04_cards and 0 <= idx < len(regs):
                self._fc04_cards[key].setText(str(regs[idx]))
        # online/offline 색상 강조
        if "hpsb_online" in self._fc04_cards:
            if 1 < len(regs):
                self._fc04_cards["hpsb_online"].setStyleSheet(
                    "background:#14532d; color:#dcfce7; border:1px solid #16a34a; border-radius:6px; padding:6px; font-weight:700;"
                    if regs[1] else
                    "background:#7f1d1d; color:#fee2e2; border:1px solid #dc2626; border-radius:6px; padding:6px; font-weight:700;"
                )
        if "lpsb_online" in self._fc04_cards:
            if 2 < len(regs):
                self._fc04_cards["lpsb_online"].setStyleSheet(
                    "background:#14532d; color:#dcfce7; border:1px solid #16a34a; border-radius:6px; padding:6px; font-weight:700;"
                    if regs[2] else
                    "background:#7f1d1d; color:#fee2e2; border:1px solid #dc2626; border-radius:6px; padding:6px; font-weight:700;"
                )

    def on_fc05_result(self, ok: bool, err):
        if not ok:
            self._append_out(f"[FC05] FAIL: {err}")
            return
        doc = int(self._coil_doc.value())
        value = bool(self._coil_value.isChecked())
        self._append_out(f"[FC05] OK: 1x{doc} -> {'ON' if value else 'OFF'}")
        row = self._doc_row_map.get(doc)
        if row is not None:
            self._table.item(row, 4).setText("ON" if value else "OFF")
        unit, addr, v = self._last_fc05
        v_hi = 0xFF if v else 0x00
        rx = bytes([unit, 0x05, (addr >> 8) & 0xFF, addr & 0xFF, v_hi, 0x00])
        self._append_raw(f"RX HEX+CRC: {self._frame_hex(rx)}")

    def on_fc15_result(self, ok: bool, err):
        if not ok:
            self._append_out(f"[FC15] FAIL: {err}")
            return
        start_doc = 892
        self._append_out(f"[FC15] OK: 1x{start_doc}..{start_doc + 7} 쓰기 완료")
        unit, start, values = self._last_fc15
        for i, v in enumerate(values):
            doc = start_doc + i
            row = self._doc_row_map.get(doc)
            if row is not None:
                self._table.item(row, 4).setText("ON" if v else "OFF")
        rx = bytes([unit, 0x0F, (start >> 8) & 0xFF, start & 0xFF, 0x00, len(values)])
        self._append_raw(f"RX HEX+CRC: {self._frame_hex(rx)}")

