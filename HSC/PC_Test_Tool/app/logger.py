"""
Log window: timestamp, function, addr, count/value, result, exception. Save to CSV.
"""
import csv
import io
from datetime import datetime
from pathlib import Path

from PyQt6.QtWidgets import QPlainTextEdit, QPushButton, QVBoxLayout, QWidget, QFileDialog
from PyQt6.QtCore import QObject, pyqtSignal


class LogHandler(QObject):
    """Emits log lines for the UI; buffers for CSV save."""
    log_line = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._lines: list[dict] = []

    def log(self, func: str, addr: int | str, count_or_value: int | str, result: str, exception: str = ""):
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
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
            line += f" | exc={exception}"
        self.log_line.emit(line)

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
