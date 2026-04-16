"""
Mainboard Modbus address map (1:1 with Guro_Mainboard Gateway modbus_ir_map v1.3).
Change only these constants when firmware mapping changes; UI logic stays the same.
All PDU addresses are 0-based unless noted as H2 4x/1x document addresses.

운영 (메인보드 Slave ID):
- 기본 EEPROM 권장값은 9이며, PC SpinBox는 이 값으로 시작합니다.
- 현재 메인보드 빌드에서 MAINBOARD_UART1_SLAVE_ID_FORCE_LIT9=1 이면 UART1 슬레이브는
  EEPROM과 무관하게 항상 unit 9로만 응답합니다(PC 테스트용). 0이면 EEPROM slave_id를 따릅니다.
- EEPROM에 ID를 저장한 뒤 FORCE가 꺼진 빌드에서는 SpinBox를 보드 ID에 맞추고
  Disconnect → Connect 로 재연결합니다.
"""
MAINBOARD_SLAVE_ID_DEFAULT = 9

# ---- Mainboard MAIN zone (FC04 addr 0..81, s_ir_main) ----
# 1차 폴링: FC04 address=0, count=MAIN_FC04_DI_VBIT_COUNT(24) — status/DI/relay/env/VBIT
MAIN_FC04_DI_VBIT_COUNT = 24
# 전체 MAIN+PACKED 스냅샷 길이 (펌 MB_IR_MAIN_COUNT)
MAIN_FC04_VENDOR_SNAPSHOT_COUNT = 82
# PACKED 블록: FC04 address=24, count=MAIN_PACKED_FC04_COUNT — HPSB/LPSB alive/coils/AVG/PKPK/CUR
MAIN_PACKED_FC04_START = 24
MAIN_PACKED_FC04_COUNT = 58

# 문서 4x 보조(ENV 존 2100..): DI/DO 비트맵 참조용. 실제 PC 툴 DI/릴레이 읽기는 FC04 addr=0 사용.
MAIN_DI_REG = 2100
MAIN_DI_COUNT = 8

# 릴레이: FC05 coil 0..3 = RELAY1..4 (0-based PDU)
MAIN_DO_REG = 2101
MAIN_DO_COUNT = 4
MAIN_VBIT_COIL_BASE = 20
MAIN_VBIT_COUNT = 4

# ---- PC 제어 (FC05 coil, MAIN zone — not 4x2120) ----
PC_ON_EN_REG = 4
PC_OFF_EN_REG = 5
PC_RESET_EN_REG = 6

# ---- PC_LED_IN (FC04 MAIN 맵 reg10 = PDU addr 10; 보조: ENV 2122) ----
PC_LED_IN_REG = 10

# ---- ENV 존 (FC04 start=2100, 펌 s_ir_env; PC는 필요 시 이 구간 직접 읽기 가능) ----
MAIN_ENV_REG = 2110
MAIN_ENV_COUNT = 3

# ---- HPSB/LPSB (MAIN PACKED, FC04 addr 24..) ----
# UI sense 배열 길이 40: read_sub_sense()가 packed에서 재배열.
SUB_SENSE_REG = MAIN_PACKED_FC04_START
SUB_SENSE_COUNT = 40
SUB_SENSE_BOARD_STRIDE = 9
# FC04: 하위 보드 SSR/릴레이 피드백 복사 영역 시작(34), 레지스터 12개 읽기
SUB_COIL_STATUS_START = 34
SUB_COIL_STATUS_FC04_COUNT = 12
# FC02 discrete: HPSB/LPSB ON/OFF 표시(1x0823..) — 펌 H2Tech 경로 유지 시 PDU 822.., 길이 14
SUB_COIL_STATUS_COUNT = 14
SUB_ALARM_START = 868
SUB_ALARM_COUNT = 12
SUB_VB_COIL_BASE = 891
SUB_VB_COIL_COUNT = 0
SUB_HPSB_COIL_BASE = 898
SUB_HPSB_COIL_COUNT = 3
SUB_LPSB_COIL_BASE = 901
SUB_LPSB_COIL_COUNT = 9
# ENV 존 FC04 addr 2112 (s_ir_env[12]) = error_flags; MAIN 블록 reg1과 동일 값
ERROR_FLAGS_REG = 2112

# ---- NVM / DIAG (FC04 start=4000, count=40, addr 4000..4039) ----
NVM_DIAG_REG_START = 4000
NVM_DIAG_REG_COUNT = 40
NVM_DIAG_LOADED = 4032
NVM_DIAG_DIRTY = 4033
NVM_DIAG_SEQUENCE = 4034
NVM_DIAG_LAST_SAVE_RESULT = 4035
NVM_DIAG_LAST_LOAD_RESULT = 4036
NVM_DIAG_RESTORE_TRY_MASK = 4037
NVM_DIAG_RESTORE_OK_MASK = 4038
NVM_DIAG_FW_MARKER_FC04 = 4039

# ---- Mainboard RTC (FC04/FC16 PDU 890..896) ----
MAIN_RTC_REG_START = 890
MAIN_RTC_REG_COUNT = 7


# ---- H2Tech 문서 주소(1x/4x) -> Modbus PDU 0-based offset 변환 ----
DOC1X_DI_BASE = 821
DOC1X_COIL_BASE = 892
DOC4X_BASE = 2000


def doc1x_to_offset(doc_addr: int) -> int:
    """문서 1x 주소를 0-based offset으로 변환.
    - DI 영역: 821 기준
    - Coil 영역: 892 기준
    """
    if doc_addr >= DOC1X_COIL_BASE:
        return doc_addr - DOC1X_COIL_BASE
    if doc_addr >= DOC1X_DI_BASE:
        return doc_addr - DOC1X_DI_BASE
    return 0


def doc4x_to_offset(doc_addr: int) -> int:
    """문서 4x 주소를 0-based offset으로 변환."""
    if doc_addr < DOC4X_BASE:
        return 0
    return doc_addr - DOC4X_BASE
