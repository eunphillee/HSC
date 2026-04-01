/**
 * @file board_rtc.c
 * @brief RTC helpers using CubeMX hrtc handle.
 */
#include "board_rtc.h"
#include "main.h"

static uint8_t hal_weekday_from_user(uint16_t user_wd)
{
    /* User: 0=Sun .. 6=Sat, HAL: 1=Mon .. 7=Sun */
    if (user_wd == 0u) return RTC_WEEKDAY_SUNDAY;
    if (user_wd <= 6u) return (uint8_t)user_wd;
    return RTC_WEEKDAY_SUNDAY;
}

static uint16_t user_weekday_from_hal(uint8_t hal_wd)
{
    if (hal_wd == RTC_WEEKDAY_SUNDAY) return 0u;
    return (uint16_t)hal_wd;
}

void BoardRtc_Init(void)
{
    if (__HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc) == 0U) {
        RTC_TimeTypeDef t = {0};
        RTC_DateTypeDef d = {0};
        t.Hours = 0;
        t.Minutes = 0;
        t.Seconds = 0;
        t.TimeFormat = 0;
        t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        t.StoreOperation = RTC_STOREOPERATION_RESET;
        d.WeekDay = RTC_WEEKDAY_SATURDAY;
        d.Month = RTC_MONTH_JANUARY;
        d.Date = 1;
        d.Year = 0; /* 2000 */
        (void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
        (void)HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
    }
}

int BoardRtc_ReadWordRegs(uint16_t *out7)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!out7) return -1;
    if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return -1;
    if (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return -1;

    out7[0] = (uint16_t)(2000u + d.Year);
    out7[1] = (uint16_t)d.Month;
    out7[2] = (uint16_t)d.Date;
    out7[3] = user_weekday_from_hal(d.WeekDay);
    out7[4] = (uint16_t)t.Hours;
    out7[5] = (uint16_t)t.Minutes;
    out7[6] = (uint16_t)t.Seconds;
    return 0;
}

int BoardRtc_WriteWordRegs(const uint16_t *in7)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    if (!in7) return -1;
    if (in7[0] < 2000u || in7[0] > 2099u) return -1;
    if (in7[1] < 1u || in7[1] > 12u) return -1;
    if (in7[2] < 1u || in7[2] > 31u) return -1;
    if (in7[3] > 6u) return -1;
    if (in7[4] > 23u) return -1;
    if (in7[5] > 59u) return -1;
    if (in7[6] > 59u) return -1;

    t.Hours = (uint8_t)in7[4];
    t.Minutes = (uint8_t)in7[5];
    t.Seconds = (uint8_t)in7[6];
    t.TimeFormat = 0;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    d.Year = (uint8_t)(in7[0] - 2000u);
    d.Month = (uint8_t)in7[1];
    d.Date = (uint8_t)in7[2];
    d.WeekDay = hal_weekday_from_user(in7[3]);

    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return -1;
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return -1;
    return 0;
}
