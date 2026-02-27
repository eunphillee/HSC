"""
H2TECH address mapping: 1xNNNN -> Modbus start address = NNNN - 1.
Bit packing in responses: LSB-first, lowest address = bit0.
"""
# FC02 discrete (1x) — Modbus start = NNNN - 1
# ON/OFF 1~16: 1x0821~0836 -> start 820, count 16
ONOFF_START = 820
ONOFF_COUNT = 16

# Door sensors: 1x0853~0860 -> start 852, count 8
DOOR_START = 852
DOOR_COUNT = 8

# Alarms 1~12: 1x0869~0880 -> start 868, count 12
ALARMS_START = 868
ALARMS_COUNT = 12

# CMD ON/OFF 1~7: 1x0885~0891 -> start 884, count 7
CMD_ONOFF_START = 884
CMD_ONOFF_COUNT = 7

# FC03 current block: ONLY start=2000, count=14
CURRENT_START = 2000
CURRENT_COUNT = 14

# FC05 write (coil): 1xNNNN -> Modbus coil address = NNNN - 1
# Door open: 1x0897 -> 896, 1x0898 -> 897
DOOR_OPEN_1_COIL = 896   # 1x0897
DOOR_OPEN_2_COIL = 897   # 1x0898
# Virtual buttons ON/OFF 8~12: 1x0892~0896 -> coils 891..895
VB_ONOFF_8_COIL = 891
VB_ONOFF_9_COIL = 892
VB_ONOFF_10_COIL = 893
VB_ONOFF_11_COIL = 894
VB_ONOFF_12_COIL = 895

# 1x0899, 1x0900 not in table -> exception 0x02
INVALID_COIL_899 = 898   # 1x0899 -> 898
INVALID_COIL_900 = 899   # 1x0900 -> 899

# MAIN Board I/O (Option 1: enable when firmware exposes FC03/FC06)
# FC03 start=2100 count=2: 4x2100=DI bitmap(bit0..7), 4x2101=DO bitmap(bit0..3); FC06 reg 2101 to write DO
MAIN_IO_ENABLED = True
MAIN_DI_REG = 2100
MAIN_DO_REG = 2101
MAIN_DI_COUNT = 1
MAIN_DO_COUNT = 1
