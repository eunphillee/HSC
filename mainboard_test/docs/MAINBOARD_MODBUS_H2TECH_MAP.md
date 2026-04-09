# Mainboard Modbus H2Tech 맵 (1차)

## 통신 조건
- Modbus RTU, 9600-8N1
- Mainboard Slave ID 기본값: `1`
- PC는 Mainboard만 요청, HPSB/LPSB는 Mainboard 내부에서 제어/집계

## 문서 주소와 실제 프레임 주소
- 문서 표기: `1x0821`, `1x0892` 같은 사람이 읽는 주소
- 실제 Modbus PDU: `0-based offset`
- 변환 함수:
  - `doc1x_to_offset(doc_addr)`
  - `doc4x_to_offset(doc_addr)`

예시:
- `1x0821 -> DI offset 0`
- `1x0892 -> Coil offset 0` (coil 기준)

## 지원 Function Code (1차)
- FC01 Read Coils
- FC02 Read Discrete Inputs
- FC03 Read Holding Registers
- FC04 Read Input Registers (진단 확장: 4x4000~)
- FC05 Write Single Coil
- FC06 Write Single Holding Register
- FC15 Write Multiple Coils
- FC16 Write Multiple Holding Registers

## 1차 핵심 주소

### DI/Coil (문서 1x)
- `1x0821~0836` 상태 비트 읽기 (FC02)
- `1x0853~0860` 자동문 센서/외부 스위치 읽기 (FC02)
- `1x0869~0880` 정상/이상 비트 읽기 (FC02)
- `1x0892~0910` 제어 Coil (FC01/05/15)

### Register
- `4x2000~2013` 집계 센서 raw (FC03)
- `4x2100~2101` Main DI/DO bitmap (FC03/FC06/FC16)
- `4x2110~2112` 환경/오류 플래그 (FC03)
- `4x3000~3002` slave/baud/factory reset (FC03/FC06/FC16)
- `4x4000~4011` 진단 Input Register (FC04)

## Coil bit packing 규칙
- FC01/FC02/FC15는 Modbus 표준대로 `LSB-first`
- 첫 번째 coil/input가 응답 byte의 bit0

## Exception code
- `0x01` Illegal Function
- `0x02` Illegal Data Address
- `0x03` Illegal Data Value
- `0x04` Slave Device Failure

## 하위보드 연동
- Mainboard `Gateway_Action_WriteSubCoil()`로 HPSB/LPSB 제어
- 실패 시 `downstream_write_fail` 알람 반영

## 온라인/오프라인 판정
- `aggregated_status.error_flags` 기반
- `AGG_ERR_COMM_HPSB`, `AGG_ERR_COMM_LPSB`로 상태 노출

## Pulse coil 정책
- 문열림/가상버튼 계열은 pulse 액션(기본 300ms)
- `value=ON`에서만 동작하고, read 시 latch 보장하지 않음

## 예제 프레임
- FC02(입력 상태 읽기): `01 02 03 34 00 08 ...CRC`
- FC05(단일 coil 제어): `01 05 03 7D FF 00 ...CRC`
- FC04(진단값 읽기 4x4000): `01 04 0F A0 00 0C ...CRC`
