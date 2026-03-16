# HPSB/LPSB 검증 UI 구성 (1차)

## 통신 구조 (중요)
- **PC는 HPSB/LPSB에 직접 연결하지 않음.** PC ↔ **메인보드만** 통신 (UART1, Modbus RTU).
- HPSB/LPSB 상태·제어는 **메인보드가 UART2 RS485로 폴링/명령한 결과**를 메인보드를 통해 PC에 보여주는 구조.

## 원칙
- **왼쪽**: 기존 Mainboard 테스트 영역 구조/배치/기능 유지 (Relay, DI, PC Status, Env Sensor).
- **오른쪽**: HPSB/LPSB 상태·제어만 추가 (검증용, 단순 구성).
- **1차**: 주소맵 확정된 항목만 구현. 미확정 항목은 UI 자리만 두고 **Reserved** 또는 **TBD** 표시.
- **로그**: [MAIN], [HPSB], [LPSB1], [LPSB2], [LPSB3] 태그로 어느 보드 요청/응답/에러인지 구분.

---

## 화면 구성

### 1) 연결 영역 (상단 전체)
- **COM Port** (드롭다운)
- **Refresh**
- **Baudrate** (고정 9600)
- **Mainboard Slave ID** (기본 9)
- **Connect** / **Disconnect**
- **Status**: Connected(녹색) / Disconnected(회색)

### 2) 왼쪽: Mainboard 테스트 (기존 유지)
- **Mainboard Outputs (Relay1~4)**: RELAY1_EN ~ RELAY4_EN 체크박스
- **Mainboard Inputs (DI_01~DI_08)**: 8개 DI LED(빨강/파랑) + "Read DI" 버튼
- **PC Status**: PC_ON_EN(500ms), PC_RESET_EN(500ms) 버튼, PC_LED_IN 표시 + "Read PC LED"
- **Env Sensor (SHTC3)**: Temp, RH, Status, Flags + "Read Sensor (5s auto)"

### 3) 오른쪽: HPSB 상태
- **HPSB (Slave 1)** 그룹박스
  - Port1, Port2, Port3: ON/OFF 표시 (FC02 1x 823~825)
  - Raw: 전류 raw 값 3개 (FC03 2000~2002)
  - Comm: OK / Timeout·CRC (error_flags bit0 = AGG_ERR_COMM_HPSB)
  - Min/Max: **TBD** (주소맵 미확정)
  - 제어: **Reserved (TBD)** (PC→HPSB 출력 제어 미확정)

### 4) 오른쪽: LPSB 상태
- **LPSB (Slave 2,3,4)** 그룹박스
  - LPSB1: Port1~3 상태 + raw
  - LPSB2: Port1~3 상태 + raw
  - LPSB3: Port1~3 상태 + raw
  - Comm: OK / Timeout·CRC (error_flags bit1 = AGG_ERR_COMM_LPSB, slave 2/3/4 공통)
  - Min/Max: **TBD**

### 5) 오른쪽: 제어
- **Read once (HPSB/LPSB)**: FC03 2000 count=14 + FC02 822 count=14 + error_flags 한 번 읽기
- **Auto poll (2s)**: 체크 시 2초 주기로 위 읽기 반복
- **LPSB SSR 펄스 (FC05)**: 버튼 5개
  - LPSB1 CH3, LPSB2 CH1, LPSB2 CH2, LPSB2 CH3, LPSB3 CH1 (VB 8~12, coil addr 891~895)

### 6) 로그 영역 (하단 전체)
- **태그**: [MAIN], [HPSB], [LPSB1], [LPSB2], [LPSB3] — 요청/응답/에러가 어느 보드인지 구분.
- TX/RX 로그 (기존 + FC03 2000, FC02 822/868, FC05 891~895)
- timeout / CRC / exception 시 RX EXC 0xNN + 태그
- 오른쪽 패널 Comm: "Comm: Timeout/CRC (1=HPSB 2=LPSB)"

---

## Modbus 주소 (메인보드 기준)
| 용도 | FC | 주소 | 비고 |
|------|-----|------|------|
| HPSB/LPSB sense | FC03 | 2000, count=14 | HPSB[3], LPSB1[3], LPSB2[3], LPSB3[3], reserved[2] |
| Sub coil 상태 | FC02 | 822, count=14 | 1x 823~836 (ONOFF_3~14) |
| Sub 알람 | FC02 | 868, count=12 | 1x 869~880 (ALM_1~12) |
| error_flags | FC03 | 2110, count=3 | reg[2] = bit0 HPSB comm, bit1 LPSB comm |
| LPSB 펄스 | FC05 | 891~895 | VB 8~12 (보드에서 펄스 출력) |

---

## Slave ID (메인보드 UART2 → HPSB/LPSB)
- **PC는 항상 메인보드(unit=9)와만 통신.** PC가 보내는 FC05는 메인보드 **coil 주소 891~895**로 전달됨.
- 메인보드 펌웨어가 UART2 RS485로 하위 보드에 보낼 때 사용하는 Slave ID (io_map.h 기준):
  - **HPSB = 1**, **LPSB1 = 2**, **LPSB2 = 3**, **LPSB3 = 4**
- 하드웨어가 2/4/8 등 다른 주소를 쓰면 펌웨어 `SLAVE_ID_*` 수정 필요.

---

## HPSB RELAY 제어
- **PC → HPSB 출력 제어는 현재 미지원.** 메인보드 H2 맵에 1x0823~0825(HPSB CH1~3)는 **READ 전용**.
- RELAY1/2/3 버튼은 **UI 토글(빨강/파랑)만** 하며, 하드웨어에는 전달되지 않음. 필요 시 메인보드에 FC05 쓰기 경로 추가 필요.

---

## RELAY/SSR 버튼 디버그 로그
- **HPSB RELAY 클릭**: `[DEBUG] Button pressed: HPSB RELAYx EN — PC→HPSB write not supported (UI only)` 로그 출력.
- **LPSB SSR 클릭**: `[DEBUG] Button pressed: LPSB SSRx EN -> FC05 addr=89X (unit=Mainboard)` 후 **TX FC05 addr=89X val=1 unit=9**, **RX OK** 또는 **RX EXC/ERR**, **Response: OK/Fail** 로그 출력.
- TX/RX/Response는 모두 `_last_log_tag`([LPSB1] 등)와 함께 기록됨.

---

## 메인보드 측 (참고)
- **DE/RE (PB12)**: UART2 하위 송신 시 `set_de_tx()` → 전송 → 2ms 대기 → `set_de_rx()` (modbus_master.c `DE_RX_GUARD_MS`)로 마지막 바이트가 나간 뒤 RX 전환.
- LPSB SSR(FC05 891~895) 수신 시 메인보드가 `Gateway_Action_PulseOutputByOnOffIndex` → `ModbusMaster_WriteCoil(slave_id, coil_index, value)`로 해당 하위 보드에 FC05 전송.

---

## 파일 변경 요약
- `app/address_map.py`: SUB_SENSE_REG, SUB_COIL_STATUS_START, SUB_ALARM_*, SUB_VB_COIL_*, ERROR_FLAGS_REG
- `app/modbus_client.py`: read_sub_sense, read_sub_coil_status, read_sub_alarms, read_error_flags, write_sub_coil_pulse
- `app/worker.py`: sub_data_result 시그널, on_request_read_sub, on_request_sub_pulse
- `app/ui_main.py`: 오른쪽 패널 (HPSB/LPSB/제어), request_read_sub, request_sub_pulse, _on_sub_data_result, _on_auto_poll_changed
