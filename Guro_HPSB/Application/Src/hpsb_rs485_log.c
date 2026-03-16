/**
 * @file hpsb_rs485_log.c
 * @brief HPSB RS485 debug log output.
 *
 * HPSB_RS485_Log() 은 modbus_slave.c 에서 weak 심볼로 선언되어 있으며,
 * 여기서 강한 구현을 제공하여 실제 UART로 문자열을 출력한다.
 *
 * 주의: 이 구현은 USART1(huart1)을 사용하므로, RS485 버스(Mainboard)와
 * 직접 연결된 상태에서 로그를 켜면 Modbus 프레임과 문자열이 섞일 수 있다.
 * 반드시 디버그 전용 연결(예: 별도 USB-UART 어댑터)에서만 사용하거나,
 * 문제 분석 후 HPSB_RS485_DEBUG_LOG 를 0으로 끄고 사용한다.
 */

#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

void HPSB_RS485_Log(const char *msg)
{
    if (!msg) return;
    size_t len = strlen(msg);
    if (len == 0) return;

    /* Best-effort: 디버그용이므로 전송 실패는 무시 */
    (void)HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)len, 100);
}

