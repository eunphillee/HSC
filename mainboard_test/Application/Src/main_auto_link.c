/**
 * @file main_auto_link.c
 * @brief Virtual-bit based DI->Relay link for channels 1..4.
 */
#include "main_auto_link.h"
#include "io_map.h"
#include "stm32f2xx_hal.h"

static uint8_t s_virt_en[4];
static uint8_t s_manual_override[4];

void MainAutoLink_Init(void)
{
    for (int i = 0; i < 4; i++) {
        s_virt_en[i] = 0u;
        s_manual_override[i] = 0u;
    }
}

void MainAutoLink_OnManualRelay(uint8_t relay_index_0_3)
{
    if (relay_index_0_3 < 4u)
        s_manual_override[relay_index_0_3] = 1u;
}

void MainAutoLink_OnVirtualCoil(uint8_t channel_index_0_3, uint8_t on)
{
    if (channel_index_0_3 >= 4u) return;
    s_virt_en[channel_index_0_3] = on ? 1u : 0u;
    /* allow relink when PC reconfigures virtual bit */
    s_manual_override[channel_index_0_3] = 0u;
}

uint16_t MainAutoLink_GetVirtEnableWord(uint8_t channel_index_0_3)
{
    if (channel_index_0_3 >= 4u) return 0u;
    return s_virt_en[channel_index_0_3] ? 1u : 0u;
}

void MainAutoLink_Tick(void)
{
    /* Rate-limit to 20ms intervals to avoid flooding the AHB bus with continuous
     * GPIO ODR writes, which creates supply noise that disrupts RS485 reception. */
    static uint32_t s_last_ms = 0u;
    uint32_t now = HAL_GetTick();
    if ((now - s_last_ms) < 20u) return;
    s_last_ms = now;

    for (uint8_t ch = 0; ch < 4u; ch++) {
        if (s_virt_en[ch]) continue;  /* virt=1: freeze current output, ignore DI */
        /* virt=0: DI -> Relay. Only write GPIO when state actually needs to change
         * to eliminate continuous ODR bus transactions while relay is steady. */
        uint8_t di  = IO_Main_ReadDI((MainDiChannel_t)ch);
        uint8_t cur = IO_Main_ReadDO((MainDoChannel_t)ch);
        uint8_t want = di ? 1u : 0u;
        if (cur != want) {
            IO_Main_WriteDO((MainDoChannel_t)ch, want);
        }
    }
}
