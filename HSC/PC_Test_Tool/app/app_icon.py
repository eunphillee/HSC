
from pathlib import Path

from PyQt6.QtGui import QColor, QIcon, QPainter, QPixmap

_LOGO_PATH = Path(__file__).resolve().parent / "guro_logo.png"
# 아이콘 캔버스 가로: 원본 로고 좌·우에 각각 더할 투명 픽셀
_PAD_LR = 20


def load_app_icon() -> QIcon:
    if not _LOGO_PATH.is_file():
        return QIcon()
    src = QPixmap(str(_LOGO_PATH))
    if src.isNull():
        return QIcon()
    w, h = src.width(), src.height()
    if w <= 0 or h <= 0:
        return QIcon()

    out = QPixmap(w + 2 * _PAD_LR, h)
    out.fill(QColor(0, 0, 0, 0))

    painter = QPainter(out)
    painter.drawPixmap(_PAD_LR, 0, src)
    painter.end()

    return QIcon(out)
