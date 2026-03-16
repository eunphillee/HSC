# FC05 처리 흐름 및 주소 맵 보고서

## 1. FC05 처리 흐름도

```
PC (FC05 coil=898 or 899, value=0/1)
    │
    ▼ USART1 (Modbus RTU)
UpstreamSlaveUart1_Poll()  [Application/Src/upstream_slave_uart1.c]
    │ rx_ring → frame_buf, expected=8, CRC OK
    ▼
process_modbus_frame(frame_buf, expected, agg)
    │ fc=0x05, start_addr = (frame[2]<<8)|frame[3], write_data=&frame[4]
    ▼
UpstreamSlave_HandleRequest(0x05, start_addr, 0, write_data, agg, resp_pdu, ...)
    │  [Gateway/Src/upstream_slave_h2tech.c]
    ▼
handle_fc05(start_addr, write_data, response, resp_max)
    │
    ├─ resp_max<5 || !write_data  → return -1 (no response)
    │
    ├─ h2_dec = H2Map_ModbusAddrToH2Dec(start_addr)  /* start_addr + 1 */
    │   → 898→899, 899→900
    │
    ├─ e = H2Map_FindByDec(H2_AREA_1X, h2_dec)  [Gateway/Src/h2tech_address_map.c]
    │   → g_map[] 에서 area==1X, h2_dec 일치 항목 검색
    │
    ├─ e == NULL  → response[0]=0x85, response[1]=0x02 (Illegal Data Address), return 2
    │
    ├─ e->rw != H2_RW_WRITE  → response[1]=0x03 (Illegal Data Value), return 2
    │
    ├─ e->action == H2_ACT_WRITE_SUB_COIL 이면
    │   h2_dec 899..910 → (slave_id, sub_coil) 계산
    │   Gateway_Action_WriteSubCoil(slave_id, sub_coil, value)
    │       → ModbusMaster_WriteCoil(slave_id, sub_coil, value)  [Modbus/Src/modbus_master.c]
    │       → USART2 TX (DE HIGH → TX → TC → DE LOW) → HAL_UART_Receive(8 bytes)
    │
    ├─ !H2Map_ApplyWrite(e, value, 300)  → response[1]=0x04 (Slave Device Failure), return 2
    │   ※ 주소는 유효하나 하위버스 쓰기 실패(타임아웃 등)
    │
    └─ response[0]=0x05, echo addr/value, return 5  → process_modbus_frame 이 tx_frame 송신
```

**진입점 요약**
- **Modbus slave FC05 handler**: `handle_fc05()` (upstream_slave_h2tech.c)
- **Coil write dispatch**: `H2Map_FindByDec()` → `H2Map_ApplyWrite()` (h2tech_address_map.c)
- **Gateway mapping**: `h2_dec_to_sub_coil()`, `Gateway_Action_WriteSubCoil()` (gateway_actions.c)

**898/899 수신 시**
- 898 → h2_dec 899 → `g_map`에서 WR_HPSB_COIL_0 → slave=1, sub_coil=0
- 899 → h2_dec 900 → WR_HPSB_COIL_1 → slave=1, sub_coil=1  
→ **두 주소 모두 테이블에 존재**. 0x02가 났다면 **FindByDec가 NULL을 반환한 경우(주소 미매핑)** 이거나, **ApplyWrite 실패 시 기존에 0x02를 반환하던 경우**임.  
→ **수정**: ApplyWrite 실패 시 **0x04(Slave Device Failure)** 반환으로 변경하여, 주소 오류(0x02)와 하위버스 실패(0x04) 구분.

---

## 2. Mainboard FC05 허용 coil 주소 (코드 기준)

**변환 규칙**: Modbus `start_addr` → `h2_dec = start_addr + 1`.  
테이블은 `h2_dec` 기준이므로, 허용 **start_addr** = 891~909.

| Modbus start_addr | h2_dec | 기능 | 비고 |
|-------------------|--------|------|------|
| 891 | 892 | VB_ONOFF_8 | PULSE_OUTPUT |
| 892 | 893 | VB_ONOFF_9 | PULSE_OUTPUT |
| 893 | 894 | VB_ONOFF_10 | PULSE_OUTPUT |
| 894 | 895 | VB_ONOFF_11 | PULSE_OUTPUT |
| 895 | 896 | VB_ONOFF_12 | PULSE_OUTPUT |
| 896 | 897 | DOOR_OPEN_CTRL_1 | PULSE_MAIN_DOOR1 |
| 897 | 898 | DOOR_OPEN_CTRL_2 | PULSE_MAIN_DOOR2 |
| **898** | **899** | **WR_HPSB_COIL_0 (HPSB relay1)** | WRITE_SUB_COIL → slave=1, coil=0 |
| 899 | 900 | WR_HPSB_COIL_1 (HPSB relay2) | slave=1, coil=1 |
| 900 | 901 | WR_HPSB_COIL_2 (HPSB relay3) | slave=1, coil=2 |
| 901 | 902 | WR_LPSB1_COIL_0 (LPSB1 SSR1) | slave=2, coil=0 |
| 902 | 903 | WR_LPSB1_COIL_1 | slave=2, coil=1 |
| 903 | 904 | WR_LPSB1_COIL_2 | slave=2, coil=2 |
| 904 | 905 | WR_LPSB2_COIL_0 | slave=4, coil=0 |
| 905 | 906 | WR_LPSB2_COIL_1 | slave=4, coil=1 |
| 906 | 907 | WR_LPSB2_COIL_2 | slave=4, coil=2 |
| 907 | 908 | WR_LPSB3_COIL_0 | slave=8, coil=0 |
| 908 | 909 | WR_LPSB3_COIL_1 | slave=8, coil=1 |
| 909 | 910 | WR_LPSB3_COIL_2 | slave=8, coil=2 |

**결론**: 898, 899는 코드상 **모두 매핑됨**. HPSB relay1/2/3, LPSB SSR1/2/3는 **FC05 WRITE_SUB_COIL**로 구현되어 있음.

---

## 3. 898/899가 실패하는 이유

- **주소 미매핑이 아님**: `g_map`에 h2_dec 899, 900이 있으므로 `FindByDec`는 항목을 찾음.
- **실제 원인**: `H2Map_ApplyWrite()` → `Gateway_Action_WriteSubCoil()` → `ModbusMaster_WriteCoil()` 호출 후, **USART2로 HPSB에 보낸 요청에 대한 응답이 없거나 실패**(타임아웃/CRC 등)하면 `ApplyWrite`가 false를 반환하고, **기존에는 0x02(Illegal Data Address)를 반환**하고 있었음.
- **수정 사항**
  1. **ApplyWrite 실패 시 예외를 0x04(Slave Device Failure)** 로 변경  
     → 주소 오류(0x02)와 하위버스/게이트웨이 실패(0x04) 구분 가능.
  2. **FC05 coil 898/899 상세 로그**  
     - `FC05_COIL_DIAG_LOG=1`로 빌드 시  
       `[GW] FC05 recv coil=898 val=1`, `[GW] try range 892~910`,  
       `[GW] mapped coil=898 -> slave=1 fc=05 sub_coil=0` 또는  
       `[GW] no mapping for coil=898`, `[GW] ApplyWrite failed coil=898`  
     - 898/899 수신 시 위와 같은 로그로 “매핑됨” vs “ApplyWrite 실패” 구분 가능.

---

## 4. 수정된 gateway 코드 요약

| 파일 | 내용 |
|------|------|
| **upstream_slave_h2tech.c** | ApplyWrite 실패 시 `response[1] = EX_SLAVE_DEVICE_FAIL`(0x04). FC05_COIL_DIAG_LOG 시 상세 로그 호출. |
| **h2tech_address_map.c** | 파일 헤더 주석 수정: 0899/0900이 테이블에 포함됨을 명시. |
| **gateway_write_log.h, modbus_master_log.c** | FC05_COIL_DIAG_LOG용 `Gateway_LogFc05Diag*` 선언/구현. |
| **app_config.h** | `FC05_COIL_DIAG_LOG` (기본 0) 추가. |

---

## 5. HPSB RELAY1: FC05 vs FC06 검증

| 구분 | 내용 |
|------|------|
| **매핑 문서** | HPSB relay는 coil write(FC05). 1x0899 = HPSB coil 0. |
| **PC 프로그램** | HPSB RELAY1 EN → `write_sub_coil(addr, value)` → **FC05** (write_coil). |
| **Mainboard** | 898→h2_dec 899 → WR_HPSB_COIL_0, **H2_ACT_WRITE_SUB_COIL** → **FC05** (ModbusMaster_WriteCoil). |

→ **세 곳 모두 HPSB RELAY1은 FC05(coil write)** 로 일치. FC06으로 바꿀 필요 없음.

---

## 6. PC 프로그램 주소 정의 출처

| 출처 | 내용 |
|------|------|
| **address_map.py** | `SUB_HPSB_COIL_BASE = 898`, `SUB_LPSB_COIL_BASE = 901`. RELAY1 = 898+0 = **898**. |
| **ui_main.py** | `addr = SUB_HPSB_COIL_BASE + idx` → RELAY1=898, RELAY2=899, RELAY3=900. |
| **변환 규칙** | Mainboard는 `h2_dec = start_addr + 1`. 따라서 PC가 898을 보내면 h2_dec 899 = HPSB coil 0. |

매핑 문서의 “1x0899 = HPSB coil 0”과 동일하게 맞추려면 **Modbus start_addr 898**을 사용하는 현재 정의가 맞음.

---

## 7. 테스트 절차

1. **HPSB 1대만** USART2(RS485)로 Mainboard에 연결.
2. Mainboard: `FC05_COIL_DIAG_LOG=1` 로 빌드 후 플래시 (필요 시 별도 시리얼로 [GW] 로그 확인).
3. PC 툴: HPSB RELAY1 ON/OFF 반복 (addr=898).
4. **기대 로그**
   - 메인보드(FC05_COIL_DIAG_LOG=1):  
     `[GW] FC05 recv coil=898 val=1`  
     `[GW] try range 892~910`  
     `[GW] mapped coil=898 -> slave=1 fc=05 sub_coil=0`  
   - 하위버스 성공 시: PC에서 RX OK, HPSB LED2(또는 해당 릴레이) 반응.
   - 하위버스 실패 시: 메인보드 `[GW] ApplyWrite failed coil=898`, PC에서는 **exception 0x04** (Slave Device Failure) 수신.

---

## 8. 성공 로그 예시

**PC 툴**
```
[DEBUG] Button: HPSB RELAY1 EN -> FC05 addr=898 val=1
[HPSB] TX FC05 addr=898 val=1 unit=9
[HPSB] RX OK
[HPSB] Write | addr=FC05 ... Response: OK
```

**Mainboard (FC05_COIL_DIAG_LOG=1, 별도 시리얼)**
```
[GW] FC05 recv coil=898 val=1
[GW] try range 892~910
[GW] mapped coil=898 -> slave=1 fc=05 sub_coil=0
```

**하위버스 실패 시 (주소는 유효, HPSB 미연결/타임아웃 등)**
- Mainboard: `[GW] ApplyWrite failed coil=898`
- PC: `[HPSB] RX EXC 0x04` (Slave Device Failure)
