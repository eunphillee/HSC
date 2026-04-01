@echo off
REM HSC PC Test Tool — Windows exe 빌드
REM 더블 클릭 시에도 창이 유지되도록 끝에 pause 가 있습니다.

chcp 65001 >nul
setlocal
title HSC PC Test Tool — Windows 빌드

cd /d "%~dp0.."
if not exist "main.py" (
  echo.
  echo [오류] main.py 를 찾을 수 없습니다.
  echo        이 파일은 반드시 다음 경로에 있어야 합니다:
  echo        ...\HSC\PC_Test_Tool\scripts\build_windows.bat
  echo        (압축만 풀고 scripts 폴더 구조가 깨지지 않았는지 확인하세요.)
  echo.
  pause
  exit /b 1
)

set "VENV_PY=%CD%\.venv\Scripts\python.exe"

REM 가상환경 없으면 생성 (py -3 우선, 실패 시 python)
if not exist "%VENV_PY%" (
  echo.
  echo [1/4] 가상환경 .venv 생성 중...
  py -3 -m venv .venv 2>nul
  if errorlevel 1 (
    python -m venv .venv
    if errorlevel 1 (
      echo.
      echo [오류] Python 을 실행할 수 없습니다.
      echo.
      echo  다음 중 하나를 해 주세요:
      echo   1^) https://www.python.org/downloads/ 에서 Python 3.11 이상 설치
      echo      설치 시 하단 "Add python.exe to PATH" 체크
      echo   2^) 또는 Microsoft Store / 설치 관리자용 "Python Launcher ^(py^)" 사용 가능한지 확인
      echo.
      echo  명령 프롬프트에서 아래를 입력해 버전이 나오는지 테스트하세요:
      echo    py -3 --version
      echo    python --version
      echo.
      pause
      exit /b 1
    )
  )
)

if not exist "%VENV_PY%" (
  echo [오류] .venv\Scripts\python.exe 가 없습니다. .venv 폴더를 지우고 다시 실행해 보세요.
  pause
  exit /b 1
)

echo.
echo [2/4] pip 패키지 설치 ^(처음은 1~2분 걸릴 수 있음^)...
"%VENV_PY%" -m pip install --upgrade pip
if errorlevel 1 (
  echo [오류] pip 업그레이드 실패
  pause
  exit /b 1
)
"%VENV_PY%" -m pip install -r requirements.txt
if errorlevel 1 (
  echo [오류] requirements.txt 설치 실패
  pause
  exit /b 1
)
"%VENV_PY%" -m pip install -r requirements-build.txt
if errorlevel 1 (
  echo [오류] requirements-build.txt ^(PyInstaller^) 설치 실패
  pause
  exit /b 1
)

echo.
echo [3/4] PyInstaller 빌드 중...
"%VENV_PY%" -m PyInstaller --noconfirm hsc_pc_tool.spec
if errorlevel 1 (
  echo.
  echo [오류] PyInstaller 빌드 실패. 위쪽 빨간/오류 메시지를 확인하세요.
  pause
  exit /b 1
)

echo.
echo [4/4] 완료.
if exist "dist\HSC_PC_Test_Tool.exe" (
  echo   결과 파일: %CD%\dist\HSC_PC_Test_Tool.exe
  dir "dist\HSC_PC_Test_Tool.exe"
) else (
  echo [경고] dist\HSC_PC_Test_Tool.exe 가 없습니다. build 폴더 로그를 확인하세요.
)
echo.
pause
endlocal
