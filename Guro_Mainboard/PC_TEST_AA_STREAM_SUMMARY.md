# PC 테스트 0xAA 스트림 — 변경 요약

## 제출: unified diff

- **파일**: `pc_test_aa_stream.diff` (프로젝트 루트 `Guro_Mainboard` 기준 경로)
- 적용: `cd Guro_Mainboard && patch -p1 < pc_test_aa_stream.diff`

---

## 목표
- Mainboard UART1 RS485(PA9/PA10, DE/RE=PB1)로 **0xAA 1바이트**를 **500ms**마다 송신.
- 송신 시 **LED2 40ms 펄스** (타이머 기반, main loop tick에서 감소).
- **ENABLE_PC_TEST_AA_STREAM=1**일 때만 동작하며, 이때 Modbus Slave/Master 폴링은 수행하지 않음.

---

## 추가/수정된 파일·함수 요약

| 파일 | 내용 |
|------|------|
| **Application/Inc/app_config.h** | `ENABLE_PC_TEST_AA_STREAM` 옵션 추가 (기본 0). |
| **Application/Inc/pc_test_aa_stream.h** | **신규.** `PcTestAA_Init()`, `PcTestAA_Tick(const aggregated_status_t *agg)` 선언. |
| **Application/Src/pc_test_aa_stream.c** | **신규.** 500ms 주기 0xAA 송신, `set_de_tx()` → `HAL_UART_Transmit` → `set_de_rx()` → guard 2ms, 송신 직전 `LED_Status_OnPcTestAASend()` 호출. |
| **Application/Inc/led_status.h** | `LED_Status_OnPcTestAASend(void)` 선언 추가. |
| **Application/Src/led_status.c** | `PC_TEST_AA_LED_PULSE_MS`(40), `pc_test_aa_led_timer_ms` 추가. Tick에서 감소, LED2 출력에 OR. `LED_Status_OnPcTestAASend()` 구현. |
| **Core/Src/main.c** | `#include "pc_test_aa_stream.h"`. `ENABLE_PC_TEST_AA_STREAM` 시 `PcTestAA_Init()` 호출, 루프에서 `PcTestAA_Tick(&aggregated_status)` 호출 및 Upstream/Modbus 폴링 비호출. `#if USE_PC_TEST_UART1_SLAVE` → `#if USE_PC_TEST_UART1_SLAVE && !ENABLE_PC_TEST_AA_STREAM` (Init·부팅 LED). |

---

## 동작 요약
- **PcTestAA_Init()**: `last_send_tick` 설정, DE=RX(`set_de_rx`)로 초기화.
- **PcTestAA_Tick()**: `HAL_GetTick()` 기준 500ms마다 0xAA 1바이트 송신. 송신 시 `LED_Status_OnPcTestAASend()` → LED2 40ms 타이머 설정. `set_de_tx()` → `HAL_UART_Transmit(&huart1, &0xAA, 1)` → `set_de_rx()` → `HAL_Delay(2)`.
- **LED2 40ms**: `LED_Status_Tick_1ms()`에서 `pc_test_aa_led_timer_ms` 감소 및 LED2 출력에 반영 (인터럽트에서 LED 직접 제어 없음).

---

## 확인 방법
- **app_config.h**에서 `ENABLE_PC_TEST_AA_STREAM`을 **1**로 설정 후 빌드·다운로드.
- PC 툴 로그에 **"RX from board: 0xAA"**가 약 500ms 간격으로 출력되는지 확인.
- 메인보드 **LED2**가 0xAA 송신 시마다 **40ms** 켜졌다 꺼지는지 확인.

---

## 빌드
- STM32CubeIDE에서 **Application/Src/pc_test_aa_stream.c**가 소스 디렉터리에 포함되어 있는지 확인. (보통 `Application/Src`를 이미 포함한 경우 자동 포함됨.)
