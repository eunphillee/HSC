#include "aggregator.h"
#include "io_map.h"
#include "modbus_table.h"
#include "modbus_master.h"
#include "main.h"
#include "shtc3.h"
#include "h2tech_address_map.h"
#include "gateway_actions.h"

/* Sub-board input reg 7..9 = CURRENT 0/1; debounce before H2 ALM bits */
#define SUB_CURRENT_DEBOUNCE_CYCLES  3u

static void debounce_current_3ch(SlaveId_t sid, uint8_t cnt[3], uint8_t *any_tripped)
{
	*any_tripped = 0;
	for (int i = 0; i < 3; i++) {
		uint8_t on = ModbusTable_GetInputReg(sid, (uint16_t)(7 + i)) ? 1u : 0u;
		if (on) {
			if (cnt[i] < 255u) cnt[i]++;
		} else {
			cnt[i] = 0;
		}
		if (cnt[i] >= SUB_CURRENT_DEBOUNCE_CYCLES)
			*any_tripped = 1u;
	}
}

void Aggregator_Update(aggregated_status_t *out)
{
	if (!out) return;

	out->timestamp_ms = HAL_GetTick();
	out->error_flags = 0;

	/* Env sensor (SHTC3 on I2C1): update at most 1 Hz */
	static uint32_t last_env_tick;
	if ((out->timestamp_ms - last_env_tick) >= 1000u) {
		last_env_tick = out->timestamp_ms;
		extern I2C_HandleTypeDef hi2c1; /* I2C1 = SHTC3 */
		float t = 0.0f, rh = 0.0f;
		if (SHTC3_Measure(&hi2c1, &t, &rh) == 0) {
			/* store x10 */
			out->env_temp_cx10 = (int16_t)(t * 10.0f);
			out->env_rh_x10    = (uint16_t)(rh * 10.0f);
			out->error_flags &= (uint16_t)~AGG_ERR_SHTC3;
		} else {
			out->env_temp_cx10 = -32768;
			out->env_rh_x10    = 0xFFFF;
			out->error_flags |= AGG_ERR_SHTC3;
		}
	}

	out->main_di = 0;
	for (int i = 0; i < MAIN_DI_COUNT; i++)
		out->main_di |= (IO_Main_ReadDI((MainDiChannel_t)i) ? (1u << i) : 0);
	out->main_do = 0;
	for (int i = 0; i < 2; i++)
		out->main_do |= (IO_Main_ReadDO((MainDoChannel_t)i) ? (1u << i) : 0);

	out->hpsb_coils      = 0;
	out->hpsb_discrete   = 0;
	out->hpsb_status_reg = 0;
	out->hpsb_alarm_reg  = 0;
	out->hpsb_sense_raw[0] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 1);
	out->hpsb_sense_raw[1] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 2);
	out->hpsb_sense_raw[2] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 3);
	out->hpsb_pkpk[0] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 4);
	out->hpsb_pkpk[1] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 5);
	out->hpsb_pkpk[2] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 6);
	out->hpsb_current_st[0] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 7) ? 1u : 0u;
	out->hpsb_current_st[1] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 8) ? 1u : 0u;
	out->hpsb_current_st[2] = ModbusTable_GetInputReg(SLAVE_ID_HPSB, 9) ? 1u : 0u;
	for (int i = 0; i < MODBUS_COIL_COUNT; i++)
		out->hpsb_coils |= (ModbusTable_GetCoil(SLAVE_ID_HPSB, (uint16_t)i) ? (1u << i) : 0);
	for (int i = 0; i < MODBUS_DISCRETE_COUNT; i++)
		out->hpsb_discrete |= (ModbusTable_GetDiscrete(SLAVE_ID_HPSB, (uint16_t)i) ? (1u << i) : 0);
	out->hpsb_status_reg = ModbusTable_GetHoldingReg(SLAVE_ID_HPSB, HOLDING_REG_STATUS);
	/* hpsb_alarm_reg filled after CURRENT debounce (below) */

	/* Separate LPSB aggregator image: coils[3] and alarm/sense per slave */
	out->lpsb1_coils[0] = ModbusTable_GetCoil(SLAVE_ID_LPSB1, 0);
	out->lpsb1_coils[1] = ModbusTable_GetCoil(SLAVE_ID_LPSB1, 1);
	out->lpsb1_coils[2] = ModbusTable_GetCoil(SLAVE_ID_LPSB1, 2);
	out->lpsb2_coils[0] = ModbusTable_GetCoil(SLAVE_ID_LPSB2, 0);
	out->lpsb2_coils[1] = ModbusTable_GetCoil(SLAVE_ID_LPSB2, 1);
	out->lpsb2_coils[2] = ModbusTable_GetCoil(SLAVE_ID_LPSB2, 2);
	out->lpsb3_coils[0] = ModbusTable_GetCoil(SLAVE_ID_LPSB3, 0);
	out->lpsb3_coils[1] = ModbusTable_GetCoil(SLAVE_ID_LPSB3, 1);
	out->lpsb3_coils[2] = ModbusTable_GetCoil(SLAVE_ID_LPSB3, 2);
	out->lpsb1_alarm_reg  = ModbusTable_GetHoldingReg(SLAVE_ID_LPSB1, HOLDING_REG_ALARM);
	out->lpsb2_alarm_reg  = ModbusTable_GetHoldingReg(SLAVE_ID_LPSB2, HOLDING_REG_ALARM);
	out->lpsb3_alarm_reg  = ModbusTable_GetHoldingReg(SLAVE_ID_LPSB3, HOLDING_REG_ALARM);
	out->lpsb1_sense_raw[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 1);
	out->lpsb1_sense_raw[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 2);
	out->lpsb1_sense_raw[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 3);
	out->lpsb1_pkpk[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 4);
	out->lpsb1_pkpk[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 5);
	out->lpsb1_pkpk[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 6);
	out->lpsb1_current_st[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 7) ? 1u : 0u;
	out->lpsb1_current_st[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 8) ? 1u : 0u;
	out->lpsb1_current_st[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 9) ? 1u : 0u;
	out->lpsb2_sense_raw[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 1);
	out->lpsb2_sense_raw[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 2);
	out->lpsb2_sense_raw[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 3);
	out->lpsb2_pkpk[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 4);
	out->lpsb2_pkpk[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 5);
	out->lpsb2_pkpk[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 6);
	out->lpsb2_current_st[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 7) ? 1u : 0u;
	out->lpsb2_current_st[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 8) ? 1u : 0u;
	out->lpsb2_current_st[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 9) ? 1u : 0u;
	out->lpsb3_sense_raw[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 1);
	out->lpsb3_sense_raw[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 2);
	out->lpsb3_sense_raw[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 3);
	out->lpsb3_pkpk[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 4);
	out->lpsb3_pkpk[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 5);
	out->lpsb3_pkpk[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 6);
	out->lpsb3_current_st[0] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 7) ? 1u : 0u;
	out->lpsb3_current_st[1] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 8) ? 1u : 0u;
	out->lpsb3_current_st[2] = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 9) ? 1u : 0u;

	if (!ModbusMaster_IsCommOk(SLAVE_ID_HPSB)) out->error_flags |= AGG_ERR_COMM_HPSB;
	if (!ModbusMaster_IsCommOk(SLAVE_ID_LPSB1) || !ModbusMaster_IsCommOk(SLAVE_ID_LPSB2) || !ModbusMaster_IsCommOk(SLAVE_ID_LPSB3))
		out->error_flags |= AGG_ERR_COMM_LPSB;
	if (Gateway_Action_PollDownstreamWriteFail()) out->error_flags |= AGG_ERR_DOWNSTREAM_WRITE;

	/* HPSB: merge slave holding alarm with debounced CURRENT (input reg 7..9) → bits 0..2 */
	static uint8_t hpsb_cur_db[3];
	{
		uint8_t trip_ch[3] = {0, 0, 0};
		for (int i = 0; i < 3; i++) {
			uint8_t on = out->hpsb_current_st[i] ? 1u : 0u;
			if (on) {
				if (hpsb_cur_db[i] < 255u) hpsb_cur_db[i]++;
			} else {
				hpsb_cur_db[i] = 0;
			}
			if (hpsb_cur_db[i] >= SUB_CURRENT_DEBOUNCE_CYCLES)
				trip_ch[i] = 1u;
		}
		uint16_t hpsb_hold = ModbusTable_GetHoldingReg(SLAVE_ID_HPSB, HOLDING_REG_ALARM);
		out->hpsb_alarm_reg = hpsb_hold;
		if (trip_ch[0]) out->hpsb_alarm_reg |= (1u << 0);
		if (trip_ch[1]) out->hpsb_alarm_reg |= (1u << 1);
		if (trip_ch[2]) out->hpsb_alarm_reg |= (1u << 2);
	}

	/* Fill H2TECH aggregated bit image for upstream FC02
	 * 0821~0822 MAIN DO; 0823~0825 HPSB coils 0~2; 0826~0828 LPSB1, 0829~0831 LPSB2, 0832~0834 LPSB3; 0835~0836 reserved. */
	H2Map_WriteAggBit(AGG_BIT_ONOFF_1, (out->main_do & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_2, (out->main_do & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_3, (out->hpsb_coils & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_4, (out->hpsb_coils & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_5, (out->hpsb_coils & (1u << 2)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_6, out->lpsb1_coils[0] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_7, out->lpsb1_coils[1] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_8, out->lpsb1_coils[2] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_9, out->lpsb2_coils[0] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_10, out->lpsb2_coils[1] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_11, out->lpsb2_coils[2] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_12, out->lpsb3_coils[0] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_13, out->lpsb3_coils[1] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_14, out->lpsb3_coils[2] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_15, false);
	H2Map_WriteAggBit(AGG_BIT_ONOFF_16, false);

	/* Door sensors: MAIN DI 0,1 = magnetic 1,2; 2,3 = button 1,2 */
	H2Map_WriteAggBit(AGG_BIT_DOOR_MAG_1, (out->main_di & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_DOOR_MAG_2, (out->main_di & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_DOOR_BTN_1, (out->main_di & (1u << 2)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_DOOR_BTN_2, (out->main_di & (1u << 3)) ? true : false);

	/* Alarms: 1=HPSB comm, 2=any LPSB comm, 3=SHTC3, 4=door fault, 5-7=HPSB OC, 8-10=LPSB1/2/3 OC, 11=PC link, 12=reserved */
	H2Map_WriteAggBit(AGG_BIT_ALM_1, (out->error_flags & AGG_ERR_COMM_HPSB) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_2, (out->error_flags & AGG_ERR_COMM_LPSB) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_3, (out->error_flags & AGG_ERR_SHTC3) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_4, false);
	H2Map_WriteAggBit(AGG_BIT_ALM_5, (out->hpsb_alarm_reg & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_6, (out->hpsb_alarm_reg & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_7, (out->hpsb_alarm_reg & (1u << 2)) ? true : false);
	/* LPSB overcurrent: any of 3 ports over threshold (mid-scale ± margin) sets ALM8/9/10 */
	{
		uint8_t trip1 = 0, trip2 = 0, trip3 = 0;
		static uint8_t lpsb1_cur_db[3];
		static uint8_t lpsb2_cur_db[3];
		static uint8_t lpsb3_cur_db[3];
		debounce_current_3ch(SLAVE_ID_LPSB1, lpsb1_cur_db, &trip1);
		debounce_current_3ch(SLAVE_ID_LPSB2, lpsb2_cur_db, &trip2);
		debounce_current_3ch(SLAVE_ID_LPSB3, lpsb3_cur_db, &trip3);
		H2Map_WriteAggBit(AGG_BIT_ALM_8, trip1 ? true : false);
		H2Map_WriteAggBit(AGG_BIT_ALM_9, trip2 ? true : false);
		H2Map_WriteAggBit(AGG_BIT_ALM_10, trip3 ? true : false);
	}
	H2Map_WriteAggBit(AGG_BIT_ALM_11, (out->error_flags & AGG_ERR_UPSTREAM_RX) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_12, (out->error_flags & AGG_ERR_DOWNSTREAM_WRITE) ? true : false);

	/* CMD ON/OFF 1~7: mirror of output state for consistency (optional) */
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_1, (out->main_do & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_2, (out->main_do & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_3, (out->hpsb_coils & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_4, (out->hpsb_coils & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_5, (out->hpsb_coils & (1u << 2)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_6, out->lpsb1_coils[0] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_7, out->lpsb1_coils[1] ? true : false);
}
