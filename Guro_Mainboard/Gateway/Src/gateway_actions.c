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

#define PULSE_MS_DOOR  300u
#define PULSE_MS_PC_IO 500u

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
    uint8_t slave_id;
    uint16_t coil_index;
    uint8_t value;
    uint8_t retries_left;
    uint32_t next_try_tick;
} PendingSubCoilWrite_t;

static volatile PendingSubCoilWrite_t s_pending_subcoil_write;

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

    /* Async sub-board coil write (non-blocking FC05 response path) */
    if (s_pending_subcoil_write.valid) {
        if ((int32_t)(now - s_pending_subcoil_write.next_try_tick) >= 0) {
            uint8_t sid = s_pending_subcoil_write.slave_id;
            uint16_t coil = s_pending_subcoil_write.coil_index;
            uint8_t val = s_pending_subcoil_write.value;
            int ret = Gateway_Action_WriteSubCoil(sid, coil, val);
            if (ret == 0) {
                s_pending_subcoil_write.valid = 0u;
            } else {
                if (s_pending_subcoil_write.retries_left > 0u)
                    s_pending_subcoil_write.retries_left--;
                if (s_pending_subcoil_write.retries_left == 0u) {
                    s_pending_subcoil_write.valid = 0u;
                    /* sticky fail flag is set inside Gateway_Action_WriteSubCoil() */
                } else {
                    /* small backoff to avoid bus collision / allow subboard settle */
                    s_pending_subcoil_write.next_try_tick = now + 30u;
                }
            }
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
    if (slave_id != 1 && slave_id != 2 && slave_id != 4 && slave_id != 8) return -1;
    if (coil_index >= MODBUS_COIL_COUNT) return -1;
    Gateway_LogWriteMapped(slave_id, coil_index, value);
    Gateway_LogUart2TxStart(slave_id, coil_index, value ? 1 : 0);
    int ret = ModbusMaster_WriteCoil(s, coil_index, value ? 1 : 0);
    err = ModbusMaster_GetLastFc05Error();
    if (ret == 0) {
        /* ACK received: definite success */
        ModbusTable_SetCoil(s, coil_index, value ? 1 : 0);
        return 0;
    }
    if (err == MODBUS_MASTER_FC05_ERR_TIMEOUT || err == MODBUS_MASTER_FC05_ERR_INVALID_RESP) {
        /* LPSB may not always echo FC05 but physically executes the command.
         * Optimistic update: PC-side FC04 read-back will confirm actual state. */
        ModbusTable_SetCoil(s, coil_index, value ? 1 : 0);
        return 0;
    }
    /* EXCEPTION (0x04 etc.): subboard explicitly rejected – report failure */
    s_downstream_write_fail = 1;
    return -1;
}

int Gateway_Action_RequestSubCoilWrite(uint8_t slave_id, uint16_t coil_index, uint8_t value)
{
    /* Single-slot queue but overwrite allowed: 최신 목표 상태를 우선 적용 */
    if (slave_id != 1 && slave_id != 2 && slave_id != 4 && slave_id != 8) return -1;
    if (coil_index >= MODBUS_COIL_COUNT) return -1;

    s_pending_subcoil_write.slave_id = slave_id;
    s_pending_subcoil_write.coil_index = coil_index;
    s_pending_subcoil_write.value = value ? 1u : 0u;
    s_pending_subcoil_write.retries_left = 5u;
    s_pending_subcoil_write.next_try_tick = HAL_GetTick();
    s_pending_subcoil_write.valid = 1u;
    return 0;
}
