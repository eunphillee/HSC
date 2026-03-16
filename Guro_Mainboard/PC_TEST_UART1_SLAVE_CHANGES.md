# PC 테스트를 USART1 RS485(PA9/PA10, DE=PB1)로 전환 — 변경 요약

## 목표
- PC ↔ Mainboard 통신을 **USART2(PA2/PA3)** 대신 **USART1 RS485(PA9/PA10, PB1=DE)** 로 수행.
- Modbus RTU Slave ID=9, 9600 8N1, PC_Test_Tool은 그대로 사용하고 PC는 Mainboard의 **UART1 RS485 포트**에 연결.

---

## huart1로 바뀐 위치 (파일/함수)

| 파일 | 변경 내용 |
|------|-----------|
| **Application/Inc/app_config.h** | 신규. `USE_PC_TEST_UART1_SLAVE=1` 시 UART1 Slave 사용, Master 폴링·USART2 Modbus 비활성화. |
| **Application/Inc/upstream_slave_uart1.h** | 신규. USART1 Slave API: `UpstreamSlaveUart1_Init`, `UpstreamSlaveUart1_Poll`, `UpstreamSlaveUart1_RxEventCallback`. |
| **Application/Src/upstream_slave_uart1.c** | 신규. **huart1** 사용. `extern UART_HandleTypeDef huart1`, `HAL_UARTEx_ReceiveToIdle_IT(&huart1, ...)`, `HAL_UART_Transmit(&huart1, ...)`. DE=PB1: `set_de_tx()` 송신 전, `set_de_rx()` 송신 후 + 2ms guard. |
| **Application/Src/upstream_pc_protocol.c** | `HAL_UARTEx_RxEventCallback`: **huart == &huart1** 이면 `UpstreamSlaveUart1_RxEventCallback(Size)` 호출. Modbus 처리 블록을 `#if UPSTREAM_PC_MODBUS_SLAVE_ENABLE` 로 감싸 USART2 Modbus 비활성화 가능. |
| **Core/Src/main.c** | `MX_USART1_UART_Init`: **BaudRate 115200 → 9600**. `USE_PC_TEST_UART1_SLAVE` 시 `UpstreamSlaveUart1_Init()` 호출, 루프에서 `UpstreamSlaveUart1_Poll(&aggregated_status)` 호출. `MODBUS_MASTER_POLL_ENABLE` 시에만 `ModbusMaster_Poll()` 호출. |

---

## 1) USART2 Modbus 비활성화
- **app_config.h**: `USE_PC_TEST_UART1_SLAVE=1` 이면 `UPSTREAM_PC_MODBUS_SLAVE_ENABLE=0`.
- **upstream_pc_protocol.c**: `#if UPSTREAM_PC_MODBUS_SLAVE_ENABLE` 안에서만 Slave ID 9·CRC·`process_modbus_frame` 처리. 꺼면 수신은 STX/ETX만 처리(코드 삭제 없음).

## 2) Modbus Slave를 USART1에 부착
- **upstream_slave_uart1.c**: USART1에 대해 ReceiveToIdle_IT, 링 버퍼, 4ms 무수신 시 프레임 종료, FC별 기대 길이·CRC 검사 후 FC02/03/05/06/15 처리, `UpstreamSlave_HandleRequest` 호출, 응답 전송. Slave ID=9.

## 3) RS485 방향 제어 (DE/RE = PB1)
- **upstream_slave_uart1.c**: `set_de_tx()` / `set_de_rx()` 로 **RS485_DE_GPIO_Port, RS485_DE_Pin**(main.h, PB1) 제어. 송신 전 DE=1, `HAL_UART_Transmit` 완료 후 DE=0, `HAL_Delay(TX_GUARD_MS)` 2ms 후 `ReceiveToIdle_IT` 재시작.

## 4) CubeMX/초기화
- **main.c**: `MX_USART1_UART_Init` 에서 **BaudRate = 9600**, 8N1 유지.
- **stm32f2xx_hal_msp.c**: 변경 없음. 이미 PA9=USART1_TX, PA10=USART1_RX, USART1 인터럽트 활성화.
- **main.c** `MX_GPIO_Init`: RS485_DE_Pin(PB1) 출력, 초기값 RESET(RX 모드) — 기존과 동일.

## 5) Master 폴링 옵션
- **app_config.h**: `USE_PC_TEST_UART1_SLAVE=1` 이면 `MODBUS_MASTER_POLL_ENABLE=0`. **main.c** 루프에서 `ModbusMaster_Poll()` 는 `#elif MODBUS_MASTER_POLL_ENABLE` 일 때만 호출. PC 테스트 시 Master 폴링 비활성화.

---

## 검증
- STM32CubeIDE에서 Guro_Mainboard 빌드 → 0 errors / 0 warnings 목표.
- 보드 다운로드 후 PC를 **Mainboard UART1 RS485 포트**에 연결, PC_Test_Tool에서 Slave ID=9, 9600, Read DI(FC03 addr=2100 cnt=2) 등 실행 시 **RX OK** 또는 **RX EXC** 수신 확인.
- **UART1 수신 존재 여부 확인**: PC툴에서 TX FC03(addr=2100, cnt=2) 전송 시 어댑터 TX LED가 깜빡이면, 메인보드 **LED2가 50ms 펄스로 깜빡이면** UART1 수신이 들어온 것(물리층 통과·프로그램 다운로드 확인). LED2가 전혀 안 깜빡이면 물리/트랜시버/커넥터/A·B·GND 문제로 확정.

---

## 통합 diff
아래는 수정된 파일에 대한 diff. 신규 파일(app_config.h, upstream_slave_uart1.h, upstream_slave_uart1.c)은 프로젝트에 추가 후 빌드하면 됨.
