# -*- coding: utf-8 -*-
from pathlib import Path
p = Path(__file__).parent / "app" / "ui_main.py"
t = p.read_text(encoding="utf-8")
t = t.replace("\uc7a0\uc2dc \uc5c5\uc81c(", "\uc7a0\uc2dc \uc5b5\uc81c(")
p.write_text(t, encoding="utf-8")
print("ok")
