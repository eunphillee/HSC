# UART1 Slave 3단계 LED 디버그 + FC03 검증

## 변경 사항 요약

### 1) LED 3단계 표시 (수신 → CRCOK → 응답송신)
- **LED4** 50ms 펄스: UART1 RxEvent 발생 시 (바이트 1개 이상 수신)
- **LED3** 50ms 펄스: CRC OK 프레임 수신 시
- **LED2** 50ms 펄스: 응답 송신 직전 (HAL_UART_Transmit 호출 직전)

### 2) tx_resp 카운터
- `HAL_UART_Transmit(&huart1, tx_frame, tx_len, 100)` 호출 직후에만 `tx_resp_count++` 수행 (실제 송신 발생 시에만 증가).

### 3) FC03 addr=2100 cnt=2 검증
- **ModbusRTU_GetExpectedRequestLength**: FC03 요청 시 8 반환 (modbus_rtu.c case 0x03: return 8).
- **CRC**: Modbus RTU (poly 0xA001), 프레임 마지막 2바이트 LSB first.
- **Slave ID**: frame_buf[0] == SLAVE_ID(9) 체크 후 처리.
- FC03 요청 형식: `09 03 08 34 00 02 CRCLo CRCHi` (addr 2100=0x0834, cnt=2).

---

## 테스트 결과 기입 (테스트 후 작성)

### LED 패턴 (Read DI 또는 FC03 addr=2100 cnt=2 전송 시)
- **LED4**: ( ) 깜빡임 / ( ) 안 깜빡임 → 수신 자체가 들어오는지
- **LED3**: ( ) 깜빡임 / ( ) 안 깜빡임 → CRC OK까지 진행되는지
- **LED2**: ( ) 깜빡임 / ( ) 안 깜빡임 → 응답 송신 경로 진입 여부

### 카운터 (UpstreamSlaveUart1_GetCounts 또는 주기 로그)
- **rx_len_fail**: 
- **rx_crc_fail**: 
- **rx_ok (rx_frame_ok_count)**: 
- **tx_resp**: 

### 해석
- LED4만 깜빡이고 LED3/LED2 안 깜빡임 → 수신은 되나 프레임 길이/CRC 실패 또는 Slave ID 불일치.
- LED4·LED3 깜빡이고 LED2 안 깜빡임 → CRC OK까지 됐으나 응답 PDU 생성 실패 또는 resp_len <= 0.
- LED4·LED3·LED2 모두 깜빡임 → 수신·CRC·송신까지 정상; PC 쪽 수신/디코딩 또는 회선 확인.

---

## 변경 diff
See: `uart1_slave_led_debug_fc03.diff`
