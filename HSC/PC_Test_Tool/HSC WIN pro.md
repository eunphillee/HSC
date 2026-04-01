# HSC PC Test Tool — Windows 배포 가이드 (Pro)

이 문서는 **HSC PC Test Tool**을 Windows 사용자에게 안전하게 넘기기 위한 절차를 정리한 것입니다.  
배포 담당자(빌드)와 최종 사용자(실행) 모두를 위해 구분해 두었습니다.

---

## 1. 배포 구조 요약

| 구분 | 내용 |
|------|------|
| 프로그램 형태 | PyInstaller로 만든 **단일 실행 파일** `HSC_PC_Test_Tool.exe` 권장 |
| 최종 사용자 PC | **Python 설치 불필요** (exe만 배포하는 경우) |
| 통신 | USB–RS485 등 **시리얼(COM)** 경로로 메인보드와 Modbus RTU |
| 빌드 환경 | **Windows PC에서만** `.exe` 생성 가능 (macOS/Linux에서 Windows용 exe 크로스 빌드 불가) |

---

## 2. 배포 담당자(빌드) — 할 일

### 2.1 사전 준비

- Windows 10/11, **Python 3.11 이상** (64비트 권장)
- 저장소에서 `HSC/PC_Test_Tool` 폴더 전체 확보
- (선택) 앱 아이콘용 `app/guro_logo.png` — 있으면 exe에 포함됨 (`hsc_pc_tool.spec` 참고)

### 2.2 실행 파일 만들기

1. 명령 프롬프트 또는 PowerShell에서 프로젝트 폴더로 이동:
   ```text
   cd HSC\PC_Test_Tool
   ```
2. 아래 중 하나로 빌드:
   - **자동:** `scripts\build_windows.bat` 더블클릭 또는 실행  
   - **수동:**
     ```bat
     python -m venv .venv
     .venv\Scripts\activate
     pip install -r requirements.txt
     pip install -r requirements-build.txt
     pyinstaller --noconfirm hsc_pc_tool.spec
     ```
3. 결과물 위치:
   ```text
   HSC\PC_Test_Tool\dist\HSC_PC_Test_Tool.exe
   ```

### 2.3 배포 전 확인 (권장)

- 같은 PC 또는 다른 Windows PC에서 `HSC_PC_Test_Tool.exe`만 복사해 **더블클릭으로 기동**되는지 확인
- USB–RS485 연결 후 **포트 목록에 COM이 보이는지** 확인
- 메인보드와 **Connect → Read DI** 등 최소 기능 동작 확인

### 2.4 사용자에게 넘길 패키지 구성 (예시)

압축 파일 하나로 묶을 때 권장 구조:

```text
HSC_PC_Test_Tool_Windows_v1.0.0.zip
├── HSC_PC_Test_Tool.exe      ← dist에서 복사한 실행 파일
├── README_사용자.txt         ← 아래 §3 내용을 복사한 간단 안내 (선택)
└── (선택) USB-RS485 드라이버 설치 링크 또는 제조사 PDF
```

- 버전명·날짜는 zip 파일명에 넣는 것이 추적에 유리합니다.  
- **소스 코드·`.venv`·`build`·`dist` 전체 폴더**는 사용자에게 줄 필요 없습니다. **exe만**으로도 충분합니다.

---

## 3. 최종 사용자(Windows) — 설치 및 실행

### 3.1 설치

- Python, pip 설치 **필요 없음** (단일 exe만 배포한 경우)
- 압축을 풀고 **원하는 폴더**에 두면 됩니다.  
  - `Program Files`보다는 **사용자 문서 폴더**나 **바탕화면 하위 폴더**가 권한 문제가 적습니다.

### 3.2 USB–RS485 (또는 COM 포트)

- PC에 장치를 연결한 뒤, **제조사 드라이버**를 설치합니다.  
- 장치 관리자에서 **포트(COM 및 LPT)** 아래에 `COM3`, `COM4` 등으로 보이면 정상입니다.
- 프로그램 상단 **Port** 드롭다운에서 해당 COM을 선택합니다.

### 3.3 프로그램 실행

1. `HSC_PC_Test_Tool.exe` 더블클릭  
2. **Baud**, **Slave ID**(메인보드 기본값 문서와 동일하게, 예: 9) 설정  
3. **Connect** 후 테스트

### 3.4 Windows 보안 알림

- 처음 실행 시 **Windows SmartScreen** 또는 백신이 “알 수 없는 게시자”로 경고할 수 있습니다.  
  - **자세한 정보 → 실행** 또는 회사 정책에 맞게 예외 처리합니다.  
- **코드 서명 인증서**로 exe에 서명하면 신뢰도가 올라갑니다 (배포 조직에서 검토).

---

## 4. 문제 해결 (FAQ)

| 증상 | 확인 사항 |
|------|-----------|
| 포트가 안 보임 | USB 케이블·드라이버, 장치 관리자에서 COM 번호 확인 |
| Connect 후 타임아웃 | Baud(보통 9600), Slave ID, RS485 A/B 배선, 종단 저항 |
| 실행이 느리다 | 첫 실행은 PyInstaller 단일 exe 특성상 약간 느릴 수 있음 |
| 백신이 파일을 막음 | 예외 폴더 등록 또는 IT에 문의 |

---

## 5. 버전·이력 관리 (선택)

배포할 때마다 아래를 내부적으로만 적어두면 혼선이 줄어듭니다.

| 날짜 | Zip 파일명 | Git 커밋 / 비고 |
|------|------------|-----------------|
| YYYY-MM-DD | HSC_PC_Test_Tool_Windows_v… | |

---

## 6. 관련 파일 (저장소 기준)

| 파일 | 설명 |
|------|------|
| `HSC/PC_Test_Tool/hsc_pc_tool.spec` | PyInstaller 설정 |
| `HSC/PC_Test_Tool/scripts/build_windows.bat` | Windows 빌드 스크립트 |
| `HSC/PC_Test_Tool/requirements.txt` | 런타임 의존성 |
| `HSC/PC_Test_Tool/requirements-build.txt` | 빌드용 (pyinstaller) |
| `HSC/PC_Test_Tool/README.md` | 개발자용 설치·실행 |

---

## 7. 요약

1. **배포 담당:** Windows에서 `pyinstaller`로 `dist\HSC_PC_Test_Tool.exe` 생성  
2. **사용자:** exe만 받아서 실행, Python 불필요, COM 드라이버만 준비  
3. **압축 배포:** `HSC_PC_Test_Tool.exe` + 버전명이 붙은 zip + (선택) 짧은 사용 안내  

이 절차를 따르면 Windows 사용자에게 **설치 부담을 최소화한 배포**가 가능합니다.
