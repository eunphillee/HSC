/**
 * @file output_state_nvm.h
 * @brief EEPROM A/B dual-block: 출력 상태(Mainboard relay, virtual bit, HPSB relay, LPSB SSR) 저장·복원.
 *
 * EEPROM 레이아웃 (AT24C02, 256 bytes):
 *   0x00~0x3F : SystemConfig A/B (기존)
 *   0x40~0x5F : OutputState block A (32 bytes)
 *   0x60~0x7F : OutputState block B (32 bytes)
 *
 * 저장 원칙:
 *   - 출력 상태가 실제로 변경된 경우에만 저장 (같은 값 재저장 방지)
 *   - A/B 교대 쓰기 + sequence counter → 최신 블록 선택
 *   - magic(0x4F53 "OS") + version + CRC16 검증
 *   - 비정상 데이터 → 기본값(Main relay 0,0,0,0 / 하위보드 0) 적용
 * 부팅 정책:
 *   - Mainboard RELAY1~4: 전원 인가마다 GPIO는 0,0,0,0 (EEPROM 저장값으로 복원하지 않음)
 *   - HPSB/LPSB: EEPROM에서 로드한 값으로 comm_ok 시 복원 (기존 유지)
 */
#ifndef OUTPUT_STATE_NVM_H
#define OUTPUT_STATE_NVM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- EEPROM 블록 위치 ---- */
#define OUTPUT_STATE_NVM_MAGIC          0x4F53u  /* "OS" */
#define OUTPUT_STATE_NVM_VERSION        1u
#define OUTPUT_STATE_NVM_BLOCK_A_BASE   0x40u
#define OUTPUT_STATE_NVM_BLOCK_B_BASE   0x60u
#define OUTPUT_STATE_NVM_BLOCK_SIZE     32u
#define OUTPUT_STATE_NVM_PAYLOAD_BYTES  24u  /* CRC 계산 범위: magic~virtual_coil */
#define OUTPUT_STATE_NVM_STRUCT_BYTES   26u  /* payload(24) + crc(2) */
#define OUTPUT_STATE_NVM_STORED_BYTES   28u  /* seq(2) + struct(26) */

/**
 * 저장 구조체 (26 bytes, __packed).
 * PAYLOAD_BYTES = 24 = offsetof(crc).
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;           /* OUTPUT_STATE_NVM_MAGIC */
    uint8_t  version;         /* OUTPUT_STATE_NVM_VERSION */
    uint8_t  _pad;            /* 정렬 예비 */
    uint8_t  main_relay[4];   /* Mainboard relay1~4 state (0/1) */
    uint8_t  virtual_coil[4]; /* FC05 20~23 virtual coil state (0/1) */
    uint8_t  hpsb_relay[3];   /* HPSB relay1~3 state */
    uint8_t  lpsb2_ssr[3];    /* LPSB slave-id=2 SSR1~3 */
    uint8_t  lpsb4_ssr[3];    /* LPSB slave-id=4 SSR1~3 */
    uint8_t  lpsb8_ssr[3];    /* LPSB slave-id=8 SSR1~3 */
    uint16_t crc;             /* CRC16 over first OUTPUT_STATE_NVM_PAYLOAD_BYTES bytes */
} output_state_nvm_t;
/* sizeof: 2+1+1+4+4+3+3+3+3+2 = 26 ✓  PAYLOAD_BYTES = 24 ✓ */

/** 부팅 시 1회: EEPROM에서 로드. 비정상이면 기본값. */
int OutputStateNvm_Load(output_state_nvm_t *state);

/** 출력 상태를 EEPROM에 저장 (변경 없으면 skip). Returns 0 on success. */
int OutputStateNvm_Save(const output_state_nvm_t *state);

/** 안전 기본값 설정 (main_relay 0,0,0,0). */
void OutputStateNvm_SetDefaults(output_state_nvm_t *state);

/** RAM 캐시 기준으로 메인보드 relay 출력 즉시 적용 (부팅 시 호출). */
void OutputStateNvm_ApplyMainboardRelays(const output_state_nvm_t *state);

/**
 * Mainboard relay 변경 알림 (FC05 성공 시 호출).
 * ch=0..3, value=0/1. 변경된 경우에만 EEPROM 저장.
 */
void OutputStateNvm_NotifyMainRelay(uint8_t ch, uint8_t value);

/**
 * virtual coil(FC05 20~23) 변경 알림.
 * ch=0..3, value=0/1. 변경된 경우에만 EEPROM 저장.
 */
void OutputStateNvm_NotifyVirtualCoil(uint8_t ch, uint8_t value);

/**
 * [target-first] 하위보드 coil target 갱신 + EEPROM 저장.
 * Gateway_Action_WriteSubCoil에서 FC05 전송 **전**에 호출.
 * EEPROM에 "원하는 상태(target)"를 먼저 기록한 뒤 hardware write.
 * slave_id=1/2/4/8, coil_index=0..2, value=0/1.
 * Returns 0=target saved OK, -1=save failed (g_state RAM은 갱신됨).
 */
int OutputStateNvm_SetSubCoilTarget(uint8_t slave_id, uint16_t coil_index, uint8_t value);

/**
 * 메인 루프에서 주기적으로 호출.
 * 하위보드가 comm_ok 전환 시 저장 상태를 FC05로 재적용.
 */
void OutputStateNvm_RestoreSubBoardsIfNeeded(void);

/** Poll 이후 InputReg 피드백으로 RESTORE_OK_MASK 갱신 (TRY 완료 슬레이브만). */
void OutputStateNvm_UpdateRestoreOkFromFeedback(void);

/**
 * 메인 루프에서 주기적으로 호출 (1~2초 간격 권장).
 * ON 상태 출력이 있는 슬레이브에 대해 on-demand poll 요청을 발행해
 * HPSB/LPSB의 통신 watchdog이 만료되지 않도록 유지한다.
 */
void OutputStateNvm_KeepAliveIfNeeded(void);

/**
 * 메인 루프에서 주기적으로 호출 (5초 간격 권장).
 * EEPROM 저장 실패(g_eeprom_dirty=1) 시 재시도한다.
 * I2C 일시적 오류로 저장이 누락되면 전원 사이클 후 상태가 초기화되는 것을 방지.
 */
void OutputStateNvm_FlushIfDirty(void);

/**
 * [주기 동기화] target(g_state) vs actual(FC04 InputReg) 비교.
 * 불일치 채널은 FC05 재시도. OUTPUT_STATE_SYNC_MAX_RETRY 연속 실패 시 fault 마킹.
 * SystemSync_Update에서 OUTPUT_STATE_SYNC_INTERVAL_MS 주기로 호출.
 * Returns 이번 호출에서 감지된 mismatch 채널 수(0=전체 일치).
 */
uint8_t OutputStateNvm_SyncTargetActual(void);

/**
 * 동기화 fault 비트맵 반환.
 * bit[0..2]  : HPSB relay1..3 (sid=1)
 * bit[3..5]  : LPSB(sid=2) SSR1..3
 * bit[6..8]  : LPSB(sid=4) SSR1..3
 * bit[9..11] : LPSB(sid=8) SSR1..3
 * 해당 bit=1 이면 MAX_RETRY 연속 불일치(write 실패 또는 하위보드 응답 없음).
 */
uint16_t OutputStateNvm_GetSyncFaultBits(void);

/** 현재 RAM 캐시 포인터 반환 (미로드 시 NULL). */
const output_state_nvm_t *OutputStateNvm_Get(void);

/** EEPROM 미반영 여부: 1 = RAM이 EEPROM보다 최신 (Save 실패), 0 = 동기화됨. */
int OutputStateNvm_IsEepromDirty(void);

/** 현재 EEPROM 저장 횟수(sequence). 부팅 직후 EEPROM에 저장된 seq가 반환됨. */
uint16_t OutputStateNvm_GetSequence(void);

/** 마지막 Save 결과: 0=OK, 1=ERR */
uint16_t OutputStateNvm_GetLastSaveResult(void);

/** 마지막 Load 결과: 0=OK, 1=ERR */
uint16_t OutputStateNvm_GetLastLoadResult(void);

/** 복원 시도(1패스) 완료 마스크: bit0=HPSB, bit1=LPSB2, bit2=LPSB4, bit3=LPSB8 */
uint16_t OutputStateNvm_GetRestoreDoneMask(void);

/** InputReg(2..4)가 want와 일치할 때만 set: 동일 비트 */
uint16_t OutputStateNvm_GetRestoreOkMask(void);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_STATE_NVM_H */
