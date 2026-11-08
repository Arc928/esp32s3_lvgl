/*
 * Simulated BMS and IMU data for the desktop simulator.
 * Values are derived from the LVGL tick, so they move smoothly no matter how
 * often the view refreshes.
 */

#include "dashboard_data.h"

#if DASHBOARD_DATA_MOCK

#include <math.h>
#include "lvgl.h"

#define MOCK_PI 3.14159265f

/* Smooth wave in [-1, 1] with the given period. */
static float wave(uint32_t now_ms, uint32_t period_ms, uint32_t phase_ms)
{
    float t = (float)((now_ms + phase_ms) % period_ms) / (float)period_ms;
    return sinf(2.0f * MOCK_PI * t);
}

bool bms_data_get_latest(bms_data_t *out)
{
    uint32_t now = lv_tick_get();
    uint32_t sec = now / 1000U;
    bool alarm_active = (sec % 30U) >= 27U; /* 3 s alarm every 30 s */

    *out = (bms_data_t){0};

    out->pack_voltage_v = 325.6f + 0.6f * wave(now, 9000U, 0U);
    out->pack_current_a = -12.3f + 2.0f * wave(now, 7000U, 1000U);
    out->soc_percent = 86.4f - 0.5f * wave(now, 20000U, 0U);
    out->soh_percent = 98.0f;
    out->full_charge_capacity_ah = 105.0f;

    out->max_cell_voltage_v = 3.456f + 0.002f * wave(now, 5000U, 0U);
    out->min_cell_voltage_v = 3.438f + 0.002f * wave(now, 6000U, 2000U);
    out->max_cell_position = (uint8_t)(8U + (sec / 10U) % 4U);
    out->min_cell_position = (uint8_t)(17U + (sec / 12U) % 3U);

    out->max_temperature_c = 32.5f + 0.4f * wave(now, 11000U, 0U);
    out->min_temperature_c = 27.8f + 0.2f * wave(now, 13000U, 3000U);
    out->max_temperature_position = (uint8_t)(2U + (sec / 15U) % 2U);
    out->min_temperature_position = (uint8_t)(5U + (sec / 15U) % 2U);

    out->relay_charge = (sec % 12U) < 4U;
    out->relay_discharge = true;
    out->relay_precharge = false;
    out->relay_heat = (sec % 16U) < 2U;
    out->relay_negative = true;
    out->relay_raw = (uint8_t)((out->relay_discharge ? 0x01U : 0U) |
                               (out->relay_charge ? 0x02U : 0U) |
                               (out->relay_precharge ? 0x04U : 0U) |
                               (out->relay_heat ? 0x08U : 0U) |
                               (out->relay_discharge_stick ? 0x10U : 0U) |
                               (out->relay_charge_stick ? 0x20U : 0U) |
                               (out->relay_negative ? 0x40U : 0U) |
                               (out->relay_negative_stick ? 0x80U : 0U));
    out->fault_code = 0x00U;

    out->has_alarm = alarm_active;
    out->alarm[0] = alarm_active ? 0x01U : 0x00U;

    out->rx_count = (now / 100U) * 4U; /* 4 frames every 100 ms */
    out->last_update_ms = now;
    return true;
}

bool imu_data_get_latest(imu_data_t *out)
{
    uint32_t now = lv_tick_get();
    float yaw = (float)((now / 50U) % 3600U) / 10.0f; /* 2 deg/s */

    out->pitch_deg = 12.0f * wave(now, 8000U, 0U);
    out->roll_deg = 6.0f * wave(now, 5000U, 1500U);
    out->yaw_deg = yaw > 180.0f ? yaw - 360.0f : yaw;
    return true;
}

#endif /* DASHBOARD_DATA_MOCK */
