# HPSB_TEST

HPSB 보드의 **기본 하드웨어 동작만** 독립 검증하는 테스트 프로젝트입니다.  
**Modbus는 사용하지 않고**, RS485로 PC에 상태 문자열만 주기 전송합니다.

- **MCU**: STM32F030K6T6  
- **통신**: RS485 half duplex, 9600 8N1  
- **동작**: 1초마다 한 줄 ASCII 상태 문자열 전송, 릴레이 3개 순차 테스트, ADC 3채널 값 포함  

### 송신 문자열 단계적 확대 (HPSB_TEST_TX_STAGE)

긴 문자열 수신이 안 될 때 **어느 길이부터** 문제인지 보려면 `main.c` 상단 `HPSB_TEST_TX_STAGE`를 1~5로 바꿔가며 테스트합니다.  
PC 툴은 **Direct HPSB** 연결 시 `\r\n`만 보고 한 줄씩 표시합니다 (raw 라인 모드).

| 스테이지 | 송신 예시 |
|----------|-----------|
| 1 | `ADC=2314\r\n` |
| 2 | `A1=2314,A2=4058,A3=4058\r\n` |
| 3 | `A1=2314,RMS=280\r\n` |
| 4 | `A1=2314,RMS=280,I1x100=477\r\n` |
| 5 | `MS=...,HPSB_TEST,SEQ=...,R1=...,A1_RAW=...,I1x100=...,A2_RAW=...,A3_RAW=...\r\n` (접두어 `<MSG>` 없음) |
| 0 | 전체 포맷 `<MSG>MS=...,HPSB_TEST,...,I1x100=...,...\r\n` |

- float 출력 없음: 전류는 `I1x100=477` 형식 (4.77A → 477).

### 전류 유무 판정 (HPSB_TEST_CUR_DETECT) — PA3 단일채널

`main.c`에서 `HPSB_TEST_CUR_DETECT`를 `1`로 두면 **PA3(TC_ADC01) 단일채널만** 64샘플 읽어 1초마다 전송합니다.  
**PA4, PA5는 전류감지 경로에서 사용하지 않습니다.**

- **PA3_AVG**: 64샘플 평균 (PA3 raw, ADC LSB)
- **PA3_MIN / PA3_MAX**: 64샘플 중 최소/최대 (PA3 raw)
- **PA3_RMS**: AC 성분 RMS (PA3 기준)
- **PA3_PKPK**: max − min (PA3 기준)
- **CUR**: RMS/PKPK 임계값 + **2회 연속** 조건 (노이즈 방지). 실측 후 `ct_current.h`에서 재조정.

로그 예: `PA3_AVG=2445,PA3_MIN=2100,PA3_MAX=2780,PA3_RMS=120,PA3_PKPK=680,CUR=ON`  
임계값: `ct_current.h`의 `CT_CUR_RMS_ON_THRESHOLD`, `CT_CUR_PKPK_ON_THRESHOLD`, `CT_CUR_RMS_OFF_THRESHOLD`, `CT_CUR_PKPK_OFF_THRESHOLD`, `CT_CUR_CONSECUTIVE`.

### PA3 단일채널 전류감지 수정 요약 (구조 정리)

- **수정된 파일**
  - `Core/Inc/adc_config.h` (신규): PA3 전용/3채널 복원 함수 선언
  - `Core/Src/main.c`: ADC 캘리브레이션 추가, `ADC_ConfigForPA3Only` / `ADC_ConfigRestoreThreeChannels` 구현, 로그 포맷 PA3_AVG/MIN/MAX/RMS/PKPK/CUR
  - `CT_Current/Inc/ct_current.h`, `Core/Inc/ct_current.h`: `CT_CurrentDetect_ADC1`에 `min_out`/`max_out` 인자 추가
  - `CT_Current/Src/ct_current.c`: `read_single_channel_pa3()` 추가, 전류감지 시 PA3 단일채널만 사용 후 3채널 복원

- **핵심 변경**
  - **측정 루프에서 DeInit/Init/Calibration 사용 안 함** (STM32F0에서 반복 시 4095 고정 등 불안정). 전류감지 시 **ConfigChannel만** CH3(PA3)으로 변경 → 64샘플 수집 → ConfigChannel만으로 3채널 복원.
  - ADC 초기화는 **시작 시 1회만**: `MX_ADC_Init()` + `HAL_ADCEx_Calibration_Start(&hadc)`.
  - 로그에 **ADC_RAW** (첫 샘플 raw) 포함: `ADC_RAW=%u,PA3_AVG=...,PA3_MIN=...,PA3_MAX=...,PA3_RMS=...,PA3_PKPK=...,CUR=...`. 정상 시 ADC_RAW는 약 2000~2600 범위.
  - PA3 GPIO: `GPIO_MODE_ANALOG`, `GPIO_NOPULL` (stm32f0xx_hal_msp.c). PA3 = ADC_CHANNEL_3 (STM32F030).

- **테스트 방법**
  1. HPSB_TEST 빌드 후 보드 플래시, PC 툴 Direct HPSB로 연결.
  2. 히터 OFF → 1초마다 한 줄 로그에서 `PA3_AVG`, `PA3_MIN`, `PA3_MAX`, `PA3_PKPK`, `CUR` 확인.
  3. 히터 ON → 동일 로그로 ON 시 값 변화 확인.
  4. 오실로로 PA3(TC_ADC01) 전압을 측정한 뒤, `PA3_AVG`와 전압 환산 `V=(PA3_AVG/4095)*VDDA` 비교.

- **threshold 재설정 시 참고**
  - 히터 OFF 구간 로그에서 **PA3_PKPK** 대표값(예: 80 근처) 확인.
  - 히터 ON 구간에서 **PA3_PKPK** 대표값(예: 80 이상) 확인.
  - `ct_current.h`의 `CT_CUR_PKPK_ON_THRESHOLD`, `CT_CUR_PKPK_OFF_THRESHOLD`를 위 실측 구간 사이로 설정 (ON ≥ X, OFF ≤ Y, X > Y로 히스테리시스 유지).

---

### ADC 채널·전압 검증 (ADC raw vs 오실로 전압)

- **TC_ADC01 = PA3 = ADC Channel 3** (CubeMX: `PA3.Signal=ADC_IN3`).  
  `main.c`의 `MX_ADC_Init()`에서 **CH3 → CH4 → CH5** 순으로 `HAL_ADC_ConfigChannel` 설정.  
  스캔 시 **첫 번째 변환 결과 = CH3 = TC_ADC01** → 로그의 `ADC1_AVG`는 이 채널의 raw 평균.
- **ADC1_AVG**: 64샘플 **raw** 평균 (오프셋 제거·RMS 보정 없음). CUR 판정용 AVG/RMS/PKPK 모두 **같은 raw 샘플**에서 계산.
- **전압 환산**: `V = (raw / 4095) × VDDA`.  
  **VDDA(ADC 기준전압)** 가 3.3V가 아니면 숫자가 오실로와 다르게 보일 수 있음.  
  예: 로그 `ADC1_AVG≈3978`인데 오실로는 **약 1.76V**이면 → **VDDA ≈ 1.8V**일 때 `3978/4095×1.8 ≈ 1.75V`로 일치.  
  즉 MCU ADC 값은 **VDDA 기준**으로 맞게 읽는 것이고, 보드의 **실제 VDDA**(1.8V/3.3V 등)를 확인한 뒤 위 식으로 비교하면 됨.

---
- 스테이지 1부터 순서대로 올리면서 PC에 한 줄씩 나오는지 확인하면, 문제가 되는 최소 길이를 찾을 수 있습니다.

---

### 신호가 안 올 때 (PC 로그에 [RX]가 안 나올 때)

1. **포트 확인**: PC 툴에서 선택한 포트가 **HPSB 보드가 연결된 포트**인지 확인하세요. 메인보드용 포트(예: wlan-debug)와 HPSB 전용 포트(예: USB-Serial 어댑터)가 다를 수 있습니다.
2. **보드 펌웨어**: HPSB 보드에 **HPSB_TEST** 프로젝트를 빌드·다운로드했는지 확인하세요. (다른 펌웨어는 1초마다 `TEST\r\n`을 보내지 않습니다.)
3. **단순 송신 모드**: `main.c` 상단 `HPSB_TEST_SIMPLE_TX` 가 `1`이면 1초마다 `TEST\r\n` 만 전송합니다. PC 툴은 **Direct HPSB** 체크 후 연결하면 raw 수신을 폴링합니다.
4. **다른 터미널로 확인**: 같은 포트를 터미널에서 열어 수신이 오는지 확인해 보세요.  
   `screen /dev/cu.xxxx 9600` (macOS) 또는 CoolTerm 등으로 9600 8N1 연결 후 1초마다 `TEST` 가 보이면 보드 송신은 정상이고, PC 툴 쪽만 점검하면 됩니다.

---

## 🔌 최소 안전 모드 (전원+LED+스위치만 연결 시)

RS485/ADC/릴레이 부품을 제거한 보드에서도 전류가 크게 나오거나 발열할 때 사용합니다.

- **`main.c` 상단**: `#define HPSB_TEST_MINIMAL_SAFE_MODE 1` 로 두면 동작합니다.
- **동작**: UART/ADC 초기화 없음. PA0~PA5, PA9, PA10, PA11은 **전부 GPIO 입력(구동 없음)**. LED 4개와 스위치(ID) 핀만 사용. 메인 루프는 **LED01만 1초마다 점멸**.
- **목적**: MCU가 문제인지, 특정 핀 구동이 원인인지 구분. 이 모드에서도 뜨거우면 이미 손상된 칩이거나 보드 단락 가능성이 큽니다. 이 모드에서 정상(전류 소폭, 발열 없음)이면 기존 펌웨어가 구동하던 핀(RS485/릴레이/ADC) 쪽 회로나 배선을 의심하세요.
- **일반 모드로 되돌리기**: `#define HPSB_TEST_MINIMAL_SAFE_MODE 0` 으로 변경 후 빌드.

---

## ⚠️ MCU 보호용 클럭 (중요)

이 보드에서 **HSE(외부 크리스탈) 또는 PLL 사용 시** 크리스탈/부품 불량·배선(TX-DE 단락 등)으로 MCU가 손상된 사례가 있습니다.  
HPSB_TEST는 **내부 HSI 8MHz만** 사용하도록 되어 있습니다 (PLL/HSE 미사용).  

- **CubeMX에서 Code 재생성**하면 `SystemClock_Config()`가 다시 PLL/HSE 설정으로 덮어쓸 수 있습니다.  
  → 재생성 후 `main.c`의 `SystemClock_Config()`를 **HSI 8MHz 전용**으로 다시 수정하거나, 이 프로젝트의 `main.c`를 그대로 유지하세요.  
- CubeMX **Clock Configuration**에서 SYSCLK 소스를 **HSI**로 두고, HSE는 사용하지 않도록 설정하는 것을 권장합니다.

---

## 1. 프로젝트 만들기 (둘 중 하나)

### 방법 A: Guro_HPSB 복제 후 교체 (권장)

1. STM32CubeIDE에서 **Guro_HPSB** 우클릭 → **Copy** → **Paste** → 이름을 **HPSB_TEST**로 변경.
2. **HPSB_TEST**에서 아래 파일을 이 폴더의 내용으로 **덮어쓰기**:
   - `Core/Inc/main.h`
   - `Core/Src/main.c`
   - `Core/Src/stm32f0xx_hal_msp.c`
3. **Project → Properties → C/C++ Build → Settings**  
   - **Source**에서 `Application`, `Modbus`, `IO` 폴더를 빌드에서 제외(제거)하거나,  
   - 해당 폴더의 소스를 삭제해도 됩니다.
4. 빌드 후 보드에 다운로드.

### 방법 B: 새 프로젝트에서 생성

1. **File → New → STM32 Project** → MCU Selector에서 **STM32F030K6Tx** 선택 → 프로젝트명 **HPSB_TEST**.
2. CubeMX에서 `CUBEMX_SUMMARY.md`대로 핀·USART1·ADC·Clock 설정 후 **Generate Code**.
3. 생성된 프로젝트의 `Core/Inc/main.h`, `Core/Src/main.c`, `Core/Src/stm32f0xx_hal_msp.c`를 이 저장소의 동일 경로 파일로 교체.
4. 빌드 후 보드에 다운로드.

---

## 2. 제공 파일 요약

| 파일 | 설명 |
|------|------|
| `Core/Inc/main.h` | 핀 정의 (PA9/10/11, 릴레이, ADC, LED, ID) |
| `Core/Src/main.c` | 초기화, 1초 주기 루프, RS485 송신, 릴레이/ADC/문자열 생성 |
| `Core/Src/stm32f0xx_hal_msp.c` | ADC(PA3,4,5), USART1(PA9,10) MSP 초기화 |
| `CUBEMX_SUMMARY.md` | CubeMX 핀·USART·ADC·클럭 설정 요약 |
| **`HPSB_TEST_결과요약.md`** | **CubeMX 요약, main.c 구조, RS485_Send, ADC 읽기, 릴레이 상태머신, PC 출력 예시 (6종 일괄)** |
| `README.md` | 이 문서 |

---

## 3. main.c 구조 요약

- **초기화**: `SystemClock_Config` (HSI 8MHz), `MX_GPIO_Init`, `MX_ADC_Init`, `MX_USART1_UART_Init`.
- **RS485 송신**: `RS485_Send(buf, len)`  
  - DE HIGH → 짧은 딜레이 → `HAL_UART_Transmit` → TC 플래그 대기 → DE LOW.
- **릴레이**: `s_relay_phase` 0~3에 따라 1초마다 R1만 ON → R2만 ON → R3만 ON → 전부 OFF 순서로 반복.
- **ADC**: `ADC_ReadThreeChannels(adc[])`로 PA3/PA4/PA5 12bit 값 읽어 문자열에 포함.
- **LED 순서 (Low=점등)**  
  - **처음 3초**: LED01만 500ms마다 깜빡임 → **LED1이 3번 깜빡인 것처럼 보임** (오프셋 수집 중).  
  - **3초 이후** (1초마다 한 단계):  
    - **LED01**은 매 초 토글(heartbeat)이라 1초마다 켜졌다 꺼졌다 반복.  
    - **LED02 → LED03 → LED04 → 전부 끔** 순서로 켜짐 (R1=LED02, R2=LED03, R3=LED04).  
  - **눈에 보이는 순서 예**: (3번 깜빡임) → **1·3번 켜짐** → **4번 켜짐** → **1번 켜짐** → **2번 켜짐** → **1·3번 켜짐** → … 반복. (1번은 매 초 토글, 2·3·4번은 릴레이 페이즈에 따라 위 순서.)
- **로그 형식**: 전송 문자열은 **`<MSG>MS=부팅후ms,HPSB_TEST,SEQ=...,...\r\n`**. PC 툴은 `<MSG>` ~ `\r\n` 한 메시지 단위로만 로그에 표시. **MS=** 값으로 1초 주기 확인 가능.

---

## 4. 테스트 상태머신 (릴레이 순차)

- **주기**: 1초.
- **phase 0**: RLY_EN01=ON, RLY_EN02=OFF, RLY_EN03=OFF  
- **phase 1**: RLY_EN01=OFF, RLY_EN02=ON, RLY_EN03=OFF  
- **phase 2**: RLY_EN01=OFF, RLY_EN02=OFF, RLY_EN03=ON  
- **phase 3**: 전부 OFF  
- 이후 phase 0으로 돌아가 반복.

각 1초 시점에 `SEQ`가 1씩 증가하고, 그 순간의 R1/R2/R3 상태와 ADC 값으로 한 줄 문자열을 만들어 RS485로 전송합니다.

---

## 5. PC에서 확인할 문자열 예시

시리얼 터미널(9600 8N1, RS485 변환기 연결)로 수신 시 예:

```
HPSB_TEST,SEQ=1,R1=1,R2=0,R3=0,A1=1234,A2=1201,A3=1198
HPSB_TEST,SEQ=2,R1=0,R2=1,R3=0,A1=1230,A2=1205,A3=1195
HPSB_TEST,SEQ=3,R1=0,R2=0,R3=1,A1=1232,A2=1200,A3=1200
HPSB_TEST,SEQ=4,R1=0,R2=0,R3=0,A1=1231,A2=1202,A3=1197
HPSB_TEST,SEQ=5,R1=1,R2=0,R3=0,A1=1234,A2=1201,A3=1198
...
```

- **SEQ**: 전송 시퀀스 번호 (1부터 증가).  
- **R1, R2, R3**: 해당 구간에서 릴레이 1/2/3 ON(1) 또는 OFF(0).  
- **A1, A2, A3**: ADC 채널 PA3, PA4, PA5의 12bit 값 (0~4095).  

이렇게 1초마다 한 줄씩 수신되면 RS485 송신·릴레이·ADC 경로가 정상 동작하는 것으로 보면 됩니다.

---

## 6. 나중에 Modbus 얹기

- `main.c`의 1초 주기 블록을 그대로 두고, **폴링 또는 인터럽트로 UART 수신**만 추가한 뒤, 수신 버퍼를 Modbus RTU 파서로 넘기면 됩니다.
- `RS485_Send()`는 응답 전송 시 그대로 사용 가능합니다. (DE 제어 포함)
- 릴레이/ADC/문자열 생성 로직은 Modbus 레지스터·코일과 매핑하는 쪽으로만 바꾸면 됩니다.
