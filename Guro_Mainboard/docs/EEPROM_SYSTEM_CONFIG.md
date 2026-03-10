# EEPROM 기반 시스템 설정 저장/복원 (A/B 이중 블록)

## 개요
- 부팅 시 EEPROM(AT24C02C, I2C3)에서 **CONFIG_A** / **CONFIG_B** 두 블록을 읽고, 검증·sequence 비교 후 하나를 선택.
- 전원 차단 중 write 깨짐 방지, 블록 손상 시 백업 블록으로 복구.
- 적용 항목: **slave_id**(Modbus 1~247), **baudrate**(허용 목록만).

---

## EEPROM 주소 맵 (AT24C02C, 256바이트) — 업데이트

| 주소(hex) | 크기 | 용도 |
|-----------|------|------|
| **0x00 ~ 0x1F** | 32 bytes | **CONFIG_A** (sequence 2 + config 18, 나머지 예약) |
| **0x20 ~ 0x3F** | 32 bytes | **CONFIG_B** (동일 구조) |
| **0x40 ~ 0x4F** | 16 bytes | EEPROM_SelfTest 영역 (점검용, 테스트 후 0xFF 복구) |
| 0x50 ~ 0xFF | 나머지 | 예약 |

### 블록 내부 레이아웃 (저장 시 20바이트 사용)

| 오프셋 | 필드 | 크기 | 설명 |
|--------|------|------|------|
| 0 | sequence | 2 | LSB first. 저장 시 증가, Load 시 큰 쪽 선택 |
| 2 | magic | 2 | 0x4853 ("HS") |
| 4 | version | 1 | 1 |
| 5 | slave_id | 1 | Modbus 슬레이브 ID (1~247만 유효) |
| 6 | baudrate | 4 | USART1 보드레이트 (허용 목록만) |
| 10 | reserved | 8 | 0, 확장용 |
| 18 | crc | 2 | CRC16 (Modbus), 오프셋 2~17 (config 16바이트)에 대해 계산 |

- **CRC 적용 범위**: 블록 내 config 부분 기준 magic~reserved 16바이트 (sequence 제외).

---

## A/B 이중 저장 구조

### Load 시
1. CONFIG_A(0x00~0x13), CONFIG_B(0x20~0x33) 각 20바이트 읽기.
2. sequence + config 파싱 후 **SystemConfig_Validate** 통과하는 블록만 유효 처리.
3. **유효 블록이 둘**: sequence가 **더 큰** 쪽 사용.
4. **유효 블록이 하나**: 해당 블록 사용.
5. **둘 다 실패**: 기본값 생성 후 CONFIG_A에 저장(seq=1), 해당 설정 적용.

### Save 시
1. **변경 여부 비교**: 현재 캐시와 새 cfg payload(magic,version,slave_id,baudrate,reserved) 동일하면 EEPROM 쓰기 생략, 0 반환.
2. **저장 시**: 현재 **사용 중이 아닌** 반대 블록에 먼저 저장 (sequence = 현재+1).
3. 저장 후 해당 블록 **Read back → Validate** 재확인.
4. 통과 시 active block 전환, 캐시 갱신.

### 검증 강화 (Validate)
- **slave_id**: 1~247만 허용. 범위 밖이면 Validate 실패.
- **baudrate**: 허용 목록만 통과 — 9600, 19200, 38400, 57600, 115200.
- 잘못된 값이면 Load 시 해당 블록 무효, 필요 시 defaults 또는 다른 블록으로 복구.

---

## Modbus 설정용 예약 레지스터 (향후 PC 툴에서 설정 변경 예정)

| Modbus 주소 (4x) | 용도 | 비고 |
|------------------|------|------|
| **4x3000** | slave_id | 1~247. Write 동작 추후 구현 |
| **4x3001** | baudrate code | 0=9600, 1=19200, 2=38400, 3=57600, 4=115200. Write 추후 구현 |
| **4x3002** | factory reset command | Write 1 = factory reset 실행. 추후 구현 |

- 헤더: `system_config.h` — `SYSCFG_MODBUS_SLAVE_ID_REG`, `SYSCFG_MODBUS_BAUDRATE_CODE_REG`, `SYSCFG_MODBUS_FACTORY_RESET_REG`.
- 실제 FC06/FC16 write 처리 및 EEPROM 반영은 추후 추가.

---

## Factory Reset 강화

- **SystemConfig_FactoryReset()** 동작:
  1. CONFIG_A(0x00~0x1F), CONFIG_B(0x20~0x3F) **전부 0xFF** 클리어.
  2. 기본값 재생성 후 **CONFIG_A**에만 저장 (sequence=1).
  3. active block = A, sequence = 1, **현재 캐시도 defaults로 갱신**.
  4. `SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET=1` 이면 weak **SystemConfig_LogFactoryResetDone()** 호출 → override 시 "CFG factory reset done" 등 로그 출력.

---

## 부팅 로그 개선

- `SYSTEM_CONFIG_BOOT_LOG=1` 이면 **SystemConfig_LogToUart(huart)** 로 한 줄 출력.
- 형식 예: **CFG[A] seq=3 id=9 baud=9600 valid=1**
  - active block (A/B), sequence, slave_id, baudrate, validate result(1=유효).
- on/off: `app_config.h` — `SYSTEM_CONFIG_BOOT_LOG`.

---

## 설정 변경 시 “변경 여부 비교 후 저장”

- **SystemConfig_Save(cfg)** 호출 시:
  - 현재 캐시와 cfg의 payload(magic, version, slave_id, baudrate, reserved) 비교.
  - **동일하면** EEPROM write 생략, 0 반환.
- `SYSTEM_CONFIG_LOG_SKIP_SAVE=1` 이면 생략 시 weak **SystemConfig_LogSkipSave()** 호출 → override 시 "CFG unchanged, skip save" 등 로그 가능.

---

## EEPROM Self-Test (생산/현장 점검용)

### API
- **int EEPROM_SelfTest(void);**
  - 반환: **0** = 성공, **-1** = 실패.

### 동작
- 테스트 영역: **0x40~0x4F** (16바이트). 설정 블록(0x00~0x3F)과 분리.
- 패턴 0x55, 0xAA, 0x00, 0xFF 순서로 write → read → compare.
- 모두 통과 후 해당 영역 0xFF로 복구.
- 한 번이라도 read/compare 실패 시 -1 반환.

### 사용 예
```c
if (EEPROM_SelfTest() == 0) {
  /* 통과: 생산/현장 점검 OK */
} else {
  /* 실패: EEPROM 또는 I2C 경로 점검 */
}
```

---

## 파일/함수 요약

### Application
- **Application/Inc/system_config.h**  
  - A/B 블록 베이스/크기, sequence, `system_config_t`, Validate/IsBaudrateAllowed, GetSequence/GetActiveBlock, Modbus 예약 주소(3000/3001/3002), LogSkipSave/LogFactoryResetDone.
- **Application/Src/system_config.c**  
  - Load(A/B 읽기, 유효·sequence 비교, 실패 시 defaults 저장), Save(비교 후 생략 또는 반대 블록 저장·재검증·전환), Validate(slave_id 1~247, baudrate 허용 목록, CRC), FactoryReset(A/B 0xFF 클리어 후 A에 defaults 저장).

### IO
- **IO/Inc/eeprom_24c02.h** / **IO/Src/eeprom_24c02.c**  
  - EEPROM_Read, EEPROM_Write, **EEPROM_SelfTest** (패턴 0x55/0xAA/0x00/0xFF write/read/compare).

### app_config.h
- `SYSTEM_CONFIG_BOOT_LOG` — 부팅 시 CFG[A/B] seq= id= baud= valid=1 로그.
- `SYSTEM_CONFIG_LOG_SKIP_SAVE` — Save 생략 시 LogSkipSave 호출.
- `SYSTEM_CONFIG_BOOT_LOG_FACTORY_RESET` — Factory reset 후 LogFactoryResetDone 호출.

---

## 기본값
- magic = 0x4853  
- version = 1  
- slave_id = 9  
- baudrate = 9600  
