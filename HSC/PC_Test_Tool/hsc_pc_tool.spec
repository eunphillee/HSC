# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec — Windows에서 실행: pyinstaller hsc_pc_tool.spec
# 산출물: dist/HSC_PC_Test_Tool.exe (단일 파일, 콘솔 없음)
#
# 주의: .exe는 Windows PC에서 빌드해야 합니다 (크로스 빌드 불가).

import os

# 스펙 파일 기준 경로
_spec_dir = os.path.dirname(os.path.abspath(SPEC))

# 선택: app/guro_logo.png 가 있으면 번들
_datas = []
_logo = os.path.join(_spec_dir, "app", "guro_logo.png")
if os.path.isfile(_logo):
    _datas.append((_logo, "app"))

a = Analysis(
    [os.path.join(_spec_dir, "main.py")],
    pathex=[_spec_dir],
    binaries=[],
    datas=_datas,
    hiddenimports=[
        "pymodbus.client.sync",
        "pymodbus.framer.rtu_framer",
        "serial",
        "serial.tools.list_ports",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name="HSC_PC_Test_Tool",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
)
