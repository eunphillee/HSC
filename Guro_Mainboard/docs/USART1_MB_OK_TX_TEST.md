## USART1 MB_OK 송신 테스트 (PC 수신 확인용)

### 목적
PC 프로그램에서 수신이 안 되는 상황을 분리하기 위해, **Mainboard가 USART1(상위 RS485)로 매 1초마다** 문자열을 송신하는지 먼저 확인합니다.

### Mainboard 펌웨어 조건
1. `Guro_Mainboard/Application/Inc/app_config.h`에서 아래 매크로를 `1`로 설정:
   - `MB_UART1_TX_OK_STREAM_TEST`
2. 빌드/다운로드 후 부팅.

### 송신 문자열
- 주기: `1초`
- 내용: `MB_OK\r\n`

### PC Terminal(screen) 확인 조건
1. PC에서 **Mainboard 상위 RS485(USART1) 포트**를 연결합니다. (PC 테스트 툴의 기본 연결 포트와 동일)
2. 시리얼 설정:
   - Baud: `9600`
   - Data bits: `8`
   - Parity: `None`
   - Stop bits: `1`
3. 예시:
   - macOS: `screen /dev/cu.usbserial-XXXX 9600`
4. 정상이라면 `MB_OK`가 1초마다 반복 출력됩니다.

### PC Test Tool에서 확인 포인트
- 연결 성공 후 로그에 아래가 먼저 표시됩니다.
  - `[PC-TOOL] serial read thread started`
- 수신되는 바이트가 있으면 UI 로그에 아래가 한 바이트씩 표시됩니다(로그 레벨/필터와 무관).
  - `[PC-TOOL] rx byte=0x..`
- 상태 배지에 누적 수신 카운트가 표시됩니다.
  - `Connected | RX count: N`

