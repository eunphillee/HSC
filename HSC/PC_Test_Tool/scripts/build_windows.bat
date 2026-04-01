@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0.."

echo [1/3] venv + 패키지 설치 (처음 한 번)
if not exist ".venv\Scripts\python.exe" (
  python -m venv .venv
)
call .venv\Scripts\activate.bat
pip install -q -r requirements.txt
pip install -q -r requirements-build.txt

echo.
echo [2/3] PyInstaller로 HSC_PC_Test_Tool.exe 빌드
pyinstaller --noconfirm hsc_pc_tool.spec
if errorlevel 1 (
  echo 빌드 실패.
  exit /b 1
)

echo.
echo [3/3] 완료: dist\HSC_PC_Test_Tool.exe
echo.
endlocal
