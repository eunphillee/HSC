# LPSB_TEST 보드 검증용 펌웨어

STM32F030K6Tx 기반 LPSB 보드 테스트. RS485 Modbus RTU Slave, LED/SSR/ADC/ID 스위치 동작 검증.

## 핀맵 (회로 기준)

| 핀 | 기능 |
|----|------|
| PA0/1/2 | SSR1_EN, SSR2_EN, SSR3_EN |
| PA3/4/5 | ACS_ADC01, ACS_ADC02, ACS_ADC03 |
| PA9/10/11 | USART1_TX, RX, RS485_DE |
| PB0/1/3/4 | ID_BIT1~4 (Slave ID) |
| PB5/6/7, PA15 | LED01~04 |

## Modbus 주소맵 (9600 8N1)

- **Slave ID**: DIP 스위치 ID_BIT1~4 (가중치 1,2,4,8). 기본 2.
- **FC03 Read Holding Registers** (시작 0, 개수 9)
  - 0: SSR1 상태 (0/1)
  - 1: SSR2 상태
  - 2: SSR3 상태
  - 3: ADC1 raw (평균)
  - 4: ADC2 raw
  - 5: ADC3 raw
  - 6: Slave ID
  - 7: Heartbeat 카운터
  - 8: 펌웨어 버전 (0x0001)
- **FC01 Read Coils** (시작 0, 개수 3): Coil 0~2 = SSR1~3
- **FC05 Write Single Coil**: 주소 0/1/2, 값 0xFF00=ON, 0x0000=OFF → SSR1/2/3 제어

## 테스트 방법

1. 빌드 후 보드에 플래시, 전원 인가 → LED01~04 순차 점등 1회.
2. PC 테스트 툴에서 RS485(또는 USB-RS485)로 Slave ID=2, 9600 8N1 연결.
3. FC03으로 주소 0, 9개 읽기 → SSR 상태·ADC raw·Slave ID·Heartbeat·Version 확인.
4. FC05로 Coil 0(주소 0) ON(0xFF00) → SSR1 켜짐, OFF(0x0000) → SSR1 꺼짐.
5. 수신 시 LED04 짧게 점멸. Heartbeat는 LED01이 약 0.5초마다 토글.

## 수정/추가 파일

- `Core/Inc/rs485_drv.h`, `Core/Src/rs485_drv.c`
- `Core/Inc/adc_app.h`, `Core/Src/adc_app.c`
- `Core/Inc/lpsb_app.h`, `Core/Src/lpsb_app.c`
- `Core/Inc/modbus_slave.h`, `Core/Src/modbus_slave.c`
- `Core/Src/main.c` (USER CODE 영역: include, init, 루프, UART RX 콜백, ADC 캘리브레이션)
