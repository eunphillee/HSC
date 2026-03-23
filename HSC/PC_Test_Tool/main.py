#!/usr/bin/env python3
"""HSC PC Test Tool — MAIN board Modbus RTU (H2TECH)."""
import sys

from PyQt6.QtWidgets import QApplication

from app.app_icon import load_app_icon
from app.ui_main import MainWindow


def main():
    app = QApplication(sys.argv)
    icon = load_app_icon()
    if not icon.isNull():
        app.setWindowIcon(icon)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
