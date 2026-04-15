"""
Log: timestamp, function, addr, count/value, result, exception. Save to CSV.
Exception decode: 0x01 Illegal Function, 0x02 Illegal Data Address, 0x03 Illegal Data Value.
"""
import csv
import io
from datetime import datetime
from pathlib import Path

from PyQt6.QtWidgets import QPlainTextEdit, QPushButton, QVBoxLayout, QWidget, QFileDialog
from PyQt6.QtCore import QObject, pyqtSignal

EXCEPTION_DECODE = {
    "0x01": "Illegal Function",
    "0x02": "Illegal Data Address",
    "0x03": "Illegal Data Value",
}


def _ts() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def decode_exception(exception: str) -> str:
    """Append human-readable decode for Modbus exception codes."""
    if not exception:
        return ""
    for code, text in EXCEPTION_DECODE.items():
        if code in exception:
            return f" | exc={exception} — {text}"
    return f" | exc={exception}"


class LogHandler(QObject):
    """Emits log lines for the UI; buffers for CSV save."""
    log_line = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._lines: list[dict] = []
        self._raw_rx_buffer: list[int] = []  # \r\n 올 때까지 모아서 한 줄로 출력
        self._raw_line_only: bool = False  # True면 <MSG> 무시, \r\n(또는 \n) 전까지 누적 후 한 줄씩만 출력 (긴 문자열 chunk 수신용)

    def log(self, func: str, addr: int | str, count_or_value: int | str, result: str, exception: str = ""):
        """Legacy log without tag (emits with no tag prefix). Prefer log_tagged for [MAIN]/[HPSB]/[LPSB*]."""
        self.log_tagged("", func, addr, count_or_value, result, exception)

    def log_tagged(self, tag: str, func: str, addr: int | str, count_or_value: int | str, result: str, exception: str = ""):
        """Log with board tag prefix: [MAIN], [HPSB], [LPSB1], [LPSB2], [LPSB3]. tag can be empty."""
        ts = _ts()
        addr_s = str(addr)
        cov_s = str(count_or_value)
        prefix = f"{tag} " if tag else ""
        row = {
            "timestamp": ts,
            "tag": tag,
            "function": func,
            "address": addr_s,
            "count_or_value": cov_s,
            "result": result,
            "exception": exception,
        }
        self._lines.append(row)
        line = f"{ts} | {prefix}{func} | addr={addr_s} count/value={cov_s} | {result}"
        if exception:
            line += decode_exception(exception)
        self.log_line.emit(line)

    def log_info(self, message: str):
        """Single-line info (e.g. startup: pymodbus version, client type)."""
        self.log_line.emit(f"{_ts()} | {message}")

    def log_request(self, tag: str, unit: int, func: str, addr: int | str, count_or_value: int | str):
        """Log outgoing request. tag: [MAIN], [HPSB], [LPSB1] etc. unit = Modbus slave ID (mainboard = EEPROM value, often 9)."""
        addr_s = str(addr)
        cov_s = str(count_or_value)
        prefix = f"{tag} " if tag else ""
        if func in ("FC02", "FC03", "FC04"):
            line = f"{_ts()} | {prefix}TX {func} addr={addr_s} cnt={cov_s} unit={unit}"
        else:
            line = f"{_ts()} | {prefix}TX {func} addr={addr_s} val={cov_s} unit={unit}"
        self.log_line.emit(line)

    def log_response(self, tag: str, ok: bool, exception_code: int | None):
        """Log response. tag: [MAIN], [HPSB], [LPSB1] etc."""
        prefix = f"{tag} " if tag else ""
        if ok:
            self.log_line.emit(f"{_ts()} | {prefix}RX OK")
        elif exception_code is not None:
            self.log_line.emit(f"{_ts()} | {prefix}RX EXC 0x{exception_code:02X}")
        else:
            self.log_line.emit(f"{_ts()} | {prefix}RX ERR")

    def set_raw_line_only(self, on: bool):
        """True면 파서 없이 \\r\\n(또는 \\n) 전까지 누적 후 한 줄씩만 출력. 긴 문자열/스테이지 수신 시 사용."""
        self._raw_line_only = on

    def log_raw_rx(self, bytes_list: list[int]):
        """수신 버퍼에 누적. raw_line_only면 \\r\\n/\\n 기준으로만 한 줄 출력. 아니면 <MSG>~\\r\\n 또는 폴백."""
        if not bytes_list:
            return
        self._raw_rx_buffer.extend(bytes_list)
        _MARKER = b"<MSG>"
        _END_CRLF = b"\r\n"
        _END_LF = b"\n"

        def emit_line(msg_bytes: list) -> None:
            try:
                text = bytes(msg_bytes).decode("ascii", errors="replace").strip()
                if text:
                    self.log_line.emit(f"{_ts()} | [RX] {text}")
            except Exception:
                pass

        def find_line_end(bbuf: bytes):
            end_crlf = bbuf.find(_END_CRLF)
            end_lf = bbuf.find(_END_LF)
            pos_crlf = end_crlf if end_crlf >= 0 else 99999
            pos_lf = end_lf if end_lf >= 0 else 99999
            end = min(pos_crlf, pos_lf)
            end_len = 2 if end == end_crlf else 1
            return end, end_len

        if self._raw_line_only:
            while True:
                bbuf = bytes(self._raw_rx_buffer)
                end, end_len = find_line_end(bbuf)
                if end >= 99999:
                    if len(self._raw_rx_buffer) > 2048:
                        self._raw_rx_buffer = self._raw_rx_buffer[-1024:]
                    break
                msg_bytes = self._raw_rx_buffer[:end]
                self._raw_rx_buffer = self._raw_rx_buffer[end + end_len :]
                emit_line(msg_bytes)
            return

        while True:
            bbuf = bytes(self._raw_rx_buffer)
            start = bbuf.find(_MARKER)
            if start < 0:
                # <MSG> 없음: \\r\\n 또는 \\n 으로 끝나는 줄이 있으면 폴백으로 출력 (수신 여부 확인)
                end_crlf = bbuf.find(_END_CRLF)
                end_lf = bbuf.find(_END_LF)
                pos_crlf = end_crlf if end_crlf >= 0 else 99999
                pos_lf = end_lf if end_lf >= 0 else 99999
                end = min(pos_crlf, pos_lf)
                end_len = 2 if end == end_crlf else 1
                if end < 99999:
                    msg_bytes = self._raw_rx_buffer[:end]
                    self._raw_rx_buffer = self._raw_rx_buffer[end + end_len :]
                    emit_line(msg_bytes)
                    continue
                # 줄 끝 없음: 마커 잘림 대비 끝 5바이트만 유지
                if len(self._raw_rx_buffer) > 5:
                    self._raw_rx_buffer = self._raw_rx_buffer[-5:]
                break
            if start > 0:
                self._raw_rx_buffer = self._raw_rx_buffer[start:]
                bbuf = bytes(self._raw_rx_buffer)
            end_crlf = bbuf.find(_END_CRLF)
            end_lf = bbuf.find(_END_LF)
            pos_crlf = end_crlf if end_crlf >= 0 else 99999
            pos_lf = end_lf if end_lf >= 0 else 99999
            end = min(pos_crlf, pos_lf)
            end_len = 2 if end == end_crlf else 1
            if end >= 99999:
                if len(self._raw_rx_buffer) > 1024:
                    self._raw_rx_buffer = self._raw_rx_buffer[:500]
                break
            msg_bytes = self._raw_rx_buffer[:end]
            self._raw_rx_buffer = self._raw_rx_buffer[end + end_len :]
            emit_line(msg_bytes)
        if len(self._raw_rx_buffer) > 1024:
            self._raw_rx_buffer.clear()

    def get_csv_content(self) -> str:
        if not self._lines:
            return ""
        out = io.StringIO()
        fieldnames = ["timestamp", "tag", "function", "address", "count_or_value", "result", "exception"]
        w = csv.DictWriter(out, fieldnames=fieldnames, extrasaction="ignore")
        w.writeheader()
        for row in self._lines:
            w.writerow({k: row.get(k, "") for k in fieldnames})
        return out.getvalue()

    def clear(self):
        self._lines.clear()
        self._raw_rx_buffer.clear()


class LogPanel(QWidget):
    def __init__(self, handler: LogHandler):
        super().__init__()
        self._handler = handler
        layout = QVBoxLayout(self)
        self._edit = QPlainTextEdit()
        self._edit.setReadOnly(True)
        self._edit.setMinimumHeight(120)
        layout.addWidget(self._edit)
        self._btn_save = QPushButton("Save to CSV")
        self._btn_save.clicked.connect(self._save_csv)
        layout.addWidget(self._btn_save)
        self._handler.log_line.connect(self._append)

    def _append(self, line: str):
        self._edit.appendPlainText(line)

    def _save_csv(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save log", "", "CSV (*.csv)")
        if not path:
            return
        content = self._handler.get_csv_content()
        if not content:
            return
        Path(path).write_text(content, encoding="utf-8")

    def clear(self):
        self._edit.clear()
        self._handler.clear()
