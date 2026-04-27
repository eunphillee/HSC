# H2TECH Address Map - Unified Rule v1.3

Encoding: **UTF-8**

**파일:** `h2tech_address_map_V1.3.md`  
**용도:** 업체 전달용 주소 표  
**참고 구현:** `upstream_slave_h2tech.c`, `h2tech_address_map.c`, `board_rtc.c`

표 컬럼: **FC | 관리주소 | 항목 | 설명**

---

# FC04 - Read Input Registers

## FC04-1. MAIN + PACKED (관리주소 0 ~ 93)

| FC | 관리주소 | 항목 | 설명 |
|----|----------|------|------|
| FC04 | 0 | MAIN_STATUS | 1=정상(error_flags==0), 0=이상 |
| FC04 | 1 | ERROR_FLAGS | 에러 플래그 (비트별 aggregated error) |
| FC04 | 2 | D_I_01 | 메인 DI #1 |
| FC04 | 3 | D_I_02 | 메인 DI #2 |
| FC04 | 4 | D_I_03 | 메인 DI #3 |
| FC04 | 5 | D_I_04 | 메인 DI #4 |
| FC04 | 6 | D_I_05 | 메인 DI #5 |
| FC04 | 7 | D_I_06 | 메인 DI #6 |
| FC04 | 8 | D_I_07 | 메인 DI #7 |
| FC04 | 9 | D_I_08 | 메인 DI #8 |
| FC04 | 10 | PC_LED_IN | PC_LED 입력 상태 (0=OFF, 1=ON) |
| FC04 | 11 | MAIN_RELAY_01 | 메인 릴레이 #1 현재 상태 |
| FC04 | 12 | MAIN_RELAY_02 | 메인 릴레이 #2 현재 상태 |
| FC04 | 13 | MAIN_RELAY_03 | 메인 릴레이 #3 현재 상태 |
| FC04 | 14 | MAIN_RELAY_04 | 메인 릴레이 #4 현재 상태 |
| FC04 | 15 | ENV_TEMP_EXT0 | 외부 온도 × 10 (signed), 센서에러=-32768 |
| FC04 | 16 | ENV_RH_EXT0 | 외부 습도 × 10, 센서에러=0xFFFF |
| FC04 | 17 | RESERVED | 예약 |
| FC04 | 18 | RESERVED | 예약 |
| FC04 | 19 | RESERVED | 예약 |
| FC04 | 20 | MAIN_VBIT_1 | 가상비트 #1 (자동연동 워드 0) |
| FC04 | 21 | MAIN_VBIT_2 | 가상비트 #2 (자동연동 워드 1) |
| FC04 | 22 | MAIN_VBIT_3 | 가상비트 #3 (자동연동 워드 2) |
| FC04 | 23 | MAIN_VBIT_4 | 가상비트 #4 (자동연동 워드 3) |
| FC04 | 24 | HPSB_ALIVE | HPSB 하트비트/통신 상태 |
| FC04 | 25 | RESERVED | 예약 |
| FC04 | 26 | RESERVED | 예약 |
| FC04 | 27 | RESERVED | 예약 |
| FC04 | 28 | LPSB1_ALIVE | LPSB1(slave=2) 하트비트/통신 상태 |
| FC04 | 29 | LPSB2_ALIVE | LPSB2(slave=4) 하트비트/통신 상태 |
| FC04 | 30 | LPSB3_ALIVE | LPSB3(slave=8) 하트비트/통신 상태 |
| FC04 | 31 | RESERVED | 예약 |
| FC04 | 32 | RESERVED | 예약 |
| FC04 | 33 | RESERVED | 예약 |
| FC04 | 34 | HPSB_CON_1 | HPSB ON/OFF #1 |
| FC04 | 35 | HPSB_CON_2 | HPSB ON/OFF #2 |
| FC04 | 36 | HPSB_CON_3 | HPSB ON/OFF #3 |
| FC04 | 37 | LPSB1_SSW1 | LPSB1(slave=2) 스위치 #1 |
| FC04 | 38 | LPSB1_SSW2 | LPSB1(slave=2) 스위치 #2 |
| FC04 | 39 | LPSB1_SSW3 | LPSB1(slave=2) 스위치 #3 |
| FC04 | 40 | LPSB2_SSW1 | LPSB2(slave=4) 스위치 #1 |
| FC04 | 41 | LPSB2_SSW2 | LPSB2(slave=4) 스위치 #2 |
| FC04 | 42 | LPSB2_SSW3 | LPSB2(slave=4) 스위치 #3 |
| FC04 | 43 | LPSB3_SSW1 | LPSB3(slave=8) 스위치 #1 |
| FC04 | 44 | LPSB3_SSW2 | LPSB3(slave=8) 스위치 #2 |
| FC04 | 45 | LPSB3_SSW3 | LPSB3(slave=8) 스위치 #3 |
| FC04 | 46 | HPSB_ADC_AVG1 | HPSB ADC AVG #1 |
| FC04 | 47 | HPSB_ADC_AVG2 | HPSB ADC AVG #2 |
| FC04 | 48 | HPSB_ADC_AVG3 | HPSB ADC AVG #3 |
| FC04 | 49 | LPSB1_ADC_AVG1 | LPSB1(slave=2) ADC AVG #1 |
| FC04 | 50 | LPSB1_ADC_AVG2 | LPSB1(slave=2) ADC AVG #2 |
| FC04 | 51 | LPSB1_ADC_AVG3 | LPSB1(slave=2) ADC AVG #3 |
| FC04 | 52 | LPSB2_ADC_AVG1 | LPSB2(slave=4) ADC AVG #1 |
| FC04 | 53 | LPSB2_ADC_AVG2 | LPSB2(slave=4) ADC AVG #2 |
| FC04 | 54 | LPSB2_ADC_AVG3 | LPSB2(slave=4) ADC AVG #3 |
| FC04 | 55 | LPSB3_ADC_AVG1 | LPSB3(slave=8) ADC AVG #1 |
| FC04 | 56 | LPSB3_ADC_AVG2 | LPSB3(slave=8) ADC AVG #2 |
| FC04 | 57 | LPSB3_ADC_AVG3 | LPSB3(slave=8) ADC AVG #3 |
| FC04 | 58 | HPSB_ADC_PEAK1 | HPSB ADC PEAK #1 |
| FC04 | 59 | HPSB_ADC_PEAK2 | HPSB ADC PEAK #2 |
| FC04 | 60 | HPSB_ADC_PEAK3 | HPSB ADC PEAK #3 |
| FC04 | 61 | LPSB1_ADC_PEAK1 | LPSB1(slave=2) ADC PEAK #1 |
| FC04 | 62 | LPSB1_ADC_PEAK2 | LPSB1(slave=2) ADC PEAK #2 |
| FC04 | 63 | LPSB1_ADC_PEAK3 | LPSB1(slave=2) ADC PEAK #3 |
| FC04 | 64 | LPSB2_ADC_PEAK1 | LPSB2(slave=4) ADC PEAK #1 |
| FC04 | 65 | LPSB2_ADC_PEAK2 | LPSB2(slave=4) ADC PEAK #2 |
| FC04 | 66 | LPSB2_ADC_PEAK3 | LPSB2(slave=4) ADC PEAK #3 |
| FC04 | 67 | LPSB3_ADC_PEAK1 | LPSB3(slave=8) ADC PEAK #1 |
| FC04 | 68 | LPSB3_ADC_PEAK2 | LPSB3(slave=8) ADC PEAK #2 |
| FC04 | 69 | LPSB3_ADC_PEAK3 | LPSB3(slave=8) ADC PEAK #3 |
| FC04 | 70 | HPSB_CUR_S1 | HPSB 전류/상태 #1 |
| FC04 | 71 | HPSB_CUR_S2 | HPSB 전류/상태 #2 |
| FC04 | 72 | HPSB_CUR_S3 | HPSB 전류/상태 #3 |
| FC04 | 73 | LPSB1_CUR_S1 | LPSB1(slave=2) 전류/상태 #1 |
| FC04 | 74 | LPSB1_CUR_S2 | LPSB1(slave=2) 전류/상태 #2 |
| FC04 | 75 | LPSB1_CUR_S3 | LPSB1(slave=2) 전류/상태 #3 |
| FC04 | 76 | LPSB2_CUR_S1 | LPSB2(slave=4) 전류/상태 #1 |
| FC04 | 77 | LPSB2_CUR_S2 | LPSB2(slave=4) 전류/상태 #2 |
| FC04 | 78 | LPSB2_CUR_S3 | LPSB2(slave=4) 전류/상태 #3 |
| FC04 | 79 | LPSB3_CUR_S1 | LPSB3(slave=8) 전류/상태 #1 |
| FC04 | 80 | LPSB3_CUR_S2 | LPSB3(slave=8) 전류/상태 #2 |
| FC04 | 81 | LPSB3_CUR_S3 | LPSB3(slave=8) 전류/상태 #3 |
| FC04 | 82 | HPSB_POWER_W1 | HPSB 추정전력 #1 (W, Estimated @220V) |
| FC04 | 83 | HPSB_POWER_W2 | HPSB 추정전력 #2 (W, Estimated @220V) |
| FC04 | 84 | HPSB_POWER_W3 | HPSB 추정전력 #3 (W, Estimated @220V) |
| FC04 | 85 | LPSB1_POWER_W1 | LPSB1(slave=2) 추정전력 #1 (W, Estimated @220V) |
| FC04 | 86 | LPSB1_POWER_W2 | LPSB1(slave=2) 추정전력 #2 (W, Estimated @220V) |
| FC04 | 87 | LPSB1_POWER_W3 | LPSB1(slave=2) 추정전력 #3 (W, Estimated @220V) |
| FC04 | 88 | LPSB2_POWER_W1 | LPSB2(slave=4) 추정전력 #1 (W, Estimated @220V) |
| FC04 | 89 | LPSB2_POWER_W2 | LPSB2(slave=4) 추정전력 #2 (W, Estimated @220V) |
| FC04 | 90 | LPSB2_POWER_W3 | LPSB2(slave=4) 추정전력 #3 (W, Estimated @220V) |
| FC04 | 91 | LPSB3_POWER_W1 | LPSB3(slave=8) 추정전력 #1 (W, Estimated @220V) |
| FC04 | 92 | LPSB3_POWER_W2 | LPSB3(slave=8) 추정전력 #2 (W, Estimated @220V) |
| FC04 | 93 | LPSB3_POWER_W3 | LPSB3(slave=8) 추정전력 #3 (W, Estimated @220V) |

## FC04-2. RTC (관리주소 890 ~ 896)

| FC | 관리주소 | 항목 | 설명 |
|----|----------|------|------|
| FC04 | 890 | RTC_YEAR | 장치 RTC 연도 (YYYY, 2000~2099) |
| FC04 | 891 | RTC_MONTH | 월 (1~12) |
| FC04 | 892 | RTC_DAY | 일 (1~31) |
| FC04 | 893 | RTC_WEEKDAY | 요일 (0=Sun .. 6=Sat) |
| FC04 | 894 | RTC_HOUR | 시 (0~23) |
| FC04 | 895 | RTC_MINUTE | 분 (0~59) |
| FC04 | 896 | RTC_SECOND | 초 (0~59) |

## FC04-3. 시스템 / 환경 (관리주소 2100 ~ 2122)

코드 기준 실제 주소. 2102~2109 중 일부는 mainboard ID 저장 진단용으로 사용한다.

| FC | 관리주소 | 항목 | 설명 |
|----|----------|------|------|
| FC04 | 2100 | MAIN_IO_DI_BITMAP | 메인 DI bitmap (bit0=DI01 .. bit7=DI08) |
| FC04 | 2101 | MAIN_IO_DO_BITMAP | 메인 DO bitmap (bit0=RELAY1 .. bit3=RELAY4) |
| FC04 | 2102 | MB_SLAVE_EFFECTIVE | 현재 적용 중인 mainboard slave id |
| FC04 | 2103 | MB_SLAVE_PENDING | 저장 전 pending slave id |
| FC04 | 2104 | MB_BAUD_PENDING | 저장 전 pending baudrate |
| FC04 | 2105 | RESERVED | 예약 |
| FC04 | 2106 | RESERVED | 예약 |
| FC04 | 2107 | PC_NO_COMM_TIMEOUT_SEC | PC 무통신 워치독 타임아웃(**초**), EEPROM(`4x3003`)과 동일 값. **PC 읽기는 FC04 전용 규칙**에 따라 본 주소 사용 |
| FC04 | 2108 | SYSCFG_LAST_SAVE_STATUS | 마지막 `SystemConfig_Save()` 상태 코드 |
| FC04 | 2109 | COIL7_LAST_FAIL_CODE | 마지막 `Save ID`(FC05 coil7) 실패 코드 |
| FC04 | 2110 | MAIN_ENV_TEMP | 보드 온도 × 10 (signed), 센서에러=-32768 |
| FC04 | 2111 | MAIN_ENV_RH | 보드 습도 × 10, 센서에러=0xFFFF |
| FC04 | 2112 | MAIN_ENV_ERR_FLAGS | 환경/통신 에러 플래그 |
| FC04 | 2113 | RESET_CSR_LOW | RCC CSR 하위 16비트 |
| FC04 | 2114 | RESET_CSR_HIGH | RCC CSR 상위 16비트 |
| FC04 | 2122 | PC_LED_IN_REG | PC_LED 입력 상태 (0=OFF, 1=ON, read-only) |

### 2108 / 2109 진단 코드

- `2108 = SYSCFG_LAST_SAVE_STATUS`
  - `0`: OK
  - `1`: NULL cfg
  - `2`: validate 실패
  - `3`: EEPROM write 실패
  - `4`: EEPROM read 실패
  - `5`: readback validate 실패
  - `6`: sequence mismatch
- `2109 = COIL7_LAST_FAIL_CODE`
  - `0`: coil7 save 성공 / 실패 없음
  - `1`: pending slave id invalid/reserved
  - `2`: `SystemConfig_Get()` NULL
  - `0x100 | n`: `SystemConfig_Save()` 실패. 하위 `n`은 위 2108 상태 코드와 동일

## FC04-4. 진단 DIAG (관리주소 4000 ~ 4039)

총 **40 워드** (UPSTREAM_DIAG_IR_COUNT = 40).

| FC | 관리주소 | 항목 | 설명 |
|----|----------|------|------|
| FC04 | 4000 | DIAG_MAIN_STATUS | 메인 정상 여부 (1=정상, 0=이상) |
| FC04 | 4001 | DIAG_HPSB_ONLINE | HPSB 통신 정상 (1=정상, 0=오류) |
| FC04 | 4002 | DIAG_LPSB_ONLINE_ANY | LPSB 중 하나라도 통신 정상 (1=정상) |
| FC04 | 4003 | DIAG_HPSB_STATUS_REG | HPSB status register |
| FC04 | 4004 | DIAG_LPSB_ALARM_REG | LPSB1 alarm register |
| FC04 | 4005 | DIAG_LPSB1_SENSE1 | LPSB1 ADC raw #1 |
| FC04 | 4006 | DIAG_LPSB1_SENSE2 | LPSB1 ADC raw #2 |
| FC04 | 4007 | DIAG_LPSB1_SENSE3 | LPSB1 ADC raw #3 |
| FC04 | 4008 | DIAG_ERROR_FLAGS | aggregated error_flags |
| FC04 | 4009 | DIAG_UART_ERR_COUNT | UART2 ORE 에러 카운터 |
| FC04 | 4010 | DIAG_DI_REMAP | 메인 DI bitmap (진단용) |
| FC04 | 4011 | DIAG_DO_REMAP | 메인 DO bitmap (진단용) |
| FC04 | 4012 | DIAG_LAST_FAIL_SID | 마지막 sub-fail slave id |
| FC04 | 4013 | DIAG_LAST_FAIL_FC | 마지막 sub-fail FC |
| FC04 | 4014 | DIAG_LAST_FAIL_REASON | 마지막 sub-fail reason |
| FC04 | 4015 | DIAG_LAST_FAIL_RXLEN | 마지막 sub-fail rx len |
| FC04 | 4016 | DIAG_SID_ROW0 | row0 fail: slave id |
| FC04 | 4017 | DIAG_FC_ROW0 | row0 fail: FC |
| FC04 | 4018 | DIAG_REASON_ROW0 | row0 fail: reason |
| FC04 | 4019 | DIAG_RXLEN_ROW0 | row0 fail: rx len |
| FC04 | 4020 | DIAG_SID_ROW1 | row1 fail: slave id |
| FC04 | 4021 | DIAG_FC_ROW1 | row1 fail: FC |
| FC04 | 4022 | DIAG_REASON_ROW1 | row1 fail: reason |
| FC04 | 4023 | DIAG_RXLEN_ROW1 | row1 fail: rx len |
| FC04 | 4024 | DIAG_SID_ROW2 | row2 fail: slave id |
| FC04 | 4025 | DIAG_FC_ROW2 | row2 fail: FC |
| FC04 | 4026 | DIAG_REASON_ROW2 | row2 fail: reason |
| FC04 | 4027 | DIAG_RXLEN_ROW2 | row2 fail: rx len |
| FC04 | 4028 | DIAG_SID_ROW3 | row3 fail: slave id |
| FC04 | 4029 | DIAG_FC_ROW3 | row3 fail: FC |
| FC04 | 4030 | DIAG_REASON_ROW3 | row3 fail: reason |
| FC04 | 4031 | DIAG_RXLEN_ROW3 | row3 fail: rx len |
| FC04 | 4032 | NVMI_LOADED | EEPROM 로드 성공 여부 (1=OK) |
| FC04 | 4033 | NVMI_DIRTY | EEPROM 미반영 데이터 유무 (1=dirty) |
| FC04 | 4034 | NVMI_SEQUENCE | 저장 횟수 시퀀스 |
| FC04 | 4035 | NVMI_LAST_SAVE_RESULT | 마지막 저장 결과 |
| FC04 | 4036 | NVMI_LAST_LOAD_RESULT | 마지막 로드 결과 |
| FC04 | 4037 | NVM_RESTORE_TRY_MASK | 복원 시도 완료 마스크 (OutputStateNvm_GetRestoreDoneMask) |
| FC04 | 4038 | NVM_RESTORE_OK_MASK | 복원 피드백 일치 마스크 (OutputStateNvm_GetRestoreOkMask) |
| FC04 | 4039 | RESERVED | 예약 (0, DIAG 배열 마지막 워드) |

---

# FC05 - Write Single Coil

표 컬럼: **FC | 주소 | 명칭 | 설명**

| FC | 주소 | 명칭 | 설명 |
|----|------|------|------|
| FC05 | 0 | MAIN_RELAY1_EN | 메인보드 릴레이#1 ON/OFF (0xFF00=ON, 0x0000=OFF) |
| FC05 | 1 | MAIN_RELAY2_EN | 메인보드 릴레이#2 ON/OFF |
| FC05 | 2 | MAIN_RELAY3_EN | 메인보드 릴레이#3 ON/OFF |
| FC05 | 3 | MAIN_RELAY4_EN | 메인보드 릴레이#4 ON/OFF |
| FC05 | 4 | PC_ON | PC_ON 펄스 트리거 (ON 쓰기 시 펄스) |
| FC05 | 5 | PC_OFF | PC_ON_EN=0 (ON 쓰기 시 OFF 동작) |
| FC05 | 6 | PC_RESET | PC_RESET 펄스 트리거 (ON 쓰기 시 펄스) |
| FC05 | 7 | CONFIG_SAVE_TRIGGER | pending ID/baud를 EEPROM에 저장하는 trigger |
| FC05 | 8 | RESERVED | 예약 |
| FC05 | 9 | RESERVED | 예약 |
| FC05 | 10 | RESERVED | 예약 |
| FC05 | 11 | RESERVED | 예약 |
| FC05 | 12 | RESERVED | 예약 |
| FC05 | 13 | RESERVED | 예약 |
| FC05 | 14 | RESERVED | 예약 |
| FC05 | 15 | RESERVED | 예약 |
| FC05 | 16 | RESERVED | 예약 |
| FC05 | 17 | RESERVED | 예약 |
| FC05 | 18 | RESERVED | 예약 |
| FC05 | 19 | RESERVED | 예약 |
| FC05 | 20 | MAIN_VBIT_1 | 가상비트#1 ON/OFF |
| FC05 | 21 | MAIN_VBIT_2 | 가상비트#2 ON/OFF |
| FC05 | 22 | MAIN_VBIT_3 | 가상비트#3 ON/OFF |
| FC05 | 23 | MAIN_VBIT_4 | 가상비트#4 ON/OFF |
| FC05 | 891 | RESERVED | 예약 |
| FC05 | 892 | RESERVED | 예약 |
| FC05 | 893 | RESERVED | 예약 |
| FC05 | 894 | RESERVED | 예약 |
| FC05 | 895 | RESERVED | 예약 |
| FC05 | 896 | RESERVED | 예약 |
| FC05 | 897 | RESERVED | 예약 |
| FC05 | 898 | WR_HPSB_COIL_0 | HPSB 릴레이#1 (메인보드가 하위로 전달) |
| FC05 | 899 | WR_HPSB_COIL_1 | HPSB 릴레이#2 |
| FC05 | 900 | WR_HPSB_COIL_2 | HPSB 릴레이#3 |
| FC05 | 901 | WR_LPSB1_COIL_0 | LPSB1(slave=2) SSR#1 |
| FC05 | 902 | WR_LPSB1_COIL_1 | LPSB1(slave=2) SSR#2 |
| FC05 | 903 | WR_LPSB1_COIL_2 | LPSB1(slave=2) SSR#3 |
| FC05 | 904 | WR_LPSB2_COIL_0 | LPSB2(slave=4) SSR#1 |
| FC05 | 905 | WR_LPSB2_COIL_1 | LPSB2(slave=4) SSR#2 |
| FC05 | 906 | WR_LPSB2_COIL_2 | LPSB2(slave=4) SSR#3 |
| FC05 | 907 | WR_LPSB3_COIL_0 | LPSB3(slave=8) SSR#1 |
| FC05 | 908 | WR_LPSB3_COIL_1 | LPSB3(slave=8) SSR#2 |
| FC05 | 909 | WR_LPSB3_COIL_2 | LPSB3(slave=8) SSR#3 |

**비고:** FC15 (Write Multiple Coils) 는 upstream 에서 허용하지 않음 (Illegal Function).

---

# FC16 - Write Multiple Holding Registers

| FC | 주소 | 명칭 | 설명 |
|----|------|------|------|
| FC16 | 890 | RTC_SET_YEAR | RTC 설정(년 YYYY, 2000~2099) — 890~896 총 7워드 |
| FC16 | 891 | RTC_SET_MONTH | RTC 설정(월 1~12) |
| FC16 | 892 | RTC_SET_DAY | RTC 설정(일 1~31) |
| FC16 | 893 | RTC_SET_WEEKDAY | RTC 설정(요일 0=Sun..6=Sat) |
| FC16 | 894 | RTC_SET_HOUR | RTC 설정(시 0~23) |
| FC16 | 895 | RTC_SET_MINUTE | RTC 설정(분 0~59) |
| FC16 | 896 | RTC_SET_SECOND | RTC 설정(초 0~59) |

**조건:** `start == 890`, `count == 7` 인 FC16 만 허용. 그 외 주소/count 는 예외(0x02).

---

# 메모 — 구 사양 이미지 표기와의 차이

아래는 전달받은 이미지(구 사양)의 표기와 현재 코드/문서 사이의 차이를 기록합니다.  
**코드 동작은 변경하지 않았으며**, 문서는 코드 기준으로 수정되었습니다.

| 항목 | 구 사양 이미지 표기 | 현재 코드/문서 기준 | 처리 |
|------|---------------------|----------------------|------|
| FC04 20~23 명칭 | MAIN_VREF_1~4 | **MAIN_VBIT_1~4** (가상비트/자동연동 워드) | 코드 기준으로 수정 |
| FC04 24~81 보드 명칭 | H_PSU / LPS01 등 | **HPSB / LPSB1(slave=2) / LPSB2(slave=4) / LPSB3(slave=8)** | 코드 기준으로 수정, 구 명칭은 동일 의미 |
| FC04 2100 구간 | 이미지에 2105=DO remap 표기 | **2100=DI bitmap, 2101=DO bitmap**, 2105=예약 | 코드 기준으로 수정 |
| FC04 2122 | 이미지에 미표기 | **2122=PC_LED_IN_REG** (read-only) | 코드 기준으로 추가 |
| FC05 891~897 | RESERVED | **RESERVED** (외부 주소표 기준 예약으로 통일) | 이미지 기준으로 유지 |
| DIAG 4001/4002 명칭 | HPSU/LPSU | **DIAG_HPSB_ONLINE / DIAG_LPSB_ONLINE_ANY** | 코드 기준으로 수정 |
| DIAG 4037 명칭 | TRY_COUNT 류 | **NVM_RESTORE_TRY_MASK** (count 아닌 마스크) | 코드 기준으로 수정 |
| DIAG 범위 | 이미지에 4038까지 | **4000~4039 (40 워드)** | 코드 기준으로 수정 (4039 추가) |
| RTC 연도 | 일부 초안에 0~99 | **YYYY (2000~2099)** | 코드 기준으로 수정 |

---

## v1.3 요약

- **FC04:** 주소 **0~93** 통합 PACKED (기존 0~81 + 추정전력 `POWER_W` 82~93). RTC **890~896**, ENV/IO **2100~2122**, DIAG **4000~4039** (40 워드).
- **FC05:** MAIN 릴레이 **0~6**, 가상비트 **20~23**, RESERVED **891~897**, 하위 SSR **898~909**.
- **FC16:** RTC 설정 **890~896** (start=890, count=7 전용).
- **코드 동작 변경 없음** — 이번 작업은 문서/주소 정의 정리만.

---

# 납품 반영 (2026-04-27) — 당일 추가·변경 사항

아래는 통합 주소표(스프레드시트)와 **동일 표 형식**(`FC | 주소 | 명칭 | 설명`)으로 정리한 항목입니다. 엑셀에 반영 시 이 블록을 그대로 옮기면 됩니다.

**표기 주의:** 외부 초안에 **FC05 주소 3**에 PC 상태 유지 시간을 넣는 경우가 있으나, **현재 펌웨어는 FC05|3 = `MAIN_RELAY4_EN`** 입니다. PC 무통신 워치독 시간은 **쓰기: 홀딩 `4x3003` + `FC06`**, **읽기: 입력 `FC04` 주소 `2107` 미러(통합 규칙 “읽기=FC04만”)**, 단위 **초**, EEPROM 저장으로 통일합니다.

## FC04 — Read Input Registers (시스템 설정 미러, PC 읽기용)

| FC | 주소 | 명칭 | 설명 |
|----|------|------|------|
| FC04 | 2107 | PC_NO_COMM_TIMEOUT_SEC | `4x3003`(EEPROM)과 동일한 PC 무통신 워치독 타임아웃(**초**). 상위 PC/PLC는 **FC04**로만 읽을 것 |

## FC06 — Write Single Register (시스템 설정 / EEPROM)

| FC | 주소 | 명칭 | 설명 |
|----|------|------|------|
| FC06 | 3000 | SET_SYSCFG_SLAVE_ID | Slave ID 쓰기 및 EEPROM 저장 (유효 범위 외 예외 응답) |
| FC06 | 3001 | SET_SYSCFG_BAUD_CODE | 보드레이트 코드 쓰기 및 EEPROM 저장 |
| FC06 | 3002 | SET_FACTORY_RESET | `1` 쓰기 시 팩토리 리셋 실행 |
| FC06 | 3003 | SET_PC_NO_COMM_TIMEOUT_SEC | PC 무통신 워치독 타임아웃(**초**) 쓰기 및 EEPROM 저장 |

## FC05 — Write Single Coil (기존 행 설명 보강)

| FC | 주소 | 명칭 | 설명 |
|----|------|------|------|
| FC05 | 20 | MAIN_VBIT_1 | 가상비트#1 ON/OFF — **변경 시 EEPROM(`OutputStateNvm`)에 저장**, 재부팅 후 복원 |
| FC05 | 21 | MAIN_VBIT_2 | 가상비트#2 ON/OFF — 동일 (EEPROM 유지) |
| FC05 | 22 | MAIN_VBIT_3 | 가상비트#3 ON/OFF — 동일 (EEPROM 유지) |
| FC05 | 23 | MAIN_VBIT_4 | 가상비트#4 ON/OFF — 동일 (EEPROM 유지) |

## 동작 사양 (주소 외 로직)

| 구분 | 명칭 | 설명 |
|------|------|------|
| 펌웨어 | PC 무통신 워치독 | 상기 `4x3003` 초 동안 PC 측 **유효 Modbus 요청**이 없으면, `FC04` **PC_LED_IN** 상태에 따라 `PC_ON` 펄스 또는 `PC_RESET` 펄스를 주기적으로 반복(간격=설정 초). 통신 복구 시 타이머 리셋 |
| 펌웨어 | 서브보드 FC04 센서 안정화 | UART2 마스터 수신: CRC 미통과·FC04 응답 길이 불일치 시 폐기. Input Register 갱신 전 **범위(예: 4095 초과) 및 jump 필터** 적용, 비정상 시 이전 정상값 유지. 로그 태그: `[MODBUS][DROP]`, `[SENSOR][DROP]` |
| PC 도구 | PC state time | Modbus 탭 Stop 옆 **1~600분** 스핀 + **Set**(EEPROM 저장) / **Read**(현재값 표시) |
