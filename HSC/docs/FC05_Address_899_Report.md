# FC05 주소 899 Illegal Data Address 원인 및 수정 보고서

## 1. 문제 재정의 (로그 기준)

- **로그**: `[DEBUG] Button: HPSB RELAY1 EN -> FC05 addr=899 val=1` → `[HPSB] RX EXC 0x02` (Illegal Data Address)
- **해석**: PC↔Mainboard 통신은 정상. Mainboard가 FC05를 받고 **예외 0x02**를 반환함. 무응답이 아니라 **주소 매핑/처리 오류**.

---

## 2. HPSB RELAY1 EN 버튼의 주소 정의 위치

| 위치 | 파일 | 내용 |
|------|------|------|
| **상수 정의** | `HSC/PC_Test_Tool/app/address_map.py` | `SUB_HPSB_COIL_BASE = 898` (수정 후), `SUB_HPSB_COIL_COUNT = 3` |
| **버튼→주소** | `HSC/PC_Test_Tool/app/ui_main.py` | `addr = SUB_HPSB_COIL_BASE + idx` → RELAY1=898, RELAY2=899, RELAY3=900 |
| **전송** | `app/modbus_client.py` | `write_sub_coil(addr, value)` — addr는 898..909 범위 검사 |
| **진단 시퀀스** | `app/worker.py` | `steps`: HPSB 898,899,900 / LPSB 901..909 |

**수정 전**: `SUB_HPSB_COIL_BASE = 899` → RELAY1이 **899**로 전송됨.  
**수정 후**: `SUB_HPSB_COIL_BASE = 898` → RELAY1이 **898**로 전송됨.

---

## 3. Mainboard FC05 coil 주소 처리

### 3.1 변환 규칙

- **공식**: `h2_dec = start_addr + 1` (`H2Map_ModbusAddrToH2Dec`)
- 즉, Modbus **start_addr 898** → **h2_dec 899** (WR_HPSB_COIL_0 = RELAY1)

### 3.2 Mainboard가 허용하는 FC05 coil 주소 (h2_dec 기준)

| Modbus start_addr | h2_dec | 용도 |
|-------------------|--------|------|
| 891 | 892 | VB_ONOFF_8 |
| 892 | 893 | VB_ONOFF_9 |
| 893 | 894 | VB_ONOFF_10 |
| 894 | 895 | VB_ONOFF_11 |
| 895 | 896 | VB_ONOFF_12 |
| 896 | 897 | DOOR_OPEN_CTRL_1 |
| 897 | 898 | DOOR_OPEN_CTRL_2 |
| **898** | **899** | **WR_HPSB_COIL_0 (RELAY1)** |
| 899 | 900 | WR_HPSB_COIL_1 (RELAY2) |
| 900 | 901 | WR_HPSB_COIL_2 (RELAY3) |
| 901..909 | 902..910 | LPSB1/2/3 coil 0,1,2 |

**유효 범위**: start_addr **892~910** (h2_dec 893~911 아님, 892~910) → 테이블 기준 **892~910** (h2_dec).

---

## 4. addr=899가 0x02를 냈던 이유

- PC가 RELAY1을 **899**로 보냄.
- Mainboard: `h2_dec = 899 + 1 = 900` → **WR_HPSB_COIL_1** (두 번째 릴레이)로 매핑됨.
- 테이블에는 h2_dec 900이 **존재**하므로, “주소 없음”이 아니라 **ApplyWrite 실패**로 0x02가 나갔을 가능성이 큼.
  - 즉, **899 → h2_dec 900 → slave=1, coil=1** 로 전달되고, 하위버스(ModbusMaster_WriteCoil) 타임아웃 등으로 실패 시 Mainboard가 동일하게 **0x02**를 반환함.

또한, 설계 의도상 **RELAY1 = coil 0 = h2_dec 899**이어야 하므로, RELAY1에는 **start_addr 898**을 써야 함.  
**899는 RELAY2(coil 1)**에 대응하는 주소이므로, “RELAY1인데 899를 썼다”는 점에서 **주소(오프셋) 불일치**가 있었음.

---

## 5. 수정 내용 요약

1. **PC 주소**
   - `address_map.py`: `SUB_HPSB_COIL_BASE = 899` → **898**, `SUB_LPSB_COIL_BASE = 902` → **901**
   - RELAY1=898, RELAY2=899, RELAY3=900 / LPSB 901..909
2. **worker.py / ui_main.py / modbus_client.py**
   - 위 898/901 기반으로 주소·범위·주석 수정
3. **Mainboard**
   - **FC05 상세 로그** (GATEWAY_WRITE_DEBUG_LOG=1):
     - `[GW] FC05 recv addr=%u value=%u`
     - `[GW] checking range 892~910`
     - `[GW] mapped addr=%u -> slave=%u coil=%u` 또는 `[GW] no gateway mapping for coil %u`
     - `[GW] ApplyWrite failed for coil %u` (ApplyWrite 실패 시)

---

## 6. 수정 전/후 로그 비교

**수정 전 (RELAY1 EN, addr=899):**
- PC: `[DEBUG] Button: HPSB RELAY1 EN -> FC05 addr=899 val=1`
- PC: `[HPSB] TX FC05 addr=899 val=1 unit=9`
- PC: `[HPSB] RX EXC 0x02` / `exception_code=0x02 Illegal Data Address`

**수정 후 (RELAY1 EN, addr=898):**
- PC: `[DEBUG] Button: HPSB RELAY1 EN -> FC05 addr=898 val=1`
- PC: `[HPSB] TX FC05 addr=898 val=1 unit=9`
- 기대: `[HPSB] RX OK` (하위버스 정상 시).  
  실패 시 Mainboard에서 GATEWAY_WRITE_DEBUG_LOG=1이면  
  `[GW] FC05 recv addr=898 value=1` → `[GW] mapped addr=898 -> slave=1 coil=0` 등으로 원인 추적 가능.

---

## 7. FC05 vs FC06 구분

- **HPSB RELAY / LPSB SSR 제어**: **FC05 (Write Single Coil)**.  
  Mainboard 주소: 898..909 (coil), gateway에서 slave/coil로 변환.
- **메인보드 DO 비트맵(2101) 등**: **FC06 (Write Single Register)**.  
  하위버스가 아닌 메인보드 내부 처리.
- PC 툴과 Mainboard 모두 **RELAY/SSR는 FC05**, **레지스터 설정은 FC06**로 일치.

---

## 8. 변경된 파일 목록

| 구분 | 파일 |
|------|------|
| PC 툴 | `address_map.py`, `modbus_client.py`, `worker.py`, `ui_main.py` |
| Mainboard | `gateway_write_log.h`, `modbus_master_log.c`, `upstream_slave_h2tech.c` |
| 문서 | `HSC/docs/FC05_Address_899_Report.md` (본 문서) |

---

## 9. 확인 절차

1. PC 툴: HPSB RELAY1 EN 클릭 시 로그에 **addr=898** 출력되는지 확인.
2. Mainboard: GATEWAY_WRITE_DEBUG_LOG=1로 빌드 후, 동일 동작 시  
   `[GW] FC05 recv addr=898 value=1` → `[GW] mapped addr=898 -> slave=1 coil=0` 확인.
3. 하위버스 정상 시: `[HPSB] RX OK`, HPSB LED2(RELAY1) 점등 확인.
