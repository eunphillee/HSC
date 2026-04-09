# HPSB / LPSB 실측 검증 · 튜닝 · HPSB 고도화 계획

> **정책 요약**  
> - **LPSB**: 현재 Guro 구조(`lpsb_ct_adc.c` + Modbus Input)로 **먼저 실측 검증**.  
> - **HPSB**: 동일하게 **먼저 검증**; 판정이 불안정하면 `HPSB_TEST`의 `ct_current.c` 계열(RMS/오프셋/히스테리시스)을 **단계적으로 이식**.

---

## 1. 실측 테스트 체크리스트

### 1.1 공통 준비

| # | 항목 | 확인 |
|---|------|------|
| A1 | 전원·GND·RS485 A/B·종단 저항(필요 시) | ☐ |
| A2 | 메인보드 ↔ HPSB/LPSB 배선(슬레이브 ID: HPSB=1, LPSB=2/4/8) | ☐ |
| A3 | 펌웨어: Guro_HPSB / Guro_LPSB / Guro_Mainboard 최신 빌드 플래시 | ☐ |
| A4 | PC 툴: Mainboard 경로에서 FC03 `2000`×`40` 또는 HPSB/LPSB UI 자동 폴링 가능 | ☐ |

### 1.2 LPSB (우선 검증)

| # | 절차 | 기대 |
|---|------|------|
| L1 | **무부하**: 3채널 SSR OFF, Input Reg 1~3(AVG)는 대략 미드레일 근처, 7~9(CURRENT)=0 | ☐ |
| L2 | **채널별 부하 ON**: SSR1만 ON → CH1 PKPK·CURRENT 반응, CH2/3는 부하 없으면 낮게 유지 | ☐ |
| L3 | 부하 제거 후 CURRENT가 **OFF로 복귀**하는지(지연/오동작 없음) | ☐ |
| L4 | **Direct LPSB** 모드에서 FC04 `start=0,count=10`으로 PC에서 직접 읽어 값과 UI 일치 | ☐ |
| L5 | 메인보드 경유 시 `aggregator`·ALM8~10(해당 LPSB) **오동작 없음** | ☐ |

### 1.3 HPSB (두 번째 검증)

| # | 절차 | 기대 |
|---|------|------|
| H1 | **RELAY OFF·무부하**: Input 1~3 AVG 안정, 7~9 CURRENT=0 | ☐ |
| H2 | 릴레이·부하 **연결 상태**에서 FC05/통신과 동시에 전류 표시가 **깨지지 않음**(이전 RS485 이슈와 구분) | ☐ |
| H3 | 채널별 부하 ON 시 **해당 채널만** PKPK/CURRENT 반응 | ☐ |
| H4 | 메인보드 FC03 `2000` 블록에서 HPSB 구간(오프셋 0~8)이 PC와 일치 | ☐ |
| H5 | ALM5~7(또는 집계 알람)이 **의도한 debounce** 후에만 올라감 | ☐ |

### 1.4 Pass/Fail 기준(권장)

- **PASS**: 무부하에서 CURRENT=0 유지, 정상 부하에서 CURRENT=1, 제거 후 짧은 시간 내 0 복귀(노이즈 한두 번은 로그로만 확인).  
- **조정 필요**: 무부하에서 CURRENT가 자주 1이 되거나, 부하 시에도 0으로 고정 → **§2 튜닝** 후 재측정.  
- **HPSB만 지속 불안정**: **§3 단계** 이식 검토.

---

## 2. Threshold / Sample count 조정 포인트

### 2.1 서브보드 펌웨어 (Guro)

| 파일 | 매크로 | 의미 | 기본 |
|------|--------|------|------|
| `Guro_HPSB/Application/Src/hpsb_ct_adc.c` | `HPSB_CT_ADC_SAMPLES` | 채널당 연속 샘플 수(AVG·PKPK 동일 버스트) | 48 |
| ↑ | `HPSB_CT_ADC_UPDATE_MS` | ADC 갱신 최소 간격(ms) | 100 |
| ↑ | `HPSB_CT_PKPK_ON_THRESHOLD` | PKPK ≥ 이 값이면 CURRENT=1 | 64 |
| `Guro_LPSB/Application/Src/lpsb_ct_adc.c` | `LPSB_CT_ADC_SAMPLES` | 동일 | 48 |
| ↑ | `LPSB_CT_ADC_UPDATE_MS` | 동일 | 100 |
| ↑ | `LPSB_CT_PKPK_ON_THRESHOLD` | 동일 | 64 |

**튜닝 가이드**

- **무부하인데 CURRENT가 ON**: `*_PKPK_ON_THRESHOLD` **상향**(예: 64→96~128).  
- **부하인데 CURRENT가 OFF**: **하향** 또는 `*_CT_ADC_SAMPLES` **소폭 증가**(노이즈 평균화).  
- **응답이 너무 느림**: `*_CT_ADC_UPDATE_MS` **하향**(부하·CPU 여유 시).  
- **샘플링 타임**: `ADC_SAMPLETIME_55CYCLES_5` → `71.5` 등 `LPSB_TEST`에 가깝게(신호 임피던스/RC에 따라).

### 2.2 메인보드 (알람 디바운스)

| 위치 | 상수 | 의미 |
|------|------|------|
| `Guro_Mainboard/Application/Src/aggregator.c` | `SUB_CURRENT_DEBOUNCE_CYCLES` | 서브 Input 7~9 CURRENT가 연속 몇 번 이상이어야 알람 비트 반영 | 3 |

PC/상위 표시만 보면 서브보드 CURRENT가 즉시 바뀌고, 메인 알람만 늦게 뜨는 것은 **정상**일 수 있음.

---

## 3. HPSB 불안정 시 — HPSB_TEST 기준 단계별 이식 계획

참조 소스: `HPSB_TEST/CT_Current/Src/ct_current.c`, `HPSB_TEST/CT_Current/Inc/ct_current.h`

### 단계 0 — 현 상태 유지

- Guro `hpsb_ct_adc.c`의 **PKPK 단일 임계값**만으로 재튜닝(§2).  
- 불필요한 코드 증가 없이 대부분의 현장 이슈는 여기서 해결되는지 확인.

### 단계 1 — 오프셋(무부하 DC) 수집

- **이식**: `CT_Offset_Collect`, `s_offset[]`, `CT_OFFSET_COLLECT_MS` / `CT_OFFSET_SAMPLE_MS` 개념.  
- **목적**: RMS 계산 전 **AC 성분** 기준으로 안정화(미드레일 드리프트 완화).  
- **산출물**: 채널별 `offset` → 이후 RMS/PKPK는 `(sample - offset)` 기반으로 선택 적용.

### 단계 2 — RMS 기반 판정 추가

- **이식**: `CT_ReadChannelRMS` 또는 동일 수식 — `sqrt( mean( (s[i]-offset)^2 ) )` 를 **rms_adc**로.  
- **판정**: 현재처럼 PKPK만이 아니라 **`rms_adc >= CT_CUR_RMS_ON_THRESHOLD`** 등 **TEST의 `CT_CUR_RMS_*` 상수**를 Guro에 매크로로 이식.  
- **Modbus**: 필요 시 Holding/Input에 **RMS 레지스터** 추가(현재는 AVG/PKPK/CURRENT만 노출).

### 단계 3 — ON/OFF 히스테리시스(연속 N회)

- **이식**: `CT_CurrentDetect_ADC1` 패턴의 **on_candidate / off_candidate** 및 `CT_CUR_CONSECUTIVE`.  
- **조건 예**: TEST처럼 `(RMS≥ON) OR (PKPK≥ON)` 와 `(RMS≤OFF) AND (PKPK≤OFF)` 조합.  
- **목적**: 맥락 없이 임계값만 건드리기 어려움 **깜빡임** 제거.

### 단계 4 — 채널 읽기 순서·더미 스캔

- **이식**: `read_three_channels()` 의 **더미 스캔 1회 + CH3→4→5 순 Poll** (크로스토크·캐패시터 정착).  
- **주의**: Guro는 **채널별 ConfigChannel** 방식이라 TEST와 타이밍이 다름; 이식 시 **한 채널 루프**를 **3채널 동시 스캔 루프**로 바꾸는 리팩터가 필요할 수 있음.

### 단계 5 — 캘리브레이션(A)

- **이식**: `CT_ADC_RMS_to_Ampere`, `CT_SetCalibration` — **A 단위 표시**가 필요할 때.  
- Modbus는 **mA×10** 등으로 확장할지 별도 설계.

### 권장 순서

1. **단계 0**으로 재튜닝.  
2. 여전히 무부하 오탐/부하 미검 → **단계 1 + 2** (오프셋 + RMS).  
3. 여전히 경계에서 깜빡임 → **단계 3**.  
4. 채널 간 간섭 의심 시 → **단계 4**.  
5. 상위 시스템이 **전류(A)** 를 요구하면 → **단계 5**.

---

## 관련 문서

- `Guro_Mainboard/CURRENT_REG_MAP.md` — 레지스터 맵·PC 블록(2000×40).  
- `HPSB_TEST/CT_Current/CT_전류_환산_설명.md`, `HPSB_TEST/HPSB_TEST_결과요약.md` — TEST 기준 상세.
