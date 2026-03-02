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

    def log(self, func: str, addr: int | str, count_or_value: int | str, result: str, exception: str = ""):
        ts = _ts()
        addr_s = str(addr)
        cov_s = str(count_or_value)
        row = {
            "timestamp": ts,
            "function": func,
            "address": addr_s,
            "count_or_value": cov_s,
            "result": result,
            "exception": exception,
        }
        self._lines.append(row)
        line = f"{ts} | {func} | addr={addr_s} count/value={cov_s} | {result}"
        if exception:
            line += decode_exception(exception)
        self.log_line.emit(line)

    def log_info(self, message: str):
        """Single-line info (e.g. startup: pymodbus version, client type)."""
        self.log_line.emit(f"{_ts()} | {message}")

    def log_request(self, unit: int, func: str, addr: int | str, count_or_value: int | str):
        """Log outgoing request as TX FCxx addr=... cnt=... or val=... (read vs write by func)."""
        addr_s = str(addr)
        cov_s = str(count_or_value)
        # FC02/03/04 = read -> cnt= ; FC05/06/15/16 = write -> val=
        if func in ("FC02", "FC03", "FC04"):
            line = f"{_ts()} | TX {func} addr={addr_s} cnt={cov_s}"
        else:
            line = f"{_ts()} | TX {func} addr={addr_s} val={cov_s}"
        self.log_line.emit(line)

    def log_response(self, ok: bool, exception_code: int | None):
        """Log response: RX OK or RX EXC 0xNN."""
        if ok:
            self.log_line.emit(f"{_ts()} | RX OK")
        elif exception_code is not None:
            self.log_line.emit(f"{_ts()} | RX EXC 0x{exception_code:02X}")
        else:
            self.log_line.emit(f"{_ts()} | RX ERR")

    def log_raw_rx(self, bytes_list: list[int]):
        """Log raw bytes received from board (e.g. 0xAA)."""
        if not bytes_list:
            return
        for b in bytes_list:
            self.log_line.emit(f"{_ts()} | RX from board: 0x{b:02X}")

    def get_csv_content(self) -> str:
        if not self._lines:
            return ""
        out = io.StringIO()
        w = csv.DictWriter(out, fieldnames=["timestamp", "function", "address", "count_or_value", "result", "exception"])
        w.writeheader()
        w.writerows(self._lines)
        return out.getvalue()

    def clear(self):
        self._lines.clear()


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
