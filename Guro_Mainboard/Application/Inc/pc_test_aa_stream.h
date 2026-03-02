/**
 * @file pc_test_aa_stream.h
 * @brief PC 테스트: UART1 RS485로 0xAA 500ms 주기 송신 + LED2 40ms 펄스.
 *        ENABLE_PC_TEST_AA_STREAM=1 일 때만 사용. Modbus와 무관하게 송신 경로/DE/PC 수신 검증용.
 */
#ifndef PC_TEST_AA_STREAM_H
#define PC_TEST_AA_STREAM_H

#include "aggregated_status.h"

#ifdef __cplusplus
extern "C" {
#endif

void PcTestAA_Init(void);
void PcTestAA_Tick(const aggregated_status_t *agg);

#ifdef __cplusplus
}
#endif

#endif /* PC_TEST_AA_STREAM_H */
