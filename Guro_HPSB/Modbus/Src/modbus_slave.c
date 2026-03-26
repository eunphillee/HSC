/**
 * @file modbus_slave.c
 * @brief HPSB: Modbus Slave - receive, dispatch FC04(read)/FC05(write) only per Unified Rule v1.2.
 *        FC01/02/03/06/0F/10 → EX_ILLEGAL_FUNCTION. LSB-first bit order.
 *        MAX3485 idle = receive = DE LOW, /RE LOW → RS485_DE_Pin(GPIOA, 현재 PA11) LOW when HPSB_RS485_DE_INVERTED=0.
 */
#include "modbus_slave.h"
#include "modbus_rtu.h"
#include "modbus_cfg.h"
#include "modbus_table.h"
#include "io_map.h"
#include "led_status.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define EX_ILLEGAL_FUNCTION      0x01u
#define EX_ILLEGAL_DATA_ADDRESS  0x02u
#define EX_ILLEGAL_DATA_VALUE    0x03u
#define EX_SLAVE_DEVICE_FAILURE  0x04u

extern UART_HandleTypeDef huart1;

/* RS485/HPSB 공통 로그 함수: 구현이 없으면 no-op. RS485 direction/PA11 디버그에서 사용. */
__attribute__((weak)) void HPSB_RS485_Log(const char *msg) { (void)msg; }

/** 수신 raw 프레임/디버그 출력용. weak: SWO·별도 UART 등으로 오버라이드 가능. 기본 no-op(버스 영향 없음). */
__attribute__((weak)) void HPSB_Debug_Log(const char *msg) { (void)msg; }

static uint8_t rx_buf[MODBUS_RTU_RX_BUF_SIZE];
static uint16_t rx_len;
static uint32_t last_rx_tick;
static uint8_t s_rx_byte;

/* Direct Modbus 무응답 디버그: 디버거에서 watch. rx / parse / reply 단계 구분. */
volatile uint16_t HPSB_dbg_rx_len;
volatile uint8_t  HPSB_dbg_crc_ok;
volatile uint8_t  HPSB_dbg_slave_match;
volatile uint8_t  HPSB_dbg_reply_started;
volatile uint8_t  HPSB_dbg_reply_done;
/* Modbus RTU t3.5 at 9600 bps ≈ 3.65 ms; use 4 ms for frame end detection (mainboard RX timeout와 충돌하지 않도록) */
#define FRAME_SILENCE_MS  4

/* UART error flag clear helper: ensure RX state is clean between frames. */
static void uart_clear_errors(void)
{
    /* Clear overrun, framing, and noise errors if set. */
    __HAL_UART_CLEAR_FLAG(&MODBUS_UART, UART_FLAG_ORE);
    __HAL_UART_CLEAR_FLAG(&MODBUS_UART, UART_FLAG_FE);
    __HAL_UART_CLEAR_FLAG(&MODBUS_UART, UART_FLAG_NE);
}

/* RS485_DE 핀(DE/RE): MAX3485 receive = DE=LOW,/RE=LOW. INVERTED=0 → RS485_DE LOW=수신, HIGH=송신. INVERTED=1 → 보드 인버터 시 HIGH=수신. */
#if HPSB_RS485_DE_INVERTED
static void set_de_tx(void) {
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] TX mode enable\r\n");
#endif
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET);
}
static void set_de_rx(void) {
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] RX mode enable\r\n");
#endif
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET);
}
#else
static void set_de_tx(void) {
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] TX mode enable\r\n");
#endif
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET);
}
static void set_de_rx(void) {
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] RX mode enable\r\n");
#endif
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET);
}
#endif

/* send_response(): DE만 토글 (HPSB_RS485_Log 없음 — TX 직전 버스/타이밍 보호) */
#if HPSB_RS485_DE_INVERTED
static void modbus_rs485_de_tx_hw(void)
{
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET);
}
static void modbus_rs485_de_rx_hw(void)
{
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET);
}
#else
static void modbus_rs485_de_tx_hw(void)
{
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_SET);
}
static void modbus_rs485_de_rx_hw(void)
{
    HAL_GPIO_WritePin(MODBUS_DE_GPIO_PORT, MODBUS_DE_GPIO_PIN, GPIO_PIN_RESET);
}
#endif

/* (LED1/2/3 debug tick 변수 제거: LED_Status_Tick_1ms()가 RELAY 상태를 직접 반영) */

/* LED1/2/3 는 led_status.c 의 Tick 에서 RELAY1/2/3 상태를 직접 반영하므로
   debug pulse 함수는 no-op 으로 처리한다. */
static void dbg_led1_pulse_ms(uint32_t ms)   { (void)ms; }
static void dbg_led3_pulse_ms(uint32_t ms)   { (void)ms; }

/* (debug blink 함수 제거: LED1/2/3 은 led_status.c Tick 에서 RELAY 상태 반영) */
static void dbg_led1_blink3_start(void) { }
static void dbg_led2_blink3_start(void) { }

/* LED1/2/3 는 LED_Status_Tick_1ms() 가 RELAY 상태를 매 루프 갱신하므로
   별도 debug blink 는 실행하지 않는다. */
void ModbusSlave_ProcessDebugLEDs(void) { }

#if HPSB_RS485_DEBUG_LOG
static void log_rs485(const char *fmt, ...) {
    char buf[80];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) HPSB_RS485_Log(buf);
}
#else
#define log_rs485(...) ((void)0)
#endif

#if HPSB_PA8_TRACE
void ModbusSlave_PA8_Log(const char *msg) { if (msg) HPSB_RS485_Log(msg); }
#else
void ModbusSlave_PA8_Log(const char *msg) { (void)msg; }
#endif

/** Raw 수신 프레임을 hex 문자열로 출력. HPSB_Debug_Log()로 전달 (weak 구현 시 no-op). */
__attribute__((unused))
static void print_hex(const char *prefix, const uint8_t *buf, uint16_t len)
{
    char line[80];
    uint16_t i, n;
    int off;
    if (buf == NULL) return;
    off = snprintf(line, sizeof(line), "%s len=%u ", prefix ? prefix : "", (unsigned)len);
    if (off < 0 || (size_t)off >= sizeof(line)) return;
    n = (uint16_t)((sizeof(line) - (size_t)off - 1) / 3);
    if (n > len) n = len;
    for (i = 0; i < n; i++)
        off += snprintf(line + off, (size_t)(sizeof(line) - (size_t)off), "%02X ", buf[i]);
    if (off > 0) {
        line[off] = '\0';
        HPSB_Debug_Log(line);
        HPSB_Debug_Log("\r\n");
    }
    if (n < len) {
        off = 0;
        for (i = n; i < len && (size_t)off < sizeof(line) - 4; i++)
            off += snprintf(line + off, (size_t)(sizeof(line) - (size_t)off), "%02X ", buf[i]);
        if (off > 0) {
            line[off] = '\0';
            HPSB_Debug_Log(line);
            HPSB_Debug_Log("\r\n");
        }
    }
}

/* Debug helper: one-line raw dump for PC side parsing */
static void log_rx_raw_one_line(void)
{
    char line[220];
    int off = snprintf(line, sizeof(line), "[HPSB-SLAVE] rx raw len=%u data=",
                       (unsigned)rx_len);
    if (off < 0 || (size_t)off >= sizeof(line)) return;
    for (uint16_t i = 0; i < rx_len; i++) {
        if (off > (int)(sizeof(line) - 5)) break;
        off += snprintf(line + off, sizeof(line) - (size_t)off, "%02X%s",
                        (unsigned)rx_buf[i], (i + 1u < rx_len) ? " " : "\r\n");
    }
    HPSB_RS485_Log(line);
}

void ModbusSlave_Init(void)
{
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] ModbusSlave_Init start\r\n");
#endif
    rx_len = 0;
    last_rx_tick = 0;
    set_de_rx();  /* idle = receive: DE/RE LOW (or HIGH if INVERTED) */
    uart_clear_errors();
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] set RX mode (init)\r\n");
#endif
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] ModbusSlave_Init end (RX mode)\r\n");
#endif
    (void)HAL_UART_Receive_IT(&MODBUS_UART, &s_rx_byte, 1u);
}

static void send_response(uint8_t *pdu, size_t pdu_len)
{
    char l[64];
    int ln = snprintf(l, sizeof(l), "[HPSB-SLAVE] tx resp len=%u\r\n", (unsigned)(pdu_len + 2u));
    if (ln > 0) HPSB_Debug_Log(l);
    HPSB_dbg_reply_started = 1;
    HPSB_dbg_reply_done = 0;
    ModbusRTU_AppendCRC(pdu, pdu_len);

    (void)HAL_UART_AbortReceive_IT(&MODBUS_UART);

    modbus_rs485_de_tx_hw();
    HAL_Delay(1);
    HAL_UART_Transmit(&MODBUS_UART, pdu, (uint16_t)(pdu_len + 2), 100);
    while (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_TC) == RESET) { }

    modbus_rs485_de_rx_hw();

    /* TX 완료 후 LED4 RS485 activity 알림 */
    LED_Status_OnRS485Activity();
    HPSB_dbg_reply_started = 0;
    HPSB_dbg_reply_done = 1;

    /* TX 이전에 두었던 HPSB_RS485_Log / log_rs485 는 DE=RX 복귀 및 송신 완료 후에만 출력 */
    HPSB_RS485_Log("[HPSB-SLAVE] send response\r\n");
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] send_response start\r\n");
#endif
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] set TX mode\r\n");
#endif
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] TX mode enable\r\n");
#endif
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] TX start len=%u\r\n", (unsigned)(pdu_len + 2));
#endif
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] TX complete\r\n");
#endif
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] after send_response back to RX\r\n");
#endif
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] back to RX mode\r\n");
#endif
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] RX mode enable\r\n");
#endif

    (void)HAL_UART_Receive_IT(&MODBUS_UART, &s_rx_byte, 1u);
}

static void send_exception(uint8_t req_fc, uint8_t ex_code, const char *reason)
{
    uint8_t pdu[8];
    char dbg[96];
    pdu[0] = MODBUS_SLAVE_ADDR;
    pdu[1] = (uint8_t)(req_fc | 0x80u);
    pdu[2] = ex_code;
    if (reason == NULL) reason = "?";
    (void)snprintf(dbg, sizeof(dbg), "[HPSB-SLAVE] exception 0x%02X reason=%s\r\n", (unsigned)ex_code, reason);
    HPSB_Debug_Log(dbg);
    (void)snprintf(dbg, sizeof(dbg), "[HPSB-SLAVE] tx resp len=%u\r\n", 5u);
    HPSB_Debug_Log(dbg);
    send_response(pdu, 3u);
}

static void process_frame(void)
{
    set_de_rx();  /* 실패/early return 모든 경로에서 RX 복귀 보장 */
    /* 디버그 상태 초기화 (LED2=바이트수신, LED3=CRC통과, LED4=slave일치, LED1=응답송신) */
    HPSB_dbg_rx_len = rx_len;
    HPSB_dbg_crc_ok = 0;
    HPSB_dbg_slave_match = 0;
    HPSB_dbg_reply_started = 0;
    HPSB_dbg_reply_done = 0;
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] process_frame start ensure RX\r\n");
#endif
    /* 수신 raw 프레임 로그: byte stream 원문 비교용 */
    log_rx_raw_one_line();

    /* slave id 디버그(요청 1바이트 vs 내 주소) */
    if (rx_len >= 1u) {
        char dbg_id[96];
        (void)snprintf(dbg_id, sizeof(dbg_id),
                       "[HPSB-SLAVE] my_slave_id=%u req_slave_id=%u\r\n",
                       (unsigned)MODBUS_SLAVE_ADDR, (unsigned)rx_buf[0]);
        HPSB_RS485_Log(dbg_id);
    } else {
        char dbg_id[96];
        (void)snprintf(dbg_id, sizeof(dbg_id),
                       "[HPSB-SLAVE] my_slave_id=%u req_slave_id=NA\r\n",
                       (unsigned)MODBUS_SLAVE_ADDR);
        HPSB_RS485_Log(dbg_id);
    }

    /* 1) 첫 바이트 수신: LED1 비블로킹 표시 (수신 경로에서 HAL_Delay 금지) */
    dbg_led1_pulse_ms(300);
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] frame complete len=%u\r\n", (unsigned)rx_len);
#endif
    if (rx_len < 4) {
        HPSB_Debug_Log("[HPSB] rx_len<4 invalid\r\n");
#if 1
        HPSB_RS485_Log("[HPSB-SLAVE] drop reason=rx_len<4\r\n");
        /* FC05는 요청 프레임이 불완전해도 timeout을 피하기 위해 예외 응답을 보낸다. */
        if (rx_len >= 2u && rx_buf[1] == 0x05u) {
            HPSB_RS485_Log("[HPSB-SLAVE] FC05 exception send (rx_len<4)\r\n");
            send_exception(0x05u, EX_SLAVE_DEVICE_FAILURE, "rx_len<4 (debug)");
            return;
        }
#endif
#if HPSB_RS485_DEBUG_LOG
        log_rs485("[HPSB_RS485] no response invalid frame len\r\n");
#endif
        set_de_rx();
        return;
    }
    /* slave_id, FC 로그 (A/B/C/D 구분용) */
    {
        char dbg[64];
        (void)snprintf(dbg, sizeof(dbg), "[HPSB] slave_id=%u fc=0x%02X\r\n", (unsigned)rx_buf[0], (unsigned)rx_buf[1]);
        HPSB_Debug_Log(dbg);
    }
    {
        uint8_t req_fc = (rx_len >= 2u) ? rx_buf[1] : 0u;
        if (rx_buf[0] != MODBUS_SLAVE_ADDR) {
            HPSB_Debug_Log("[HPSB] A. slave id mismatch\r\n");
#if 1
            /* FC05는 디버깅 단계에서 "무응답"을 없애기 위해,
             * slave id mismatch여도 예외응답을 전송한다. */
            if (req_fc == 0x05u && rx_len >= 2u) {
                HPSB_RS485_Log("[HPSB-SLAVE] drop reason=slave id mismatch (fc05)\r\n");
                send_exception(req_fc, EX_SLAVE_DEVICE_FAILURE, "slave id mismatch (debug)");
                return;
            }
#endif
#if HPSB_RS485_DEBUG_LOG
        log_rs485("[HPSB_RS485] no response slave id mismatch (got %u)\r\n", (unsigned)rx_buf[0]);
#endif
            HPSB_RS485_Log("[HPSB-SLAVE] drop reason=slave id mismatch\r\n");
            set_de_rx();
            return;
        }
    }
    HPSB_dbg_slave_match = 1;
#if HPSB_RS485_DEBUG_LOG
    log_rs485("[HPSB_RS485] slave id matched\r\n");
#endif
    /* CRC: 수신 2바이트 vs 계산값 출력 (실패 시 원인 확인용). */
    {
        uint16_t rx_crc = (uint16_t)(rx_buf[rx_len - 2] | (rx_buf[rx_len - 1] << 8));
        uint16_t calc_crc = ModbusRTU_CRC16(rx_buf, rx_len - 2);
        char dbg[72];
        (void)snprintf(dbg, sizeof(dbg), "[HPSB] rx_crc=0x%04X calc_crc=0x%04X %s\r\n",
            (unsigned)rx_crc, (unsigned)calc_crc,
            (rx_crc == calc_crc) ? "OK" : "FAIL");
        HPSB_Debug_Log(dbg);
    }
    if (ModbusRTU_CRC16Check(rx_buf, rx_len) != 0) {
        HPSB_dbg_crc_ok = 0;
        HPSB_Debug_Log("[HPSB] B. CRC fail (see rx_crc vs calc_crc above)\r\n");
#if 1
        /* FC05는 디버깅 단계에서 "무응답"을 없애기 위해,
         * CRC fail이어도 예외응답을 전송한다. */
        if (rx_len >= 2u && rx_buf[1] == 0x05u) {
            HPSB_RS485_Log("[HPSB-SLAVE] drop reason=CRC fail (fc05)\r\n");
            send_exception(0x05u, EX_SLAVE_DEVICE_FAILURE, "CRC fail (debug)");
            return;
        }
#endif
#if HPSB_RS485_DEBUG_LOG
        log_rs485("[HPSB_RS485] no response CRC fail\r\n");
#endif
        /* CRC fail: LED1 triple blink (non-blocking) */
        dbg_led1_blink3_start();
        HPSB_RS485_Log("[HPSB-SLAVE] drop reason=CRC fail\r\n");
        set_de_rx();
        return;
    }
    HPSB_dbg_crc_ok = 1;
    HPSB_Debug_Log("[HPSB] CRC pass\r\n");

    uint8_t fc = rx_buf[1];
    uint8_t tx_pdu[MODBUS_MAX_PDU_LEN];
    size_t tx_len = 0;
    {
        uint16_t addr = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
        uint16_t cv = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
        char dbg[96];
        (void)snprintf(dbg, sizeof(dbg), "[HPSB-SLAVE] parsed fc=%02X addr=%u val/count=%u\r\n",
                       (unsigned)fc, (unsigned)addr, (unsigned)cv);
        HPSB_Debug_Log(dbg);
    }

    switch (fc) {
        /* Unified Rule v1.0: Read only FC04, Control only FC05.
         * FC01/FC02/FC03 read paths are disabled (exception response). */
        case 0x01: {
            send_exception(fc, EX_ILLEGAL_FUNCTION, "fc01 disabled (Unified Rule: FC04 read only)");
            break;
        }
        case 0x02: {
            send_exception(fc, EX_ILLEGAL_FUNCTION, "fc02 disabled (Unified Rule: FC04 read only)");
            break;
        }
        case 0x03: {
            send_exception(fc, EX_ILLEGAL_FUNCTION, "fc03 disabled (Unified Rule: FC04 read only)");
            break;
        }
        case 0x04: {
            uint16_t start = (uint16_t)((rx_buf[2] << 8) | rx_buf[3]);
            uint16_t num   = (uint16_t)((rx_buf[4] << 8) | rx_buf[5]);
            ModbusTable_RefreshInputRegs();
            if (start + num > INPUT_REG_COUNT) { send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc04 addr out of range"); break; }
            uint16_t regs[INPUT_REG_COUNT];
            for (uint16_t i = 0; i < num; i++) regs[i] = ModbusTable_GetInputReg(start + i);
            tx_len = ModbusRTU_BuildFC04Response(tx_pdu, MODBUS_SLAVE_ADDR, regs, num);
            HPSB_Debug_Log("[HPSB] FC04 map ready\r\n");
            send_response(tx_pdu, tx_len);
            break;
        }
        case 0x05: {
                HPSB_RS485_Log("[HPSB-SLAVE] ENTER FC05\r\n");
            uint16_t coil_addr; uint8_t value;
            uint8_t v_hi = rx_buf[4];
            uint8_t v_lo = rx_buf[5];
            {
                char dbg[96];
                (void)snprintf(dbg, sizeof(dbg),
                    "[HPSB-SLAVE] parsed fc=05 addr=%u val_raw=0x%02X%02X\r\n",
                    (unsigned)((rx_buf[2] << 8) | rx_buf[3]), (unsigned)v_hi, (unsigned)v_lo);
                HPSB_Debug_Log(dbg);
            }
            if (ModbusRTU_ParseFC05Request(rx_buf, rx_len, &coil_addr, &value) != 0) {
                HPSB_RS485_Log("[HPSB-SLAVE] drop reason=FC05 parse fail\r\n");
                HPSB_Debug_Log("[HPSB] C. FC05 parse fail\r\n");
#if HPSB_RS485_DEBUG_LOG
                log_rs485("[HPSB_RS485] fc05 parse fail\r\n");
#endif
                /* FC05 parse fail: LED2 triple blink (non-blocking) */
                dbg_led2_blink3_start();
                send_exception(fc, EX_ILLEGAL_DATA_VALUE, "invalid coil value (expect FF00/0000)");
                set_de_rx();
                break;
            }
            {
                char dbg[56];
                (void)snprintf(dbg, sizeof(dbg), "[HPSB] FC05 coil_addr=%u value=%u\r\n", (unsigned)coil_addr, (unsigned)value);
                HPSB_Debug_Log(dbg);
            }
            {
                char dbg2[80];
                (void)snprintf(dbg2, sizeof(dbg2), "[HPSB-SLAVE] parsed addr=%u value=%u\r\n",
                                (unsigned)coil_addr, (unsigned)value);
                HPSB_Debug_Log(dbg2);
            }
#if HPSB_RS485_DEBUG_LOG
            log_rs485("[HPSB_RS485] fc05 addr=%u value=%u\r\n", (unsigned)coil_addr, (unsigned)value);
#endif
            if (coil_addr > 3u) {
                HPSB_RS485_Log("[HPSB-SLAVE] drop reason=FC05 coil_addr out of range (0..3 only)\r\n");
                HPSB_Debug_Log("[HPSB] coil_addr out of range (0..3 only)\r\n");
#if HPSB_RS485_DEBUG_LOG
                log_rs485("[HPSB_RS485] exception response (invalid coil addr)\r\n");
#endif
                /* 잘못된 coil 주소: LED3 비블로킹 450ms */
                dbg_led3_pulse_ms(450);
                send_exception(fc, EX_ILLEGAL_DATA_ADDRESS, "fc05 coil out of range");
                set_de_rx();
                break;
            }
            if (coil_addr != 0u)
                HPSB_Debug_Log("[HPSB] D. coil_addr != 0\r\n");
            else
                HPSB_Debug_Log("[HPSB] FC05 coil_addr==0 OK\r\n");
            /* coil_addr==0 success: LED3 hold ON 500ms. coil_addr!=0: short pulse. */
            dbg_led3_pulse_ms(coil_addr == 0u ? 500u : 300u);
#if HPSB_RS485_DEBUG_LOG
            log_rs485("[HPSB_RS485] before response\r\n");
#endif
            tx_len = ModbusRTU_BuildFC05Response(tx_pdu, MODBUS_SLAVE_ADDR, coil_addr, value);
            send_response(tx_pdu, tx_len);
            /* Important:
             * 응답 전 coil 상태를 먼저 바꾸면(특히 릴레이/SSR 전원이 RS485 트랜시버에 영향을 주는 보드일 때)
             * send_response 송신이 끊기면서 master가 0 received로 타임아웃 날 수 있다.
             * 그래서 coil 반영은 응답 전송 이후로 미룬다. */
            ModbusTable_SetCoil(coil_addr, value);
            HPSB_Debug_Log("[HPSB] FC05 coil write\r\n");
            break;
        }
        case 0x06:
        case 0x0F:
        case 0x10:
            /* Unified Rule v1.2: Write = FC05 ONLY. FC06/FC15/FC16 금지. */
            send_exception(fc, EX_ILLEGAL_FUNCTION, "write FC not allowed (v1.2: FC05 only)");
            break;
        default:
#if HPSB_RS485_DEBUG_LOG
            log_rs485("[HPSB_RS485] no response unsupported fc=0x%02X\r\n", (unsigned)fc);
#endif
            send_exception(fc, EX_ILLEGAL_FUNCTION, "unsupported function");
            set_de_rx();
            break;
    }
#if HPSB_PA8_TRACE
    HPSB_RS485_Log("[RS485] process_frame end\r\n");
#endif
    set_de_rx();  /* 모든 경로 후 RX 복귀 */
}

void ModbusSlave_Poll(void)
{
    /* If UART error flags are set, RX IT can stall. Clear and restart IT. */
    if (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_ORE) ||
        __HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_FE)  ||
        __HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_NE)) {
        uart_clear_errors();
        rx_len = 0;
        last_rx_tick = 0;
        set_de_rx();
        (void)HAL_UART_AbortReceive_IT(&MODBUS_UART);
        (void)HAL_UART_Receive_IT(&MODBUS_UART, &s_rx_byte, 1u);
    }
    if (rx_len > 0 && (HAL_GetTick() - last_rx_tick) >= FRAME_SILENCE_MS) {
#if HPSB_PA8_TRACE
        HPSB_RS485_Log("[RS485] poll frame timeout\r\n");
#endif
        /* Frame boundary detected. Pause RX IT to avoid mixing next request bytes into current frame while parsing. */
        (void)HAL_UART_AbortReceive_IT(&MODBUS_UART);
        process_frame();
        rx_len = 0;
        last_rx_tick = 0;
        uart_clear_errors();
        (void)HAL_UART_Receive_IT(&MODBUS_UART, &s_rx_byte, 1u);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &MODBUS_UART)
        return;
    last_rx_tick = HAL_GetTick();
    if (rx_len < MODBUS_RTU_RX_BUF_SIZE) {
        rx_buf[rx_len++] = s_rx_byte;
        LED_Status_OnRS485Activity();   /* RX 바이트 수신 시 LED4 activity 알림 */
    }
#if HPSB_RS485_DEBUG_LOG
    if (rx_len == 1u)
        log_rs485("[HPSB_RS485] frame byte received\r\n");
#endif
    (void)HAL_UART_Receive_IT(&MODBUS_UART, &s_rx_byte, 1u);
}

/* TX 경로 단독 검증: Modbus 파싱 없이 고정 8바이트(FC05 coil0 OFF) 송신. DE HIGH → TX → TC → DE LOW. */
void ModbusSlave_SendTestFrame(void)
{
    static const uint8_t test_frame[8] = { 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xCA };
    set_de_tx();
    for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }
    HAL_UART_Transmit(&MODBUS_UART, (uint8_t *)test_frame, 8, 100);
    while (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_TC) == RESET) { }
    set_de_rx();
}

/* RS485 송신 경로 단독 검증: 고정 문자열 송신. DE HIGH → UART TX → TC → DE LOW. (Modbus/CRC/slave 무관) */
void ModbusSlave_SendTestString(const char *str, uint16_t len)
{
    if (str == NULL || len == 0) return;

    /* ASCII 브리지 OKOK 테스트용 로그/DE 추적.
     * 목표 순서(OKOK 문자열 기준):
     *   DE=TX
     *   UART transmit("OKOK\r\n")
     *   TC wait
     *   DE=RX
     */
    set_de_tx();
#if HPSB_OKOK_STREAM_TEST
    /* payload 원문 비교를 위해 hex raw만 남김 (ASCII OKOK 문자열 라벨은 제외) */
    HPSB_RS485_Log("[HPSB-TX] DE=TX\r\n");
    HPSB_RS485_Log("[HPSB-TX-RAW] 4F 4B 4F 4B 0D 0A\r\n");
#endif
    for (volatile uint32_t d = 0; d < 500; d++) { (void)d; }
    HAL_UART_Transmit(&MODBUS_UART, (const uint8_t *)str, len, 100);
    while (__HAL_UART_GET_FLAG(&MODBUS_UART, UART_FLAG_TC) == RESET) { }

#if HPSB_OKOK_STREAM_TEST
    HPSB_RS485_Log("[HPSB-TX] DE=RX\r\n");
#endif
    set_de_rx();
}
