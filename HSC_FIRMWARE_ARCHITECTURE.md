# HSC (Smart Shelter Control System) 펌웨어 아키텍처 설계서

**문서 버전:** 1.0  
**대상 보드:** Guro_Mainboard, Guro_HPSB, Guro_LPSB  
**툴체인:** STM32CubeIDE, HAL

---

## 1. 보드별 펌웨어 아키텍처 개요

### 1.1 MAIN BOARD (Guro_Mainboard)

| 항목 | 내용 |
|------|------|
| MCU | STM32F205VC (Cortex-M3) |
| 역할 | 시스템 마스터, 환경 모니터링, 도어 제어, 알람 집계 |
| 통신 | RS485 Modbus RTU Master (USART1), I2C (SHTC3) |
| 타이머 | 1ms 베이스 틱 스케줄러, WWDG |
| 특징 | 비블로킹 메인 루프, 상태 기계, 서브보드 주기 폴링 |

### 1.2 HPSB (Guro_HPSB)

| 항목 | 내용 |
|------|------|
| MCU | STM32F030K6 (Cortex-M0+) |
| 역할 | 고전류 릴레이 제어, 디지털 입력 읽기, 상태/알람 보고 |
| 통신 | RS485 Modbus RTU Slave (USART1) |
| I/O | 디지털 입력, 릴레이 출력 (HAL GPIO) |

### 1.3 LPSB (Guro_LPSB)

| 항목 | 내용 |
|------|------|
| MCU | STM32F030K6 (Cortex-M0+) |
| 역할 | SSR 출력 제어, 디지털 입력 읽기, 상태/알람 보고 |
| 통신 | RS485 Modbus RTU Slave (USART1) |
| I/O | 디지털 입력, SSR 출력 (GPIO) |

---

## 2. 폴더 구조 (Layered Firmware)

각 보드 프로젝트 내에 동일한 상위 레이어 구조를 적용한다.  
**CubeMX 재생성 시** `Core/`, `Drivers/` 는 덮어쓰이므로 사용자 코드는 아래 사용자 레이어에만 둔다.

### 2.1 MAIN BOARD 폴더 구조

```
Guro_Mainboard/
├── Core/                    # CubeMX 생성 (수정 최소화)
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32f2xx_hal_conf.h
│   │   └── stm32f2xx_it.h
│   └── Src/
│       ├── main.c           # 초기화 + App_Run() 호출만
│       ├── stm32f2xx_it.c
│       └── stm32f2xx_hal_msp.c
├── Drivers/                 # CubeMX HAL (비수정)
├── Modbus/                  # Modbus Master
│   ├── Inc/
│   │   ├── modbus_master.h
│   │   ├── modbus_rtu.h
│   │   └── modbus_cfg.h
│   └── Src/
│       ├── modbus_master.c
│       └── modbus_rtu.c
├── IO/                      # I/O 추상화 (디지털 입출력, 매핑)
│   ├── Inc/
│   │   ├── io_map.h         # enum 기반 채널 정의
│   │   ├── dio_main.h
│   │   └── relay_main.h
│   └── Src/
│       ├── dio_main.c
│       └── relay_main.c
├── Drivers/
│   └── SHTC3/               # SHTC3 센서 드라이버 (I2C + CRC)
│       ├── Inc/
│       │   ├── shtc3.h
│       │   └── shtc3_crc.h
│       └── Src/
│           ├── shtc3.c
│           └── shtc3_crc.c
├── Application/
│   ├── Inc/
│   │   ├── app_scheduler.h   # 1ms 타이머 기반 태스크 스케줄러
│   │   ├── app_door_control.h
│   │   ├── app_alarm.h
│   │   ├── app_env.h        # 온습도 필터/임계값
│   │   └── app_config.h     # 주소, 주기, 임계값 등
│   └── Src/
│       ├── app_scheduler.c
│       ├── app_door_control.c
│       ├── app_alarm.c
│       ├── app_env.c
│       └── app_main.c       # 상태 기계 + Run 호출
└── (기존 .ioc, .cproject 등)
```

### 2.2 HPSB / LPSB 폴더 구조 (동일 패턴)

```
Guro_HPSB/   (또는 Guro_LPSB/)
├── Core/                    # CubeMX 생성
├── Drivers/                 # HAL
├── Modbus/                  # Modbus Slave
│   ├── Inc/
│   │   ├── modbus_slave.h
│   │   ├── modbus_rtu.h
│   │   └── modbus_cfg.h
│   └── Src/
│       ├── modbus_slave.c
│       └── modbus_rtu.c
├── IO/                      # I/O 추상화 + Modbus 레지스터 매핑
│   ├── Inc/
│   │   ├── io_map.h         # DI/DO enum 및 Modbus 주소 매핑
│   │   ├── dio_slave.h
│   │   └── relay_slave.h    (HPSB) / ssr_slave.h (LPSB)
│   └── Src/
│       ├── dio_slave.c
│       └── relay_slave.c    / ssr_slave.c
└── Application/
    ├── Inc/
    │   ├── app_scheduler.h  # 1ms 틱
    │   ├── app_slave.h      # Slave 상태 기계
    │   └── app_config.h
    └── Src/
        ├── app_scheduler.c
        └── app_main.c
```

---

## 3. 모듈 책임 정의

### 3.1 MAIN BOARD

| 모듈 | 경로 | 책임 |
|------|------|------|
| **Modbus Master** | Modbus/ | RTU 프레임 송수신, FC03/04/06/16 요청/응답 파싱, 타임아웃, 멀티 드롭 순차 폴링 |
| **SHTC3** | Drivers/SHTC3/ | I2C 트랜잭션, 주기 측정 트리거, CRC 검증, 온·습도 raw → 정수 변환 |
| **IO (DIO/Relay)** | IO/ | 로컬 디지털 입력 스캔, 로컬 릴레이 출력, enum 기반 채널 접근 |
| **Scheduler** | Application/app_scheduler | 1ms SysTick 기반 플래그 설정, 태스크 주기 관리 (no blocking) |
| **Door Control** | Application/app_door_control | DI/환경/알람 기반 도어 열림·닫힘 결정, 인터록, Modbus Write 요청 생성 |
| **Alarm** | Application/app_alarm | 로컬 + 서브보드 알람 집계, 우선순위, 알람 상태 레지스터 |
| **Env** | Application/app_env | SHTC3 데이터 수신, 이동평균/저역필터, 임계값 비교, 과열/과습 플래그 |
| **App Main** | Application/app_main | 상위 상태 기계 (Init → Run → Fault), WWDG 리셋, Run 내에서 스케줄러·도어·알람·Modbus 조율 |

### 3.2 HPSB / LPSB

| 모듈 | 경로 | 책임 |
|------|------|------|
| **Modbus Slave** | Modbus/ | RTU 수신, FC01/02/03/04/05/06/15/16 처리, 응답 프레임 조립, 예외 응답 |
| **IO Map** | IO/io_map.h | DI/DO 채널 enum, Modbus 코일/디스크릿/홀딩/입력 레지스터 주소 매핑 테이블 |
| **DIO Slave** | IO/dio_slave | GPIO 입력 폴링 → Modbus 입력 이미지 반영 |
| **Relay/SSR** | IO/relay_slave, ssr_slave | Modbus 코일/레지스터 값 → GPIO 출력 반영 |
| **Scheduler** | Application/app_scheduler | 1ms 틱, 주기적 IO 스캔·Modbus 처리 타이밍 |
| **App Main** | Application/app_main | Init → Run 상태, Run에서 스케줄러 실행 및 Modbus_Slave_Poll() 호출 |

---

## 4. 상태 기계 (텍스트 다이어그램)

### 4.1 MAIN BOARD 상위 상태 기계

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                     MAIN BOARD STATE MACHINE             │
                    └─────────────────────────────────────────────────────────┘

     ┌──────────┐
     │  POWER   │
     │   ON     │
     └────┬─────┘
          │
          ▼
    ┌──────────┐     init_ok      ┌──────────┐    run_ok     ┌──────────┐
    │  INIT    │─────────────────►│   RUN    │◄──────────────│  FAULT   │
    │          │                  │          │               │          │
    └────┬─────┘                  └────┬─────┘               └────┬─────┘
         │                              │                           │
         │ init_fail / wwdg             │ fault_cond                │ recover
         │                              │ (comm_fail, alarm_critical)│ (user/auto)
         └─────────────────────────────┼───────────────────────────┘
                                       ▼
                                  ┌──────────┐
                                  │  FAULT   │
                                  │ (safe    │
                                  │  state)  │
                                  └──────────┘

    RUN 내부 서브 상태 (선택 사항, 폴링 순서만 있으면 될 수 있음):
    ┌────────────────────────────────────────────────────────────────────────┐
    │  RUN: [Idle] → [Poll HPSB] → [Poll LPSB] → [Read SHTC3] → [Door Logic] │
    │       → [Alarm Update] → [WWDG Refresh] → [Idle] ...                    │
    └────────────────────────────────────────────────────────────────────────┘
```

### 4.2 MAIN BOARD Run 내 태스크 흐름 (논리적 순서)

```
    [1ms tick] ──► Scheduler_Update() ──► set flags for 10ms, 100ms, 1000ms

    10ms  ──► Modbus_Master_Poll() (한 슬레이브에 1회 요청 또는 응답 처리)
    100ms ──► Door_Control_Update(), Alarm_Aggregate()
    1000ms ──► SHTC3_Trigger_Measurement() / Read_Result(), Env_Update()
```

### 4.3 HPSB / LPSB 상위 상태 기계

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                   HPSB / LPSB STATE MACHINE             │
                    └─────────────────────────────────────────────────────────┘

     ┌──────────┐
     │  POWER   │
     │   ON     │
     └────┬─────┘
          │
          ▼
    ┌──────────┐     init_ok      ┌──────────┐
    │  INIT    │─────────────────►│   RUN    │
    │ (GPIO,   │                  │(Modbus   │
    │  Modbus, │                  │ Slave,   │
    │  Sched)  │                  │ IO scan) │
    └──────────┘                  └──────────┘

    RUN: 반복
      - Scheduler_Update()
      - IO_Scan()  (DI 읽기 → Modbus 이미지 갱신)
      - Modbus_Slave_Poll()  (UART 수신, 파싱, FC 처리, 응답 전송)
      - Output_Update()  (Modbus 코일/레지스터 → 릴레이/SSR 출력)
```

---

## 5. 태스크 스케줄링 전략

### 5.1 베이스 틱

- **소스:** SysTick 1ms (HAL 기본).
- **역할:** `Scheduler_Update()` 에서 1ms마다 카운터 증가, 주기 만료 시 해당 태스크 플래그 세트.
- **규칙:** `HAL_Delay()` 등 블로킹 지연 사용 금지. 모든 주기 동작은 플래그 + 메인 루프에서 처리.

### 5.2 MAIN BOARD 주기 테이블

| 태스크 | 주기 | 액션 |
|--------|------|------|
| Scheduler_Update | 1 ms | 틱 카운터 증가, 10/100/1000 ms 플래그 설정 |
| Modbus_Master_Poll | 10 ms | 한 번에 한 트랜잭션 (요청 보내기 또는 응답 처리), 라운드 로빈 HPSB → LPSB |
| Door_Control_Update | 100 ms | DI, 환경, 알람 기반 도어 명령 계산, 필요 시 Modbus Write 요청 큐에 적재 |
| Alarm_Aggregate | 100 ms | 로컬 + HPSB/LPSB 알람 비트 합산 |
| SHTC3_Trigger / Read | 1000 ms | 측정 트리거 또는 결과 읽기 (상태 기계로 분리 가능) |
| Env_Update | 1000 ms | 필터 적용, 임계값 비교 |
| WWDG_Refresh | 500 ms (예) | WWDG 리셋 (설정에 따름) |

### 5.3 HPSB / LPSB 주기 테이블

| 태스크 | 주기 | 액션 |
|--------|------|------|
| Scheduler_Update | 1 ms | 틱 카운터, 10 ms 플래그 |
| IO_Scan | 10 ms | 모든 DI 읽기 → Modbus 입력 이미지 업데이트 |
| Modbus_Slave_Poll | 10 ms (또는 수신 시) | UART 수신, 프레임 파싱, FC 처리, 응답 전송 |
| Output_Update | 10 ms | 홀딩 레지스터/코일 → 릴레이·SSR 출력 |

### 5.4 스케줄러 추상 인터페이스 (개념)

- `Scheduler_Init()`: 카운터/플래그 초기화.
- `Scheduler_Update()`: SysTick 또는 1ms 타이머 콜백에서 호출.
- `Scheduler_IsDue(TASK_xxx)`: 해당 태스크 주기 만료 시 true 반환, 다음 주기로 리셋.
- 또는 `Scheduler_GetFlags()`로 비트마스크 반환 후 메인 루프에서 비트별 처리.

---

## 6. 보드 간 통신 구조 (RS485 Modbus RTU)

### 6.1 물리 / 링크

- **버스:** 단일 RS485 (A, B, GND). MAIN ──┬── HPSB  
                                            └── LPSB  
- **드라이버:** USART + DE(방향) 제어 GPIO. 송신 시 DE=1, 수신 대기 시 DE=0.
- **파라미터:** 9600 (또는 19200) baud, 8N1. 슬레이브 주소: HPSB=1, LPSB=2 (app_config.h에서 정의).

### 6.2 MAIN → HPSB/LPSB 요청 (Master)

- **폴링 순서:** 라운드 로빈. 한 번에 하나의 슬레이브에 대해 요청 1개 + 응답 대기 (또는 타임아웃).
- **주요 요청:**
  - FC03: 홀딩 레지스터 블록 읽기 (상태, 알람, DI 이미지).
  - FC04: 입력 레지스터 읽기 (선택 사항).
  - FC06/FC16: 도어/릴레이 제어 명령 쓰기.
- **타임아웃:** 3.5 character time 이상 응답 없으면 해당 슬레이브 통신 실패로 간주, 다음 슬레이브로 진행.

### 6.3 HPSB/LPSB → MAIN 응답 (Slave)

- FC01/02: 코일/디스크릿 상태 읽기 요청에 대한 응답.
- FC03/04: 레지스터 읽기 요청에 대한 응답 (바이트 순서: high byte first per register).
- FC05/06/15/16: 쓰기 요청에 대한 에코 응답 (정상 시 요청 내용 반복).

### 6.4 레지스터 맵 (개념, 보드별 상세는 io_map.h에서 정의)

- **공통 원칙:**
  - 코일 (FC01/05): DO 채널 (릴레이/SSR) 1비트 per coil.
  - 디스크릿 (FC02): DI 채널 1비트 per input.
  - 홀딩 레지스터 (FC03/06/16): 제어 설정, 알람 마스크, 예약.
  - 입력 레지스터 (FC04): 읽기 전용 상태, ADC 등 (필요 시).
- **주소 범위:** 0-based. 예: HPSB 코일 0..7, LPSB 코일 0..7, 홀딩 0..N-1.  
  MAIN은 이 주소를 알고 FC03/06/16으로 읽기/쓰기.

### 6.5 프로토콜 계층 분리

- **Modbus/ (RTU):** 프레임 인코딩/디코딩, CRC, 예외 코드. 보드 타입에 따라 Master 또는 Slave만 포함.
- **Application:** “HPSB 릴레이 3번 ON” 같은 의미 단위로 Modbus 요청/응답을 호출. 통신 실패 시 재시도/알람은 Application에서 처리.

---

## 7. SHTC3 드라이버 구조 (CRC 포함)

### 7.1 역할

- I2C로 SHTC3와 통신.
- **주기 측정 트리거 모드** 사용 (Wake up → Measure → Sleep 사이클).
- 모든 데이터 수신 시 **CRC 검증** (Sensirion CRC-8, 다항식 0x31).

### 7.2 파일 구성

| 파일 | 역할 |
|------|------|
| `shtc3.h` / `shtc3.c` | 초기화, 측정 트리거, 결과 읽기, 온·습도 보정 공식 적용, 외부에 °C / %RH 제공 |
| `shtc3_crc.h` / `shtc3_crc.c` | CRC-8 계산 (2바이트 + checksum 검증), 재사용 가능한 유틸 |

### 7.3 SHTC3 내부 상태 기계 (드라이버)

```
    IDLE ──► TRIGGER_SENT ──► (wait ~12ms) ──► READ_RESULT ──► (CRC check) ──► IDLE
                                                                   │
                                                                   └── CRC_FAIL ──► IDLE (에러 반환)
```

- **IDLE:** 측정 트리거 명령 전송 (I2C Write: 0x3517 등, 데이터시트 기준).
- **TRIGGER_SENT:** 다음 1초 주기까지 대기 또는 별도 타이머로 ~12ms 후 READ_RESULT로 전이.
- **READ_RESULT:** 6바이트 읽기 (T high, T low, T crc, H high, H low, H crc). 각 2바이트+CRC 검증.
- **CRC_FAIL:** 해당 측정 무시, 에러 코드 반환, IDLE로 복귀.

### 7.4 API 개념 (함수만 나열)

- `SHTC3_Init(hi2c)`: I2C 핸들 바인딩, 소프트 리셋(선택).
- `SHTC3_TriggerMeasurement()`: 논블로킹, 트리거만 전송.
- `SHTC3_IsMeasurementReady()`: 읽기 가능 여부 (시간 또는 플래그 기반).
- `SHTC3_ReadResult(float *temp_c, float *rh_pct)`: 결과 읽기 + CRC 검증, 보정 적용, 성공/실패 반환.
- `SHTC3_CRC_Compute(uint8_t *data, uint8_t len)`: 2바이트용 CRC-8 (내부 또는 shtc3_crc.c).

### 7.5 MAIN 보드 연동

- 1초 주기 태스크에서: `SHTC3_TriggerMeasurement()` 호출 후, 다음 1초 주기에서 `SHTC3_ReadResult()` 호출 (또는 중간 12ms 후 준비 플래그 확인).
- 읽은 값은 `App_Env`로 전달 → 필터 → 임계값 비교 → 도어/알람 로직에 사용.

---

## 8. 메인 제어 루프 스켈레톤 (MAIN BOARD)

```c
/* app_main.c (개념) */

#include "app_scheduler.h"
#include "app_door_control.h"
#include "app_alarm.h"
#include "app_env.h"
#include "modbus_master.h"
#include "shtc3.h"

static App_State_t app_state = APP_STATE_INIT;

void App_Init(void)
{
    Scheduler_Init();
    Modbus_Master_Init();
    SHTC3_Init(&hi2c1);   /* or hi2c3, from main.h */
    Door_Control_Init();
    Alarm_Init();
    Env_Init();
    app_state = APP_STATE_RUN;
}

void App_Run(void)   /* main.c while(1) 에서만 호출 */
{
    Scheduler_Update();   /* 1ms 마다 호출되도록 SysTick 또는 타이머에서 호출 가능;
                             또는 여기서 한 번 호출 시 1ms 경과 체크 */

    switch (app_state)
    {
    case APP_STATE_RUN:
        if (Scheduler_IsDue(TASK_MODBUS_POLL))
            Modbus_Master_Poll();
        if (Scheduler_IsDue(TASK_DOOR_UPDATE))
            Door_Control_Update();
        if (Scheduler_IsDue(TASK_ALARM))
            Alarm_Aggregate();
        if (Scheduler_IsDue(TASK_ENV_1S))
        {
            SHTC3_TriggerMeasurement();  /* 또는 이미 트리거됐으면 ReadResult */
            SHTC3_ReadResult(&temp_c, &rh_pct);
            Env_Update(temp_c, rh_pct);
        }
        if (Scheduler_IsDue(TASK_WWDG))
            HAL_WWDG_Refresh(&hwwdg);
        if (Alarm_IsCritical())
            app_state = APP_STATE_FAULT;
        break;

    case APP_STATE_FAULT:
        Door_Control_EnterSafeState();
        if (Scheduler_IsDue(TASK_WWDG))
            HAL_WWDG_Refresh(&hwwdg);
        /* recover 조건 시 app_state = APP_STATE_RUN; */
        break;

    default:
        break;
    }
}
```

- `main.c` 의 `while(1)` 에서는 `App_Run()` 만 호출. 초기화는 `main()` 내 `USER CODE BEGIN 2` 에서 `App_Init()` 호출.
- `Scheduler_Update()` 가 1ms마다 호출되려면, SysTick 콜백에서 `Scheduler_Update()` 를 호출하거나, `App_Run()` 진입 시 `HAL_GetTick()` 차이로 1ms 경과 시에만 `Scheduler_Update()` 호출하는 방식 가능.

---

## 9. Modbus 추상화 레이어 구조

### 9.1 Master (MAIN)

- **modbus_rtu.c:**  
  - 프레임 조립 (주소, FC, 데이터, CRC), UART 송신, DE=1/0 제어.  
  - 수신 버퍼에서 한 프레임 추출, CRC 검증, 슬레이브 주소/FC 확인.
- **modbus_master.c:**  
  - `Modbus_Master_ReadHoldingRegs(slave_id, start_addr, n_regs, buf)`  
  - `Modbus_Master_WriteSingleReg(slave_id, addr, value)`  
  - `Modbus_Master_WriteMultiRegs(slave_id, start_addr, n_regs, buf)`  
  - 내부 상태: IDLE / SEND_REQUEST / WAIT_RESPONSE / PARSE_RESPONSE.  
  - 한 번의 `Modbus_Master_Poll()` 호출당 한 상태 전이 (한 트랜잭션만 진행).  
  - 결과는 콜백 또는 전역 이미지(레지스터/코일 캐시)로 Application에 전달.

### 9.2 Slave (HPSB/LPSB)

- **modbus_rtu.c:**  
  - 수신 바이트 적재, 3.5 character 타임아웃으로 프레임 완료 판단, CRC 검증, FC 디스패치.
- **modbus_slave.c:**  
  - FC01/02/03/04/05/06/15/16 핸들러.  
  - **IO 계층과의 연결:** 코일/디스크릿/홀딩/입력 레지스터 주소 → `io_map.h` 의 채널 또는 레지스터 배열 인덱스로 매핑.  
  - 읽기: IO_GetCoil(), IO_GetDiscrete(), IO_GetHoldingReg(), IO_GetInputReg().  
  - 쓰기: IO_SetCoil(), IO_SetHoldingReg() 등.  
  - 응답 프레임 조립 후 UART 송신, DE 제어.

### 9.3 설정 (modbus_cfg.h)

- 보드 타입별 컴파일 플래그: `MODBUS_MASTER` / `MODBUS_SLAVE`.
- 슬레이브 주소 (HPSB=1, LPSB=2), UART, DE GPIO, 타임아웃, 버퍼 크기.

---

## 10. 확장성 (향후 AI/IoT 연동)

- **통신 계층 분리:** Application은 “슬레이브 1번 레지스터 0~3 읽기” 수준의 API만 사용. 실제 전송은 Modbus 계층이 담당. 나중에 Ethernet/Wi‑Fi 모듈을 붙이면 동일한 “이미지”를 MQTT/HTTP로도 내보낼 수 있도록 Adapter만 추가.
- **이미지 버퍼:** MAIN에서 서브보드 상태·알람을 보관하는 전역 구조체(이미지)를 두고, Modbus 폴링 결과로만 갱신. AI/원격 모니터링은 이 이미지를 주기적으로 읽어서 전송하는 방식으로 확장.
- **설정 외부화:** 임계값, 슬레이브 주소, 폴링 주기 등을 `app_config.h` 또는 별도 설정 구조체로 두고, 추후 NVS/EEPROM/원격 설정으로 대체 가능하도록 접근.
- **모듈화:** SHTC3, Modbus, IO, Scheduler는 서로 의존성을 줄이고, Application만 이들을 조합. 새 센서나 프로토콜 추가 시 해당 드라이버와 Application 훅만 추가.

---

## 문서 이력

- 1.0: 초안 (MAIN/HPSB/LPSB 아키텍처, 폴더 구조, 상태 기계, 스케줄링, Modbus, SHTC3, 메인 루프 스켈레톤, 확장성)
