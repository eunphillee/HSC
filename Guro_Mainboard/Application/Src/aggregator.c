#include "aggregator.h"
#include "io_map.h"
#include "modbus_table.h"
#include "modbus_master.h"
#include "main.h"

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

	out->lpsb_coils      = 0;
	out->lpsb_discrete   = 0;
	out->lpsb_status_reg = 0;
	out->lpsb_alarm_reg  = 0;
	out->lpsb_sense_raw  = 0;
	for (int i = 0; i < MODBUS_COIL_COUNT; i++)
		out->lpsb_coils |= (ModbusTable_GetCoil(SLAVE_ID_LPSB, (uint16_t)i) ? (1u << i) : 0);
	for (int i = 0; i < MODBUS_DISCRETE_COUNT; i++)
		out->lpsb_discrete |= (ModbusTable_GetDiscrete(SLAVE_ID_LPSB, (uint16_t)i) ? (1u << i) : 0);
	out->lpsb_status_reg = ModbusTable_GetHoldingReg(SLAVE_ID_LPSB, HOLDING_REG_STATUS);
	out->lpsb_alarm_reg  = ModbusTable_GetHoldingReg(SLAVE_ID_LPSB, HOLDING_REG_ALARM);

	out->error_flags = 0;
	if (!ModbusMaster_IsCommOk(SLAVE_ID_HPSB)) out->error_flags |= AGG_ERR_COMM_HPSB;
	if (!ModbusMaster_IsCommOk(SLAVE_ID_LPSB)) out->error_flags |= AGG_ERR_COMM_LPSB;
}
