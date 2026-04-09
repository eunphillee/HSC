/**
 * @file output_state_nvm.c
 * @brief EEPROM output-state persistence using A/B blocks with sequence + CRC.
 */
#include "output_state_nvm.h"
#include "eeprom_24c02.h"
#include "gateway_actions.h"
#include "main.h"
#include "app_config.h"
#include "modbus_master.h"
#include "modbus_rtu.h"
#include "modbus_table.h"
#include <stdio.h>
#include <string.h>

#if OUTPUT_STATE_NVM_DEBUG_LOG
#define NVM_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define NVM_DEBUG_PRINTF(...) ((int)0)
#endif

static output_state_nvm_t g_state;
static uint16_t g_seq;
static uint8_t g_active_block;
static uint8_t g_loaded;
static uint8_t g_eeprom_dirty;
static uint8_t g_restored_once[4];
static uint16_t g_sync_fault_bits;
static uint16_t g_last_save_result;
static uint16_t g_last_load_result;
static uint16_t g_restore_done_mask;
static uint16_t g_restore_ok_mask;
/* 이전 comm_ok 상태 추적: 0→1 전환 감지로 슬레이브 재연결/재부팅 시 re-restore 수행 */
static uint8_t s_prev_comm_ok[4];

static uint16_t nvm_crc(const output_state_nvm_t *s)
{
    return ModbusRTU_CRC16((const uint8_t *)s, OUTPUT_STATE_NVM_PAYLOAD_BYTES);
}

static uint16_t nvm_base(uint8_t block_index)
{
    return (block_index == 0u) ? OUTPUT_STATE_NVM_BLOCK_A_BASE : OUTPUT_STATE_NVM_BLOCK_B_BASE;
}

static void nvm_pack(uint8_t *raw, uint16_t seq, const output_state_nvm_t *state)
{
    output_state_nvm_t tmp = *state;
    tmp.crc = nvm_crc(&tmp);
    raw[0] = (uint8_t)(seq & 0xFFu);
    raw[1] = (uint8_t)((seq >> 8) & 0xFFu);
    memcpy(&raw[2], &tmp, sizeof(tmp));
}

static void nvm_unpack(const uint8_t *raw, uint16_t *seq, output_state_nvm_t *state)
{
    *seq = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
    memcpy(state, &raw[2], sizeof(*state));
}

static int nvm_valid(const output_state_nvm_t *state)
{
    if (state->magic != OUTPUT_STATE_NVM_MAGIC) return 0;
    if (state->version != OUTPUT_STATE_NVM_VERSION) return 0;
    return (nvm_crc(state) == state->crc) ? 1 : 0;
}

static uint8_t *nvm_select_sub_target(uint8_t slave_id)
{
    if (slave_id == 1u) return g_state.hpsb_relay;
    if (slave_id == 2u) return g_state.lpsb2_ssr;
    if (slave_id == 4u) return g_state.lpsb4_ssr;
    if (slave_id == 8u) return g_state.lpsb8_ssr;
    return NULL;
}

int OutputStateNvm_Load(output_state_nvm_t *state)
{
    uint8_t raw_a[OUTPUT_STATE_NVM_STORED_BYTES];
    uint8_t raw_b[OUTPUT_STATE_NVM_STORED_BYTES];
    output_state_nvm_t a;
    output_state_nvm_t b;
    uint16_t seq_a = 0u;
    uint16_t seq_b = 0u;
    int valid_a = 0;
    int valid_b = 0;

    if (EEPROM_Read(OUTPUT_STATE_NVM_BLOCK_A_BASE, raw_a, OUTPUT_STATE_NVM_STORED_BYTES) == 0) {
        nvm_unpack(raw_a, &seq_a, &a);
        valid_a = nvm_valid(&a);
    }
    if (EEPROM_Read(OUTPUT_STATE_NVM_BLOCK_B_BASE, raw_b, OUTPUT_STATE_NVM_STORED_BYTES) == 0) {
        nvm_unpack(raw_b, &seq_b, &b);
        valid_b = nvm_valid(&b);
    }

    if (valid_a && valid_b) {
        if (seq_a >= seq_b) {
            g_state = a;
            g_seq = seq_a;
            g_active_block = 0u;
        } else {
            g_state = b;
            g_seq = seq_b;
            g_active_block = 1u;
        }
    } else if (valid_a) {
        g_state = a;
        g_seq = seq_a;
        g_active_block = 0u;
    } else if (valid_b) {
        g_state = b;
        g_seq = seq_b;
        g_active_block = 1u;
    } else {
        OutputStateNvm_SetDefaults(&g_state);
        g_seq = 1u;
        g_active_block = 0u;
        if (OutputStateNvm_Save(&g_state) != 0) {
            g_last_load_result = 1u;
            (void)NVM_DEBUG_PRINTF("[NVM] load fail\r\n");
            return -1;
        }
    }

    g_loaded = 1u;
    g_eeprom_dirty = 0u;
    g_sync_fault_bits = 0u;
    g_last_load_result = 0u;
    g_restore_done_mask = 0u;
    g_restore_ok_mask = 0u;
    memset(g_restored_once, 0, sizeof(g_restored_once));
    memset(s_prev_comm_ok, 0, sizeof(s_prev_comm_ok));
    if (state) *state = g_state;
    (void)NVM_DEBUG_PRINTF("[NVM] load ok hpsb=%u,%u,%u lpsb2=%u,%u,%u blk=%u seq=%u\r\n",
                 (unsigned)g_state.hpsb_relay[0], (unsigned)g_state.hpsb_relay[1],
                 (unsigned)g_state.hpsb_relay[2],
                 (unsigned)g_state.lpsb2_ssr[0], (unsigned)g_state.lpsb2_ssr[1],
                 (unsigned)g_state.lpsb2_ssr[2],
                 (unsigned)g_active_block, (unsigned)g_seq);
    return 0;
}

int OutputStateNvm_Save(const output_state_nvm_t *state)
{
    uint8_t next_block;
    uint16_t next_seq;
    uint16_t base;
    uint8_t raw[OUTPUT_STATE_NVM_STORED_BYTES];
    uint8_t verify_raw[OUTPUT_STATE_NVM_STORED_BYTES];
    output_state_nvm_t verify_state;
    uint16_t verify_seq = 0u;

    if (!state) return -1;

    /* NOTE: 이 함수의 모든 호출자(SetSubCoilTarget, NotifyMainRelay 등)는
     * g_state를 먼저 수정한 뒤 &g_state를 전달하므로,
     * memcmp(&g_state, &g_state) 는 항상 0 → 조기 반환으로 EEPROM에 절대 쓰이지 않음.
     * 호출자 측에서 "변경 없으면 Save 미호출" 을 이미 보장하므로 이 체크는 삭제. */

    next_block = (uint8_t)(1u - g_active_block);
    next_seq = (uint16_t)(g_seq + 1u);
    base = nvm_base(next_block);
    nvm_pack(raw, next_seq, state);

    if (EEPROM_Write(base, raw, OUTPUT_STATE_NVM_STORED_BYTES) != 0) {
        g_state = *state;
        g_eeprom_dirty = 1u;
        g_last_save_result = 1u;
        (void)NVM_DEBUG_PRINTF("[NVM] save fail (write) hpsb=%u,%u,%u\r\n",
                     (unsigned)state->hpsb_relay[0], (unsigned)state->hpsb_relay[1],
                     (unsigned)state->hpsb_relay[2]);
        return -1;
    }

    if (EEPROM_Read(base, verify_raw, OUTPUT_STATE_NVM_STORED_BYTES) != 0) {
        g_state = *state;
        g_eeprom_dirty = 1u;
        g_last_save_result = 1u;
        (void)NVM_DEBUG_PRINTF("[NVM] save fail (verify read)\r\n");
        return -1;
    }
    nvm_unpack(verify_raw, &verify_seq, &verify_state);
    if (!nvm_valid(&verify_state) || verify_seq != next_seq) {
        g_state = *state;
        g_eeprom_dirty = 1u;
        g_last_save_result = 1u;
        (void)NVM_DEBUG_PRINTF("[NVM] save fail (verify crc/seq)\r\n");
        return -1;
    }

    g_state = verify_state;
    g_seq = verify_seq;
    g_active_block = next_block;
    g_loaded = 1u;
    g_eeprom_dirty = 0u;
    g_last_save_result = 0u;
    (void)NVM_DEBUG_PRINTF("[NVM] save ok hpsb=%u,%u,%u blk=%u seq=%u\r\n",
                 (unsigned)g_state.hpsb_relay[0], (unsigned)g_state.hpsb_relay[1],
                 (unsigned)g_state.hpsb_relay[2], (unsigned)g_active_block, (unsigned)g_seq);
    return 0;
}

void OutputStateNvm_SetDefaults(output_state_nvm_t *state)
{
    memset(state, 0, sizeof(*state));
    state->magic = OUTPUT_STATE_NVM_MAGIC;
    state->version = OUTPUT_STATE_NVM_VERSION;
    state->crc = nvm_crc(state);
}

void OutputStateNvm_ApplyMainboardRelays(const output_state_nvm_t *state)
{
    if (state && g_loaded) {
        memcpy(g_state.main_relay, state->main_relay, sizeof(g_state.main_relay));
    }
}

void OutputStateNvm_NotifyMainRelay(uint8_t ch, uint8_t value)
{
    if (!g_loaded || ch >= 4u) return;
    value = value ? 1u : 0u;
    if (g_state.main_relay[ch] != value) {
        g_state.main_relay[ch] = value;
        (void)OutputStateNvm_Save(&g_state);
    }
}

int OutputStateNvm_SetSubCoilTarget(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    int rc = -1;
    uint8_t *dst = nvm_select_sub_target(slave_id);
    uint8_t norm = value ? 1u : 0u;

    if (g_loaded && dst != NULL && coil_index <= 2u) {
        if (dst[coil_index] == norm) {
            rc = 0;
        } else {
            dst[coil_index] = norm;
            rc = OutputStateNvm_Save(&g_state);
        }
    }

    return rc;
}

void OutputStateNvm_RestoreSubBoardsIfNeeded(void)
{
    static const uint8_t sid_map[4] = {1u, 2u, 4u, 8u};
    static uint32_t s_retry_poll_tick;
    uint8_t i;

    if (!g_loaded) return;

    for (i = 0u; i < 4u; i++) {
        uint8_t sid = sid_map[i];
        uint8_t *want;
        uint16_t coil;

        /* comm_ok 0→1 전환 감지: 슬레이브가 재연결/재부팅되면 코일 이미지를 초기화하고
         * re-restore를 허용한다. 코일 이미지를 0으로 리셋해야 GetCoil 기반 비교에서
         * 항상 want와 mismatch가 발생해 FC05를 재전송할 수 있다.
         * (s_prev_comm_ok 업데이트는 항상 수행 — skip 전에 해야 함) */
        {
            uint8_t curr_ok = ModbusMaster_IsCommOk((SlaveId_t)sid);
            if (s_prev_comm_ok[i] == 0u && curr_ok != 0u) {
                /* 재연결 전환: 코일 이미지 초기화 + 복원 재허용 */
                uint16_t c;
                for (c = 0u; c < 3u; c++) {
                    ModbusTable_SetCoil((SlaveId_t)sid, c, 0u);
                }
                g_restored_once[i] = 0u;
                (void)NVM_DEBUG_PRINTF("[RESTORE] comm restored sid=%u -> re-restore\r\n", (unsigned)sid);
            }
            s_prev_comm_ok[i] = curr_ok;
        }

        /* 이미 복원 완료 → 재시도 없음 (HPSB InputReg는 전류 피드백 기반이므로
         * mismatch 판정으로 재전송하면 HPSB가 toggle-OFF 동작을 일으킴) */
        if (g_restored_once[i]) {
            continue;
        }
        if (!ModbusMaster_IsCommOk((SlaveId_t)sid)) {
            /* comm 없으면 재폴 요청 (1초 throttle) */
            uint32_t now = HAL_GetTick();
            if (s_retry_poll_tick == 0u) s_retry_poll_tick = now;
            if ((uint32_t)(now - s_retry_poll_tick) >= 1000u) {
                s_retry_poll_tick = now;
                ModbusMaster_RequestOnDemandPoll((uint16_t)sid);
                (void)NVM_DEBUG_PRINTF("[RESTORE] comm not ok sid=%u -> re-poll\r\n", (unsigned)sid);
            }
            continue;
        }

        want = nvm_select_sub_target(sid);
        if (want == NULL) continue;

        (void)NVM_DEBUG_PRINTF("[RESTORE] start sid=%u want=%u,%u,%u\r\n",
                     (unsigned)sid, (unsigned)want[0], (unsigned)want[1], (unsigned)want[2]);

        for (coil = 0u; coil < 3u; coil++) {
            /* GetCoil = 마지막으로 FC05 명령한 상태 (전류 피드백이 아닌 명령 상태 기준 비교).
             * GetInputReg는 HPSB 전류 피드백 기반이라 부하 없으면 항상 0 → 오판 발생. */
            uint8_t cur = ModbusTable_GetCoil((SlaveId_t)sid, coil) ? 1u : 0u;
            if (cur != want[coil]) {
                (void)NVM_DEBUG_PRINTF("[RESTORE] compare sid=%u coil=%u cur=%u want=%u -> write\r\n",
                             (unsigned)sid, (unsigned)coil, (unsigned)cur, (unsigned)want[coil]);
                Gateway_WriteSubCoil_SetNextReason("RESTORE");
                (void)NVM_DEBUG_PRINTF("[RESTORE] write sid=%u coil=%u val=%u\r\n",
                             (unsigned)sid, (unsigned)coil, (unsigned)want[coil]);
                (void)Gateway_Action_WriteSubCoil(sid, coil, want[coil]);
            } else {
                (void)NVM_DEBUG_PRINTF("[RESTORE] compare sid=%u coil=%u cur=%u want=%u -> skip\r\n",
                             (unsigned)sid, (unsigned)coil, (unsigned)cur, (unsigned)want[coil]);
            }
        }

        g_restored_once[i] = 1u;
        g_restore_done_mask |= (uint16_t)(1u << i);
        (void)NVM_DEBUG_PRINTF("[RESTORE] done sid=%u\r\n", (unsigned)sid);
    }
}

void OutputStateNvm_UpdateRestoreOkFromFeedback(void)
{
    static const uint8_t sid_map[4] = {1u, 2u, 4u, 8u};
    uint8_t i;

    if (!g_loaded) return;

    for (i = 0u; i < 4u; i++) {
        uint8_t sid = sid_map[i];
        uint8_t *want;
        uint16_t coil;
        uint8_t all_match;

        if ((g_restore_done_mask & (uint16_t)(1u << i)) == 0u) continue;
        if (!ModbusMaster_IsCommOk((SlaveId_t)sid)) continue;

        want = nvm_select_sub_target(sid);
        if (want == NULL) continue;

        all_match = 1u;
        for (coil = 0u; coil < 3u; coil++) {
            uint8_t fb = ModbusTable_GetInputReg((SlaveId_t)sid, (uint16_t)(2u + coil)) ? 1u : 0u;
            if (fb != want[coil]) {
                all_match = 0u;
                break;
            }
        }
        if (all_match != 0u) {
            g_restore_ok_mask |= (uint16_t)(1u << i);
        } else {
            g_restore_ok_mask &= (uint16_t)~(1u << i);
        }
    }
}

uint8_t OutputStateNvm_SyncTargetActual(void)
{
    static const uint8_t sid_map[4] = {1u, 2u, 4u, 8u};
    uint16_t mismatch = 0u;
    uint16_t bit = 0u;
    uint8_t i;

    if (!g_loaded) return 0u;

    g_sync_fault_bits = 0u;

    for (i = 0u; i < 4u; i++) {
        uint8_t sid = sid_map[i];
        uint8_t *want = nvm_select_sub_target(sid);
        uint16_t coil;
        if (want == NULL) continue;

        for (coil = 0u; coil < 3u; coil++, bit++) {
            uint8_t cur = ModbusTable_GetCoil((SlaveId_t)sid, coil) ? 1u : 0u;
            if (cur != want[coil]) {
                mismatch++;
                g_sync_fault_bits |= (uint16_t)(1u << bit);
            }
        }
    }

    return (uint8_t)mismatch;
}

uint16_t OutputStateNvm_GetSyncFaultBits(void)
{
    return g_sync_fault_bits;
}

const output_state_nvm_t *OutputStateNvm_Get(void)
{
    return g_loaded ? &g_state : NULL;
}

int OutputStateNvm_IsEepromDirty(void)
{
    return g_eeprom_dirty ? 1 : 0;
}

uint16_t OutputStateNvm_GetSequence(void)
{
    return g_loaded ? g_seq : 0u;
}

uint16_t OutputStateNvm_GetLastSaveResult(void)
{
    return g_last_save_result;
}

uint16_t OutputStateNvm_GetLastLoadResult(void)
{
    return g_last_load_result;
}

uint16_t OutputStateNvm_GetRestoreDoneMask(void)
{
    return g_restore_done_mask;
}

uint16_t OutputStateNvm_GetRestoreOkMask(void)
{
    return g_restore_ok_mask;
}

void OutputStateNvm_FlushIfDirty(void)
{
    static uint32_t s_flush_tick;
    uint32_t now;

    if (!g_loaded || !g_eeprom_dirty) return;

    now = HAL_GetTick();
    if (s_flush_tick == 0u) s_flush_tick = now;
    if ((uint32_t)(now - s_flush_tick) < 5000u) return;
    s_flush_tick = now;

    (void)NVM_DEBUG_PRINTF("[NVM] dirty retry...\r\n");
    if (OutputStateNvm_Save(&g_state) == 0) {
        g_eeprom_dirty = 0u;
        (void)NVM_DEBUG_PRINTF("[NVM] dirty retry ok\r\n");
    } else {
        (void)NVM_DEBUG_PRINTF("[NVM] dirty retry fail\r\n");
    }
}

void OutputStateNvm_KeepAliveIfNeeded(void)
{
    /* keep-alive 주기: 1700ms.
     * 모니터링 tick(2000ms)과 다른 주기로 어긋나게 동작하여 동시 폴 충돌 방지.
     * 최근 1500ms 이내에 해당 slave의 성공 poll이 이미 있었으면 skip하여
     * UART2 부하를 최소화한다. */
    static const uint8_t sid_map[4] = {1u, 2u, 4u, 8u};
    static uint32_t s_keepalive_tick;
    uint32_t now;
    uint8_t i;

    if (!g_loaded) return;

    now = HAL_GetTick();
    if (s_keepalive_tick == 0u) s_keepalive_tick = now;
    if ((uint32_t)(now - s_keepalive_tick) < 1700u) return;
    s_keepalive_tick = now;

    for (i = 0u; i < 4u; i++) {
        uint8_t sid = sid_map[i];
        uint8_t *want = nvm_select_sub_target(sid);
        uint16_t coil;
        uint8_t any_on = 0u;
        uint32_t last_poll;

        if (want == NULL) continue;
        for (coil = 0u; coil < 3u; coil++) {
            if (want[coil]) { any_on = 1u; break; }
        }
        if (!any_on) continue;

        /* 최근 1500ms 내 성공 poll이 있었으면 HPSB watchdog 이미 갱신됨 → skip */
        last_poll = ModbusMaster_GetLastPollTick((SlaveId_t)sid);
        if (last_poll != 0u && (uint32_t)(now - last_poll) < 1500u) continue;

        ModbusMaster_RequestOnDemandPoll((uint16_t)sid);
    }
}
