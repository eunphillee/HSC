"""
Mainboard Modbus address map (1:1 with mainboard firmware).
Change only these constants when firmware mapping changes; UI logic stays the same.
All addresses for a single slave (Mainboard only).
"""
# Slave ID for Mainboard (default)
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
# FC03 start=2000 count=14: HPSB sense [0,1,2], LPSB1 [0,1,2], LPSB2 [0,1,2], LPSB3 [0,1,2], reserved [12,13]
SUB_SENSE_REG = 2000
SUB_SENSE_COUNT = 14
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
SUB_HPSB_COIL_BASE = 898   # FC05 addr 898,899,900 = HPSB coil 0,1,2 (H2 899,900,901)
SUB_HPSB_COIL_COUNT = 3
SUB_LPSB_COIL_BASE = 901  # FC05 addr 901..909 = LPSB1(2) coil 0,1,2, LPSB2(4) 0,1,2, LPSB3(8) 0,1,2
SUB_LPSB_COIL_COUNT = 9
# error_flags (FC03 2112): bit0=AGG_ERR_COMM_HPSB, bit1=AGG_ERR_COMM_LPSB
ERROR_FLAGS_REG = 2112
