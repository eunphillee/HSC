/**
 * @file output_state_nvm.c
 * @brief EEPROM 출력 상태 저장·복원 - 현재 기능 보류(stub).
 *        모든 함수가 no-op 또는 안전한 기본값을 반환.
 *        추후 재활성화 시 이 파일을 교체하세요.
 */
#include "output_state_nvm.h"
#include <string.h>

int OutputStateNvm_Load(output_state_nvm_t *state)
{
    if (state) OutputStateNvm_SetDefaults(state);
    return 0;
}

int OutputStateNvm_Save(const output_state_nvm_t *state)
{
    (void)state;
    return 0;
}

void OutputStateNvm_SetDefaults(output_state_nvm_t *state)
{
    if (!state) return;
    (void)memset(state, 0, sizeof(*state));
    state->magic   = OUTPUT_STATE_NVM_MAGIC;
    state->version = OUTPUT_STATE_NVM_VERSION;
}

void OutputStateNvm_ApplyMainboardRelays(const output_state_nvm_t *state)
{
    (void)state;
}

void OutputStateNvm_NotifyMainRelay(uint8_t ch, uint8_t value)
{
    (void)ch;
    (void)value;
}

int OutputStateNvm_SetSubCoilTarget(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    (void)slave_id;
    (void)coil_index;
    (void)value;
    return 0;
}

void OutputStateNvm_RestoreSubBoardsIfNeeded(void)
{
}

uint8_t OutputStateNvm_SyncTargetActual(void)
{
    return 0u;
}

uint16_t OutputStateNvm_GetSyncFaultBits(void)
{
    return 0u;
}

const output_state_nvm_t *OutputStateNvm_Get(void)
{
    return NULL;
}

int OutputStateNvm_IsEepromDirty(void)
{
    return 0;
}

uint16_t OutputStateNvm_GetSequence(void)
{
    return 0u;
}
