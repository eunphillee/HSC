"""
Mainboard Modbus address map (1:1 with mainboard firmware).
Change only these constants when firmware mapping changes; UI logic stays the same.
All addresses for a single slave (Mainboard only).
"""
# Slave ID for Mainboard (default, H2Tech 문서 기준)
MAINBOARD_SLAVE_ID_DEFAULT = 9

# ---- Mainboard I/O (FC04/FC05) ----
# FC04 read start=MAIN_DI_REG count=1 → low 8 bits = DI_01..DI_08
MAIN_DI_REG = 2100
MAIN_DI_COUNT = 8

# FC05 write single coil MAIN_DO_REG → bits 0..3 = RELAY1_EN..RELAY4_EN
MAIN_DO_REG = 2101
MAIN_DO_COUNT = 4

# ---- PC control GPIO (4x2120..2122) ----
# FC05 write coil: value=1 → 100ms HIGH pulse, value=0 → LOW
PC_ON_EN_REG = 2120
PC_RESET_EN_REG = 2121
# FC04 read 1 register: 0=OFF, 1=ON
PC_LED_IN_REG = 2122

# ---- MAIN Env (SHTC3) ----
# FC04 read start=MAIN_ENV_REG count=2
# Reg0: temp_c_x10 (signed)
# Reg1: rh_x10 (unsigned)
MAIN_ENV_REG = 2110
MAIN_ENV_COUNT = 3  # + error_flags

# ---- HPSB/LPSB (via Mainboard FC04) ----
# UI sense 레이아웃(길이=40: 각 보드당 AVG[3],PKPK[3],CUR[3])은 유지하지만,
# 통신은 FC04로만 수행됩니다.
# Mainboard routing 복사영역 시작값:
# - HPSB copy: 100..115 (count=16, Unified Rule v1.1)
# - LPSB copies: 200..213, 300..313, 400..413 (각 count=14)
SUB_SENSE_REG = 100
SUB_SENSE_COUNT = 40
SUB_SENSE_BOARD_STRIDE = 9
# FC02 discrete (1x): H2 dec 823..836 = start 822, count 14 (ONOFF_3..14 = HPSB CH1~3, LPSB1~3 CH1~3)
SUB_COIL_STATUS_START = 822
SUB_COIL_STATUS_COUNT = 14
# FC02 discrete: H2 dec 869..880 = start 868, count 12 (ALM_1..12)
SUB_ALARM_START = 868
SUB_ALARM_COUNT = 12
# FC05 write single coil: H2 892..896 = Modbus addr 891..895 (VB_ONOFF_8..12 → LPSB pulse). 897,898=DOOR.
SUB_VB_COIL_BASE = 891   # FC05 addr 891 = H2 892 = VB_ONOFF_8 (LPSB1_CH3 pulse), ... 895=VB_ONOFF_12
SUB_VB_COIL_COUNT = 5    # 891..895
# Unified Rule:
# - Mainboard local relay control: FC05 coil0..3 (0-based)
# - Downstream(HPSB/LPSB) relay/SSR control via Mainboard routing: use H2Tech mapped coil addresses.
#   HPSB: 898..901 (RELAY1..4), LPSB: 902..910 (SSR1..3 * 3 boards)
# UI는 HPSB 채널을 3개만 표시(0..2)하므로 SUB_HPSB_COIL_COUNT는 3으로 둡니다.
SUB_HPSB_COIL_BASE = 898
SUB_HPSB_COIL_COUNT = 3
SUB_LPSB_COIL_BASE = 902   # FC05 addr 902..910 = LPSB2/3/4(=slave 2/4/8) SSR1..3
SUB_LPSB_COIL_COUNT = 9    # 902..904=slave2, 905..907=slave4, 908..910=slave8
# error_flags (FC03 2112): bit0=AGG_ERR_COMM_HPSB, bit1=AGG_ERR_COMM_LPSB
ERROR_FLAGS_REG = 2112


# ---- H2Tech 문서 주소(1x/4x) -> Modbus PDU 0-based offset 변환 ----
# 문서 표기 예: 1x0821, 1x0892 / 실제 요청은 address=0-based
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
