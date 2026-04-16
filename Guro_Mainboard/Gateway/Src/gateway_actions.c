/**
 * @file gateway_actions.c
 * @brief Implements Gateway_Action_PulseMainDoor1/2 (non-blocking 300ms) and
 *        Gateway_Action_PulseOutputByOnOffIndex(8..12) as TOGGLE on LPSB coils.
 */
#include "gateway_actions.h"
#include "h2tech_address_map.h"
#include "gateway_write_log.h"
#include "io_map.h"
#include "modbus_table.h"
#include "modbus_master.h"
#include "main.h"
#include "bsp_gpio.h"
#include "app_config.h"
#include <stdio.h>
#define PULSE_MS_DOOR  300u
#define PULSE_MS_PC_IO 500u
#define SUBCOIL_PENDING_RETRY_COUNT      5u
#define SUBCOIL_PENDING_RETRY_BACKOFF_MS 30u
#define SUBCOIL_REQUEST_COALESCE_MS      40u
#define SUBCOIL_MIN_TX_GAP_MS            80u

static uint8_t door1_active;
static uint32_t door1_tick;
static uint8_t door2_active;
static uint32_t door2_tick;
static uint8_t pc_on_en_pulse_active;
static uint32_t pc_on_en_pulse_tick;
static uint8_t pc_reset_en_pulse_active;
static uint32_t pc_reset_en_pulse_tick;
/* Set when downstream WriteCoil fails; sticky until cleared. Cleared by ClearDownstreamWriteFailAlarm (e.g. on PC read of 1x0880 or auto after N s). */
static volatile uint8_t s_downstream_write_fail;

typedef struct {
    uint8_t valid;
    uint8_t value;
    uint8_t retries_left;
    uint32_t next_try_tick;
} PendingSubCoilWrite_t;

static volatile PendingSubCoilWrite_t s_pending_subcoil_write[4][MODBUS_COIL_COUNT];
static uint32_t s_subcoil_last_issue_tick[4][MODBUS_COIL_COUNT];
static uint8_t s_pending_subcoil_rr_index;

static const char *s_write_sub_reason = "UNKNOWN";

static int subcoil_slave_to_slot(uint8_t slave_id)
{
    switch (slave_id) {
    case 1u: return 0;
    case 2u: return 1;
    case 4u: return 2;
    case 8u: return 3;
    default: return -1;
    }
}

static uint8_t subcoil_slot_to_slave(uint8_t slot)
{
    static const uint8_t s_slot_to_slave[] = { 1u, 2u, 4u, 8u };
    if (slot >= (uint8_t)(sizeof(s_slot_to_slave) / sizeof(s_slot_to_slave[0])))
        return 0u;
    return s_slot_to_slave[slot];
}

void Gateway_WriteSubCoil_SetNextReason(const char *reason)
{
    s_write_sub_reason = (reason && reason[0] != '\0') ? reason : "UNKNOWN";
}

void Gateway_Action_PulseMainDoor1(uint16_t pulse_ms)
{
    (void)pulse_ms;
    if (door1_active) return;
    IO_Main_WriteDO(MAIN_DO_RELAY1, 1);
    door1_tick = HAL_GetTick();
    door1_active = 1;
}

void Gateway_Action_PulseMainDoor2(uint16_t pulse_ms)
{
    (void)pulse_ms;
    if (door2_active) return;
    IO_Main_WriteDO(MAIN_DO_RELAY2, 1);
    door2_tick = HAL_GetTick();
    door2_active = 1;
}

void Gateway_Action_PulseOutputByOnOffIndex(uint8_t onoff_index_1based, uint16_t pulse_ms)
{
    (void)pulse_ms;
    /* Explicit mapping: ON/OFF 8 -> Slave 2 coil 2; 9 -> Slave 3 coil 0; ... 12 -> Slave 4 coil 0 */
    SlaveId_t slave_id;
    uint16_t coil_index;
    switch (onoff_index_1based) {
    case 8:  slave_id = SLAVE_ID_LPSB1; coil_index = 2; break;
    case 9:  slave_id = SLAVE_ID_LPSB2; coil_index = 0; break;
    case 10: slave_id = SLAVE_ID_LPSB2; coil_index = 1; break;
    case 11: slave_id = SLAVE_ID_LPSB2; coil_index = 2; break;
    case 12: slave_id = SLAVE_ID_LPSB3; coil_index = 0; break;
    default: return;
    }
    uint8_t cur = ModbusTable_GetCoil(slave_id, coil_index);
    uint8_t next = cur ? 0 : 1;
    /* Write downstream first; update local image only on success */
    ModbusMaster_SetFc05TxReason("INTERNAL");
    int ret = ModbusMaster_WriteCoil(slave_id, coil_index, next);
    if (ret == 0)
        ModbusTable_SetCoil(slave_id, coil_index, next);
    else
        s_downstream_write_fail = 1;
}

void Gateway_Action_StartPulsePC_ON_EN(void)
{
    if (pc_on_en_pulse_active) return;
    BSP_WritePC_ON_EN(1);
    pc_on_en_pulse_tick = HAL_GetTick();
    pc_on_en_pulse_active = 1;
}

void Gateway_Action_StartPulsePC_RESET_EN(void)
{
    if (pc_reset_en_pulse_active) return;
    BSP_WritePC_RESET_EN(1);
    pc_reset_en_pulse_tick = HAL_GetTick();
    pc_reset_en_pulse_active = 1;
}

void Gateway_Action_Update(void)
{
    uint32_t now = HAL_GetTick();

    if (door1_active && (now - door1_tick >= PULSE_MS_DOOR)) {
        IO_Main_WriteDO(MAIN_DO_RELAY1, 0);
        door1_active = 0;
    }
    if (door2_active && (now - door2_tick >= PULSE_MS_DOOR)) {
        IO_Main_WriteDO(MAIN_DO_RELAY2, 0);
        door2_active = 0;
    }
    if (pc_on_en_pulse_active && (now - pc_on_en_pulse_tick >= PULSE_MS_PC_IO)) {
        BSP_WritePC_ON_EN(0);
        pc_on_en_pulse_active = 0;
    }
    if (pc_reset_en_pulse_active && (now - pc_reset_en_pulse_tick >= PULSE_MS_PC_IO)) {
        BSP_WritePC_RESET_EN(0);
        pc_reset_en_pulse_active = 0;
    }

    /* Async sub-board coil write:
     * keep the latest target per slave/coil and send only one when UART2 is idle.
     * This prevents FC05 button mashing from turning into a UART2 write storm. */
    if (!ModbusMaster_IsBusy()) {
        uint8_t total_slots = (uint8_t)(4u * MODBUS_COIL_COUNT);
        for (uint8_t tries = 0u; tries < total_slots; ++tries) {
            uint8_t flat_index = (uint8_t)((s_pending_subcoil_rr_index + tries) % total_slots);
            uint8_t sid_slot = (uint8_t)(flat_index / MODBUS_COIL_COUNT);
            uint8_t coil = (uint8_t)(flat_index % MODBUS_COIL_COUNT);
            volatile PendingSubCoilWrite_t *pending = &s_pending_subcoil_write[sid_slot][coil];
            uint8_t sid;
            int ret;

            if (!pending->valid) continue;
            if ((int32_t)(now - pending->next_try_tick) < 0) continue;

            sid = subcoil_slot_to_slave(sid_slot);
            if (sid == 0u) {
                pending->valid = 0u;
                continue;
            }

            Gateway_WriteSubCoil_SetNextReason("PENDING");
            s_subcoil_last_issue_tick[sid_slot][coil] = now;
            ret = Gateway_Action_WriteSubCoil(sid, coil, pending->value);
            if (ret == 0) {
                pending->valid = 0u;
            } else {
                if (pending->retries_left > 0u)
                    pending->retries_left--;
                if (pending->retries_left == 0u) {
                    pending->valid = 0u;
                    /* sticky fail flag is set inside Gateway_Action_WriteSubCoil() */
                } else {
                    /* small backoff to avoid bus collision / allow subboard settle */
                    pending->next_try_tick = now + SUBCOIL_PENDING_RETRY_BACKOFF_MS;
                }
            }
            s_pending_subcoil_rr_index = (uint8_t)((flat_index + 1u) % total_slots);
            break;
        }
    }
}

uint8_t Gateway_Action_PollDownstreamWriteFail(void)
{
    return (uint8_t)s_downstream_write_fail;
}

void Gateway_Action_ClearDownstreamWriteFailAlarm(void)
{
    s_downstream_write_fail = 0;
}

/* PC→Mainboard FC05 to coil 898..909: forward to HPSB/LPSB (Slave 1,2,4,8). Returns 0 on success. */
int Gateway_Action_WriteSubCoil(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    SlaveId_t s = (SlaveId_t)slave_id;
    ModbusMasterFc05Err_t err;
    const char *reason;
    if (slave_id != 1 && slave_id != 2 && slave_id != 4 && slave_id != 8) return -1;
    if (coil_index >= MODBUS_COIL_COUNT) return -1;

    reason = s_write_sub_reason;
    s_write_sub_reason = "UNKNOWN";

#if GATEWAY_WRITE_DEBUG_LOG
    {
        char wb[120];
        int wn = snprintf(wb, sizeof(wb), "[WRITE_SUB] reason=%s sid=%u coil=%u val=%u\r\n",
                          reason, (unsigned)slave_id, (unsigned)coil_index,
                          (unsigned)(value ? 1u : 0u));
        if (wn > 0) (void)printf("%s", wb);
    }
#endif

    Gateway_LogWriteMapped(slave_id, coil_index, value);
    Gateway_LogUart2TxStart(slave_id, coil_index, value ? 1 : 0);
    ModbusMaster_SetFc05TxReason(reason);
    int ret = ModbusMaster_WriteCoil(s, coil_index, value ? 1 : 0);
    err = ModbusMaster_GetLastFc05Error();

    if (ret == 0) {
        /* ACK: 확정 성공 → coil/InputReg 이미지 갱신 (actual 낙관적 반영) */
        ModbusTable_SetCoil(s, coil_index, value ? 1 : 0);
        ModbusTable_SetInputReg(s, 2u + coil_index, value ? 1u : 0u);
        return 0;
    }
    if (err == MODBUS_MASTER_FC05_ERR_TIMEOUT || err == MODBUS_MASTER_FC05_ERR_INVALID_RESP) {
        /* TIMEOUT/INVALID: LPSB가 무응답이지만 물리 실행 가능성 있음 → 낙관적 갱신.
         * SyncTargetActual가 2초 후 actual을 확인하여 불일치 시 재시도. */
        ModbusTable_SetCoil(s, coil_index, value ? 1 : 0);
        ModbusTable_SetInputReg(s, 2u + coil_index, value ? 1u : 0u);
        return 0;
    }
    /* EXCEPTION: 하위보드가 명시적으로 거부 → actual 이미지 갱신 안 함.
     * target은 이미 저장됨 → SyncTargetActual가 재시도 후 fault 판정. */
    s_downstream_write_fail = 1;
    return -1;
}

int Gateway_Action_RequestSubCoilWrite(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    /* Queue latest target per slave/coil so PC FC05 storms collapse into a small
     * number of actual UART2 writes. */
    int sid_slot = subcoil_slave_to_slot(slave_id);
    volatile PendingSubCoilWrite_t *pending;
    uint32_t now;
    uint32_t ready_tick;
    uint32_t earliest_tick;

    if (sid_slot < 0) return -1;
    if (coil_index >= MODBUS_COIL_COUNT) return -1;

    pending = &s_pending_subcoil_write[sid_slot][coil_index];
    now = HAL_GetTick();
    ready_tick = now + SUBCOIL_REQUEST_COALESCE_MS;
    earliest_tick = s_subcoil_last_issue_tick[sid_slot][coil_index] + SUBCOIL_MIN_TX_GAP_MS;
    if (s_subcoil_last_issue_tick[sid_slot][coil_index] != 0u &&
        (int32_t)(ready_tick - earliest_tick) < 0) {
        ready_tick = earliest_tick;
    }

    if (pending->valid && pending->value == (value ? 1u : 0u)) {
        /* Same target/value is already queued; keep the earlier send time. */
        return 0;
    }

    pending->value = value ? 1u : 0u;
    pending->retries_left = SUBCOIL_PENDING_RETRY_COUNT;
    pending->next_try_tick = ready_tick;
    pending->valid = 1u;
    return 0;
}
