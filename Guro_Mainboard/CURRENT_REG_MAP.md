# Current Register Map — HSC Gateway

Per-port **AVG (ADC 평균)**, **PKPK (피크투피크)**, **CURRENT (0/1)** 는 HPSB/LPSB에서 FC04 Input Reg로 제공되고, MAIN이 폴링 후 PC에 FC03 4x2000 블록으로 노출한다.

---

## 1. 값 정의

- **AVG:** 채널당 N회 샘플 산술평균 (12비트 ADC 스케일 0..4095).
- **PKPK:** 같은 버스트에서 max−min.
- **CURRENT:** 보드에서 `PKPK >= 임계값` 이면 1, 아니면 0 (임계값: `HPSB_CT_PKPK_ON_THRESHOLD` / `LPSB_CT_PKPK_ON_THRESHOLD`, 기본 64).

---

## 2. Downstream (HPSB/LPSB → MAIN)

### 2.1 HPSB (Slave ID = 1)

| Input Reg (3x) | 이름 | 설명 |
|----------------|------|------|
| 0 | DI image | 8비트 discrete 이미지 |
| 1..3 | CT_CHx_AVG | 포트 1..3 AVG |
| 4..6 | CT_CHx_PKPK | 포트 1..3 PKPK |
| 7..9 | CT_CHx_CURRENT | 포트 1..3 전류 감지 0/1 |

**FC04** start=0, count=**10**.

### 2.2 LPSB (Slave ID = 2, 4, 8)

동일 레이아웃 (ACS712 등 동일 ADC 핀).

**FC04** start=0, count=**10**.

### 2.3 MAIN polling

- HPSB / LPSB1 / LPSB2 / LPSB3: 각각 **FC04 start=0, count=10**.
- 집계: `hpsb_sense_raw[]` = AVG, `hpsb_pkpk[]`, `hpsb_current_st[]` (LPSB 동일).

---

## 3. Upstream (MAIN → PC)

**블록:** 4x**2000** .. 4x**2027** — Modbus start **2000**, count **40**, **FC03**, 읽기 전용.

요청은 **start=2000, count=40** 만 허용 (그 외는 exception).

레이아웃 (워드 오프셋 0 기준):

| 오프셋 | 내용 |
|--------|------|
| 0..2 | HPSB AVG ch1..3 |
| 3..5 | HPSB PKPK ch1..3 |
| 6..8 | HPSB CURRENT ch1..3 |
| 9..17 | LPSB1 AVG/PKPK/CURRENT (각 3워드씩) |
| 18..26 | LPSB2 |
| 27..35 | LPSB3 |
| 36..39 | 예약 (0) |

---

## 4. 테스트 (부하 ON/OFF)

1. 부하 없음: 각 채널 **CURRENT=0**, PKPK 낮음.
2. 부하 연결: PKPK 상승 → **CURRENT=1** (임계값은 노이즈에 맞게 펌웨어 매크로 조정).
3. PC 툴: Mainboard 경로에서 FC03 2000/40 또는 HPSB/LPSB UI의 `AVG/PKPK/I` 표시 확인.
4. Direct LPSB: **FC04** slave=2, start=0, count=10.

---

## 5. 구현 파일

| 보드 | 변경 |
|------|------|
| HPSB | `hpsb_ct_adc.c`, `io_map.h` INPUT 10개, `modbus_table.c`, `main.c` |
| LPSB | `lpsb_ct_adc.c`, 동일 패턴 |
| MAIN | `io_map.h` MODBUS_INPUT_REG_COUNT=10, `modbus_table.c` 폴링, `aggregator.c`, `upstream_slave_h2tech.c`, `aggregated_status.h` |
| PC | `address_map.py` SUB_SENSE_COUNT=40, `ui_main.py`, `modbus_client.py` |
