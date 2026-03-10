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
