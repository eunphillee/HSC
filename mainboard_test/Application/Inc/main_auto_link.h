/**
 * @file main_auto_link.h
 * @brief DI1..4 -> Relay1..4 auto link controlled by virtual bits(coil20..23).
 */
#ifndef MAIN_AUTO_LINK_H
#define MAIN_AUTO_LINK_H

#include <stdint.h>

void MainAutoLink_Init(void);
void MainAutoLink_Tick(void);

void MainAutoLink_OnManualRelay(uint8_t relay_index_0_3);
void MainAutoLink_OnVirtualCoil(uint8_t channel_index_0_3, uint8_t on);
uint16_t MainAutoLink_GetVirtEnableWord(uint8_t channel_index_0_3);

#endif /* MAIN_AUTO_LINK_H */
