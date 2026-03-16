# CT 전류 환산: ADC raw → RMS → 전류(A)

## 1. ADC raw → RMS → Ampere 계산 로직

### 1.1 흐름 요약

```
[부팅] → 3초간 무부하 ADC 샘플 수집 → DC 오프셋(offset) 계산
         ↓
[1초 주기] → 64회 ADC 샘플 (1ms 간격) → offset 제거 → AC RMS(ADC LSB) → I = k × RMS → PC 전송
```

### 1.2 무부하 오프셋(Offset)

- **목적**: CT + 1.65V 바이어스 회로에서 “전류 없을 때” ADC 중심값을 잡기 위함.
- **방식**: 부팅 후 **3초(CT_OFFSET_COLLECT_MS)** 동안 **50ms(CT_OFFSET_SAMPLE_MS)** 간격으로 3채널 ADC를 읽고, 채널별 평균을 오프셋으로 저장.
- **가정**: 부팅 직후 3초는 부하(히터)가 꺼져 있다고 보고, 이 구간 평균을 DC 오프셋으로 사용.
- **보정**: 수집된 오프셋이 500~3700 LSB(약 0.4V~3.0V) 밖이면 **2048(1.65V)** 로 고정. 샘플이 하나도 없으면 전 채널 2048 사용. (오프셋 0이면 RMS 과대 → 전류 19~22A처럼 나오는 현상 방지.)

### 1.3 RMS 계산

- **샘플 수**: **64개(CT_RMS_SAMPLES)**, 샘플 간 **1ms(CT_RMS_SAMPLE_DELAY_MS)** → 약 64ms 구간 ≈ 50Hz 기준 **3.2 사이클**.
- **목적**: 정밀 계측기 수준이 아니라, “히터 ON/OFF에 따라 4.7A 근처가 나오는지” 검증용.
- **공식**:
  - `ac[i] = sample[i] - offset`
  - `sum_sq = Σ(ac[i]²)`
  - `rms_adc = √(sum_sq / N)` (N=64, ADC LSB 단위)

### 1.4 전류(A) 환산

- **식**: `I [A] = cal_k × rms_adc`
- **cal_k**: 채널별 캘리브레이션 계수. `CT_SetCalibration(ch, k)` / `CT_GetCalibration(ch)` 로 설정·조회.
- **초기값**: ADC1만 전류 사용, **k=0.017** (1050W/220V=4.77A, A1_RMS≈280 기준). ADC2/ADC3는 전류 미사용(unused).

---

## 2. C 코드 구조 (요약)

| 파일 | 역할 |
|------|------|
| `CT_Current/Inc/ct_current.h` | 채널 수, 샘플 수/간격 상수, API 선언 |
| `CT_Current/Src/ct_current.c` | 오프셋 수집, RMS 계산, I = k×rms 변환 |
| `Core/Src/main.c` | 부팅 후 `CT_Offset_Collect()` 호출, 1초마다 `CT_ReadChannelRMS()` → `CT_ADC_RMS_to_Ampere()` → RS485 문자열 출력 |

### 2.1 주요 API

- `CT_IsOffsetDone()`: 오프셋 수집 완료 여부 (1이면 RMS/전류 계산 가능).
- `CT_Offset_Collect(hadc)`: 매 루프에서 호출. 3초 동안 50ms 간격으로 샘플 수집 후 오프셋 계산.
- `CT_ReadChannelRMS(hadc, ch, &rms_adc, &raw_avg)`: 채널 `ch`(0~2)에 대해 64샘플 RMS 및 평균 raw 반환.
- `CT_ADC_RMS_to_Ampere(rms_adc, cal_k)`: `I = cal_k * rms_adc` 계산.
- `CT_SetCalibration(ch, k)` / `CT_GetCalibration(ch)`: 채널별 `cal_k` 설정/조회.

---

## 3. 캘리브레이션 방법

### 3.1 목표

- 히터 **ON** 시 실제 전류 **4.77A**(1050W / 220V)에 맞추기.

### 3.2 절차

1. **히터 ON** 상태에서 PC 로그로 `I1=…A`, `A1_RAW`(또는 RMS에 대응하는 raw 평균) 확인.
2. 현재 `rms_adc` 값은 `CT_ReadChannelRMS()`가 반환하는 `rms_adc`와 동일 (로그에 RMS를 찍어두었다면 그 값 사용).
3. **목표 전류** `I_target = 4.77f`, **실제 측정 RMS** `rms_measured` 일 때:
   - `k_new = I_target / rms_measured`
4. 펌웨어에서 `CT_SetCalibration(0, k_new)` 호출 (예: `main()` 초기화 구간 또는 한 번만 호출되는 곳).
   - 또는 `ct_current.c`의 `s_cal_k[0]` 초기값을 `k_new`로 변경 후 재빌드.

### 3.3 예시

- 히터 ON 시 `rms_adc = 398` 이고, 현재 `k=0.012` → `I = 0.012 × 398 ≈ 4.78A` 이면 이미 적당함.
- 만약 `I = 5.2A`처럼 크게 나오면: `k_new = 4.77 / (5.2/0.012) ≈ 0.011` 로 줄이면 됨 (또는 측정한 rms_adc로 `k_new = 4.77 / rms_adc`).

---

## 4. PC 로그 예시 출력 형식

```
HPSB_TEST,SEQ=100,R1=1,R2=0,R3=0,A1_RAW=2314,A1_RMS=281,K=0.0170,I1=4.78A,A2_RAW=4062,I2=unused,A3_RAW=4062,I3=unused
```

- **SEQ**: 1초마다 증가하는 시퀀스 번호.
- **R1,R2,R3**: 릴레이 상태 (0/1).
- **A1_RAW**: ADC1(CT) 구간 평균 raw. **A1_RMS**: offset 제거 후 RMS(ADC LSB). **K**: 캘리 계수(0.0119 등). **I1**: 환산 전류 [A].
- **A2_RAW, A3_RAW**: ADC2/ADC3 raw (CT 미연결 시 참고용). **I2, I3**: `unused` (전류 계산 제외).

---

## 5. HCT17W + R19(18Ω) + 1.65V 회로 참고

- CT 2차 전류 → R19(18Ω) burden → 전압 → 1.65V 바이어스와 합성 후 ADC(PA3/4/5) 입력.
- ADC는 12bit, Vref 3.3V 기준. 1.65V ≈ 2048 LSB.
- **BAT54 제거** 후에도 오프셋 수집은 “무부하 시 ADC 평균”으로 동작하며, RMS는 이 오프셋을 빼서 AC 성분만으로 계산.

---

## 6. 샘플 개수·샘플링 방식 요약

| 항목 | 값 | 비고 |
|------|-----|------|
| 오프셋 수집 시간 | 3초 | CT_OFFSET_COLLECT_MS |
| 오프셋 샘플 간격 | 50ms | CT_OFFSET_SAMPLE_MS |
| RMS 샘플 수 | 64 | CT_RMS_SAMPLES |
| RMS 샘플 간격 | 1ms | CT_RMS_SAMPLE_DELAY_MS |
| 50Hz 기준 구간 | 약 64ms | ≈ 3.2 사이클 |

- 60Hz 지역이면 64ms ≈ 3.8 사이클. “대략 전류값 검증” 목적에는 충분.

---

## 7. 빌드 시 참고 (sqrtf)

- `ct_current.c`에서 `sqrtf()` 사용. **undefined reference to `sqrtf'** 가 나오면:
  - **STM32CubeIDE**: Project → Properties → C/C++ Build → Settings → MCU GCC Linker → Libraries → Libraries (-l) 에 `m` 추가.
  - 또는 Linker 스크립트/컴파일러 옵션에 `-lm` 추가.
