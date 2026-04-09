/**
 * @file board_rtc.h
 * @brief RTC register helper for Modbus bridge.
 */
#ifndef BOARD_RTC_H
#define BOARD_RTC_H

#include <stdint.h>

void BoardRtc_Init(void);

/* 7-word view for Modbus 890..896: year,month,day,weekday,hour,minute,second */
int BoardRtc_ReadWordRegs(uint16_t *out7);
int BoardRtc_WriteWordRegs(const uint16_t *in7);

#endif /* BOARD_RTC_H */
