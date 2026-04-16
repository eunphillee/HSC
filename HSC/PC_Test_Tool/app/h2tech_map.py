"""
H2TECH address mapping: 1xNNNN -> Modbus start address = NNNN - 1.
Bit packing in responses: LSB-first, lowest address = bit0.
"""
# PC UI / 문서용 기본값. 메인보드 실제 Modbus unit은 EEPROM(SystemConfig) slave_id (기본 9).
MAIN_SLAVE_ID_DEFAULT = 9

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

# FC03 current block: start=2000, count=40 (AVG/PKPK/CURRENT per HPSB + LPSB×3 + reserved×4)
CURRENT_START = 2000
CURRENT_COUNT = 40

# FC05 write (coil): 1xNNNN -> Modbus coil address = NNNN - 1
# 891~897 (1x0892~0898 VB/문열림): 펌웨어 미지원(제거됨).
# 하위 SSR: 1x0899~0910 -> Modbus coil 898..909 (HPSB/LPSB)
SUB_COIL_WRITE_FIRST_DOC = 899
SUB_COIL_WRITE_LAST_DOC = 910

# MAIN Board I/O — v1.3: PC 툴은 FC04 MAIN(0..) + FC05 coil0..3/4/5/6 가 단일 경로.
# 4x2100/2101은 ENV 존 비트맵(보조); 레거시 FC03/FC06 경로는 펌에 따라 다를 수 있음.
MAIN_IO_ENABLED = True
MAIN_DI_REG = 2100
MAIN_DO_REG = 2101
MAIN_DI_COUNT = 1
MAIN_DO_COUNT = 1
