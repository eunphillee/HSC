"""
Mainboard Modbus address map (1:1 with mainboard firmware).
Change only these constants when firmware mapping changes; UI logic stays the same.
All addresses for a single slave (Mainboard only).
"""
# Slave ID for Mainboard (default, H2Tech 문서 기준)
MAINBOARD_SLAVE_ID_DEFAULT = 9

# ---- Mainboard I/O (FC03/FC06) ----
# FC03 read start=MAIN_DI_REG count=1 → low 8 bits = DI_01..DI_08
MAIN_DI_REG = 2100
MAIN_DI_COUNT = 8

# FC06 write single register MAIN_DO_REG → bits 0..3 = RELAY1_EN..RELAY4_EN
MAIN_DO_REG = 2101
MAIN_DO_COUNT = 4

# ---- PC control GPIO (4x2120..2122) ----
# FC06 write: value=1 → 100ms HIGH pulse, value=0 → LOW
PC_ON_EN_REG = 2120
PC_RESET_EN_REG = 2121
# FC03 read 1 register: 0=OFF, 1=ON
PC_LED_IN_REG = 2122

# ---- MAIN Env (SHTC3) ----
# FC03 read start=MAIN_ENV_REG count=2
# Reg0: temp_c_x10 (signed)
# Reg1: rh_x10 (unsigned)
MAIN_ENV_REG = 2110
MAIN_ENV_COUNT = 3  # + error_flags

# ---- HPSB/LPSB (via Mainboard FC03/FC02/FC05) ----
# FC03 start=2000 count=40: 각 보드당 9워드 × 4(HPSB+LPSB1+2+3) + 예약 4
# 보드 오프셋 0,9,18,27: [0..2]=AVG, [3..5]=PKPK, [6..8]=CURRENT(0/1)
SUB_SENSE_REG = 2000
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
# FC05 write sub-board coil (value 0 or 1). Mainboard forwards to HPSB(slave 1)/LPSB(2,4,8).
# Mainboard: h2_dec = start_addr+1. 897=Door1, 898=Door2, 899..901=HPSB coil 0..2, 902..910=LPSB.
# So start_addr 896=Door1, 897=Door2, 898..900=HPSB RELAY1..3, 901..909=LPSB (9 coils).
SUB_HPSB_COIL_BASE = 898   # FC05 addr 898,899,900 = HPSB coil 0,1,2 (RELAY1,2,3) — matches Mainboard h2_dec 899..901
SUB_HPSB_COIL_COUNT = 3
SUB_LPSB_COIL_BASE = 901   # FC05 addr 901..909 = LPSB1/2/3 coil 0,1,2 each (h2_dec 902..910)
SUB_LPSB_COIL_COUNT = 9    # 901..903=LPSB1, 904..906=LPSB2, 907..909=LPSB3
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
