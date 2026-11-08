/*
 * Data sources for the dashboard.
 *
 * The view only reads these two functions. On the simulator they are served by
 * dashboard_data_mock.c; on the board, set DASHBOARD_DATA_MOCK to 0 and implement
 * them on top of bms_can_get_latest() and the MPU6050 driver.
 */

#ifndef DASHBOARD_DATA_H
#define DASHBOARD_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#ifndef DASHBOARD_DATA_MOCK
#define DASHBOARD_DATA_MOCK 1
#endif

/* Field names match bms_can_data_t (see docs/bms_can_display_design.md). */
typedef struct {
    /* 0x180700F4: pack */
    float pack_voltage_v;
    float pack_current_a;
    float soc_percent;
    float soh_percent;
    float full_charge_capacity_ah;

    /* 0x180701F4: cells */
    float max_cell_voltage_v;
    float min_cell_voltage_v;
    uint8_t max_cell_position;
    uint8_t min_cell_position;

    /* 0x180702F4: temperature, relays, fault */
    float max_temperature_c;
    float min_temperature_c;
    uint8_t max_temperature_position;
    uint8_t min_temperature_position;
    bool relay_charge;
    bool relay_discharge;
    bool relay_precharge;
    bool relay_heat;
    bool relay_negative;
    bool relay_charge_stick;
    bool relay_discharge_stick;
    bool relay_negative_stick;
    uint8_t relay_raw;
    uint8_t fault_code;

    /* 0x180703F4: alarms */
    bool has_alarm;
    uint8_t alarm[5];

    /* Receiver state */
    uint32_t rx_count;
    uint32_t last_update_ms; /* lv_tick_get() time of the last decoded frame */
} bms_data_t;

typedef struct {
    float pitch_deg;
    float roll_deg;
    float yaw_deg;
} imu_data_t;

/* Both return false until the first valid sample has been received. */
bool bms_data_get_latest(bms_data_t *out);
bool imu_data_get_latest(imu_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DASHBOARD_DATA_H */
