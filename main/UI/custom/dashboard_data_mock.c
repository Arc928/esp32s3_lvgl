/*
 * Simulated BMS and IMU data for the desktop simulator.
 * Values are derived from the LVGL tick, so they move smoothly no matter how
 * often the view refreshes.
 */

#include "dashboard_data.h"

#if DASHBOARD_DATA_MOCK

#include <math.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"

#define MOCK_PI                 3.14159265f
#define MOCK_FIRST_CMD_DELAY_MS 1800U

typedef struct {
    uint8_t device_id;
    mqtt_cmd_action_t action;
    bool include_timestamp;
    const char *other_cmd_info;
    const char *parameters;
} mock_mqtt_sample_t;

static const mock_mqtt_sample_t k_mock_cmds[] = {
    {
        3U,
        MQTT_CMD_ACTION_START,
        true,
        "",
        "{\"speed\":1.5,\"row\":3,\"mode\":\"auto\"}"
    },
    {
        4U,
        MQTT_CMD_ACTION_STOP,
        true,
        "",
        "{\"reason\":\"manual_override\",\"pump\":\"off\"}"
    },
    {
        1U,
        MQTT_CMD_ACTION_RESET,
        false,
        "",
        "{}"
    },
    {
        6U,
        MQTT_CMD_ACTION_MAINTENANCE,
        true,
        "",
        "{\"check\":\"battery_and_blade\",\"lock_drive\":true}"
    },
    {
        2U,
        MQTT_CMD_ACTION_FORWARD,
        true,
        "forward",
        "{\"speed\":0.8,\"distance_m\":5}"
    },
    {
        2U,
        MQTT_CMD_ACTION_BACKWARD,
        true,
        "backward",
        "{\"speed\":0.5,\"distance_m\":2}"
    },
    {
        5U,
        MQTT_CMD_ACTION_LEFT,
        true,
        "left",
        "{\"angle_deg\":30,\"speed\":0.4}"
    },
    {
        7U,
        MQTT_CMD_ACTION_RIGHT,
        true,
        "right",
        "{\"angle_deg\":30,\"speed\":0.4}"
    },
    {
        8U,
        MQTT_CMD_ACTION_OTHER,
        true,
        "调整行距并执行预热自检",
        "{\"row_spacing_cm\":35,\"preheat_s\":60,\"tray_index\":2,\"retry\":3,\"calibrate\":true}"
    },
};

#define MOCK_CMD_COUNT (sizeof(k_mock_cmds) / sizeof(k_mock_cmds[0]))

static mqtt_cmd_data_t s_mock_device_cmds[MQTT_CMD_DEVICE_MAX];
static bool s_mock_has_device_cmd[MQTT_CMD_DEVICE_MAX];
static mqtt_cmd_data_t s_mock_latest_cmd;
static uint32_t s_mock_seq;
static uint32_t s_mock_cursor;
static bool s_mock_boot_triggered;

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if(dst_size == 0U) {
        return;
    }
    if(src == NULL) {
        dst[0] = '\0';
        return;
    }
    lv_snprintf(dst, dst_size, "%s", src);
}

static void push_mock_sample(const mock_mqtt_sample_t *sample)
{
    mqtt_cmd_data_t cmd;
    time_t now_s = time(NULL);

    memset(&cmd, 0, sizeof(cmd));
    s_mock_seq++;
    cmd.seq = s_mock_seq;
    cmd.device_id = sample->device_id;
    cmd.action = sample->action;
    cmd.timestamp_ms = (sample->include_timestamp && now_s > 0) ? ((int64_t)now_s * 1000LL) : 0LL;
    cmd.received_tick_ms = lv_tick_get();
    copy_str(cmd.other_cmd_info, sizeof(cmd.other_cmd_info), sample->other_cmd_info);
    copy_str(cmd.parameters, sizeof(cmd.parameters), sample->parameters);

    s_mock_latest_cmd = cmd;
    if(cmd.device_id >= MQTT_CMD_DEVICE_MIN && cmd.device_id <= MQTT_CMD_DEVICE_MAX) {
        size_t idx = (size_t)(cmd.device_id - MQTT_CMD_DEVICE_MIN);
        s_mock_device_cmds[idx] = cmd;
        s_mock_has_device_cmd[idx] = true;
    }
}

void mqtt_data_mock_trigger_next(void)
{
    s_mock_boot_triggered = true;
    push_mock_sample(&k_mock_cmds[s_mock_cursor]);
    s_mock_cursor = (uint32_t)((s_mock_cursor + 1U) % MOCK_CMD_COUNT);
}

static void maybe_trigger_initial_mock(void)
{
    if(!s_mock_boot_triggered && lv_tick_get() >= MOCK_FIRST_CMD_DELAY_MS) {
        mqtt_data_mock_trigger_next();
    }
}

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

bool mqtt_data_is_connected(void)
{
    return true;
}

bool mqtt_data_get_latest_cmd(mqtt_cmd_data_t *out)
{
    maybe_trigger_initial_mock();
    if(out == NULL || s_mock_seq == 0U) {
        return false;
    }
    *out = s_mock_latest_cmd;
    return true;
}

bool mqtt_data_get_device_cmd(uint8_t device_id, mqtt_cmd_data_t *out)
{
    size_t idx;

    maybe_trigger_initial_mock();
    if(out == NULL || device_id < MQTT_CMD_DEVICE_MIN || device_id > MQTT_CMD_DEVICE_MAX) {
        return false;
    }
    idx = (size_t)(device_id - MQTT_CMD_DEVICE_MIN);
    if(!s_mock_has_device_cmd[idx]) {
        return false;
    }
    *out = s_mock_device_cmds[idx];
    return true;
}

#endif /* DASHBOARD_DATA_MOCK */
