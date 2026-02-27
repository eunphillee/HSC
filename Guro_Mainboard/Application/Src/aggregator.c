#include "aggregator.h"
#include "io_map.h"
#include "modbus_table.h"
#include "modbus_master.h"
#include "main.h"
#include "h2tech_address_map.h"
#include "gateway_actions.h"

/* Overcurrent thresholds (configurable). Raw ADC/register value above this sets alarm. */
#define HPSB_OC_THRESHOLD_RAW  2048u   /* 12-bit ADC: adjust for HCT17W */
#define LPSB_OC_THRESHOLD_RAW  2048u   /* ACS712 via input reg: adjust per hardware */
#define HPSB_OC_CYCLES_REQUIRED 3u    /* Consecutive cycles above threshold before ALM5/6/7 */

void Aggregator_Update(aggregated_status_t *out)
{
	if (!out) return;

	out->timestamp_ms = HAL_GetTick();

	out->env_temp_cx10 = -32768;
	out->env_rh_x10    = 0xFFFF;

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
	out->hpsb_sense_raw  = 0;
	for (int i = 0; i < MODBUS_COIL_COUNT; i++)
		out->hpsb_coils |= (ModbusTable_GetCoil(SLAVE_ID_HPSB, (uint16_t)i) ? (1u << i) : 0);
	for (int i = 0; i < MODBUS_DISCRETE_COUNT; i++)
		out->hpsb_discrete |= (ModbusTable_GetDiscrete(SLAVE_ID_HPSB, (uint16_t)i) ? (1u << i) : 0);
	out->hpsb_status_reg = ModbusTable_GetHoldingReg(SLAVE_ID_HPSB, HOLDING_REG_STATUS);
	out->hpsb_alarm_reg  = ModbusTable_GetHoldingReg(SLAVE_ID_HPSB, HOLDING_REG_ALARM);

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
	out->lpsb1_sense_raw  = ModbusTable_GetInputReg(SLAVE_ID_LPSB1, 1);
	out->lpsb2_sense_raw  = ModbusTable_GetInputReg(SLAVE_ID_LPSB2, 1);
	out->lpsb3_sense_raw  = ModbusTable_GetInputReg(SLAVE_ID_LPSB3, 1);

	out->error_flags = 0;
	if (!ModbusMaster_IsCommOk(SLAVE_ID_HPSB)) out->error_flags |= AGG_ERR_COMM_HPSB;
	if (!ModbusMaster_IsCommOk(SLAVE_ID_LPSB1) || !ModbusMaster_IsCommOk(SLAVE_ID_LPSB2) || !ModbusMaster_IsCommOk(SLAVE_ID_LPSB3))
		out->error_flags |= AGG_ERR_COMM_LPSB;
	if (Gateway_Action_PollDownstreamWriteFail()) out->error_flags |= AGG_ERR_DOWNSTREAM_WRITE;

	/* HPSB overcurrent: ADC1/2/3 (HCT17W). If > threshold for 3 consecutive cycles, set ALM5/6/7. */
	static uint8_t hpsb_oc_count[3];
	uint16_t hpsb_raw[3];
	hpsb_raw[0] = IO_ReadHpsbCurrentRaw(0);
	hpsb_raw[1] = IO_ReadHpsbCurrentRaw(1);
	hpsb_raw[2] = IO_ReadHpsbCurrentRaw(2);
	for (int i = 0; i < 3; i++) {
		if (hpsb_raw[i] > HPSB_OC_THRESHOLD_RAW) {
			if (hpsb_oc_count[i] < 255u) hpsb_oc_count[i]++;
		} else {
			hpsb_oc_count[i] = 0;
		}
	}
	out->hpsb_alarm_reg = 0;
	if (hpsb_oc_count[0] >= HPSB_OC_CYCLES_REQUIRED) out->hpsb_alarm_reg |= (1u << 0);
	if (hpsb_oc_count[1] >= HPSB_OC_CYCLES_REQUIRED) out->hpsb_alarm_reg |= (1u << 1);
	if (hpsb_oc_count[2] >= HPSB_OC_CYCLES_REQUIRED) out->hpsb_alarm_reg |= (1u << 2);

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
	H2Map_WriteAggBit(AGG_BIT_ALM_8, (out->lpsb1_sense_raw > LPSB_OC_THRESHOLD_RAW) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_9, (out->lpsb2_sense_raw > LPSB_OC_THRESHOLD_RAW) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_10, (out->lpsb3_sense_raw > LPSB_OC_THRESHOLD_RAW) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_11, (out->error_flags & AGG_ERR_UPSTREAM_RX) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_ALM_12, false);

	/* CMD ON/OFF 1~7: mirror of output state for consistency (optional) */
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_1, (out->main_do & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_2, (out->main_do & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_3, (out->hpsb_coils & (1u << 0)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_4, (out->hpsb_coils & (1u << 1)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_5, (out->hpsb_coils & (1u << 2)) ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_6, out->lpsb1_coils[0] ? true : false);
	H2Map_WriteAggBit(AGG_BIT_CMD_ONOFF_7, out->lpsb1_coils[1] ? true : false);
}
