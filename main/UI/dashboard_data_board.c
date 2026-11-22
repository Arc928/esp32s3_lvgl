/*
 * 界面数据源（板上实现）
 *
 * custom/ 里的界面代码来自 lvgl_bms_ui 模拟器项目，只通过 dashboard_data.h 的接口取数据。
 * 模拟器用 dashboard_data_mock.c 的假数据；板上在 main/CMakeLists.txt 里定义
 * DASHBOARD_DATA_MOCK=0，改由这里从各服务读取。
 *
 * 所有接口都在 LVGL 定时器或点击回调里调用，对应服务的取数接口均为线程安全。
 */

#include "dashboard_data.h"

#if !DASHBOARD_DATA_MOCK

#include <limits.h>
#include <string.h>
#include "esp_timer.h"
#include "lvgl.h"
#include "bms_service.h"
#include "imu_service.h"
#include "mqtt_service.h"

/* esp_timer_get_time() 从上电起算，lv_tick_get() 从 LVGL 适配层启动起算，
 * 两者存在固定偏移；先算已过去时长再换算到 LVGL tick 基准，避免相对时间下溢。 */
static uint32_t esp_time_us_to_lv_tick_ms(int64_t time_us)
{
    int64_t age_us = esp_timer_get_time() - time_us;
    uint32_t age_ms;

    if (age_us <= 0) {
        age_ms = 0U;
    } else if (age_us >= (int64_t)UINT32_MAX * 1000LL) {
        age_ms = UINT32_MAX;
    } else {
        age_ms = (uint32_t)(age_us / 1000LL);
    }

    return lv_tick_get() - age_ms;
}

static mqtt_cmd_action_t map_control_action(app_control_action_t action)
{
    switch (action) {
    case APP_CTRL_ACTION_START:
        return MQTT_CMD_ACTION_START;
    case APP_CTRL_ACTION_STOP:
        return MQTT_CMD_ACTION_STOP;
    case APP_CTRL_ACTION_RESET:
        return MQTT_CMD_ACTION_RESET;
    case APP_CTRL_ACTION_MAINTENANCE:
        return MQTT_CMD_ACTION_MAINTENANCE;
    case APP_CTRL_ACTION_FORWARD:
        return MQTT_CMD_ACTION_FORWARD;
    case APP_CTRL_ACTION_BACKWARD:
        return MQTT_CMD_ACTION_BACKWARD;
    case APP_CTRL_ACTION_LEFT:
        return MQTT_CMD_ACTION_LEFT;
    case APP_CTRL_ACTION_RIGHT:
        return MQTT_CMD_ACTION_RIGHT;
    case APP_CTRL_ACTION_OTHER:
    default:
        return MQTT_CMD_ACTION_OTHER;
    }
}

static void convert_control_cmd(const app_device_control_t *src, mqtt_cmd_data_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->seq = src->seq;
    dst->device_id = src->device_id;
    dst->action = map_control_action(src->action);
    dst->timestamp_ms = src->timestamp_ms;
    dst->received_tick_ms = esp_time_us_to_lv_tick_ms(src->received_us);
    strlcpy(dst->other_cmd_info, src->other_cmd_info, sizeof(dst->other_cmd_info));
    strlcpy(dst->parameters, src->parameters, sizeof(dst->parameters));
}

/* 收到过至少一帧有效 BMS CAN 报文后返回 true；界面依据 last_update_ms 判断 3 秒离线。 */
bool bms_data_get_latest(bms_data_t *out)
{
    bms_can_data_t src;

    if (out == NULL || !bms_service_get_latest(&src)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->pack_voltage_v = src.pack_voltage_v;
    out->pack_current_a = src.pack_current_a;
    out->soc_percent = src.soc_percent;
    out->soh_percent = src.soh_percent;
    out->full_charge_capacity_ah = src.full_charge_capacity_ah;

    out->max_cell_voltage_v = src.max_cell_voltage_v;
    out->min_cell_voltage_v = src.min_cell_voltage_v;
    out->max_cell_position = src.max_cell_position;
    out->min_cell_position = src.min_cell_position;

    out->max_temperature_c = src.max_temperature_c;
    out->min_temperature_c = src.min_temperature_c;
    out->max_temperature_position = src.max_temperature_position;
    out->min_temperature_position = src.min_temperature_position;
    out->relay_charge = src.relay_charge;
    out->relay_discharge = src.relay_discharge;
    out->relay_precharge = src.relay_precharge;
    out->relay_heat = src.relay_heat;
    out->relay_negative = src.relay_negative;
    out->relay_charge_stick = src.relay_charge_stick;
    out->relay_discharge_stick = src.relay_discharge_stick;
    out->relay_negative_stick = src.relay_negative_stick;
    out->relay_raw = src.relay_raw;
    out->fault_code = src.fault_code;

    out->has_alarm = src.has_alarm;
    memcpy(out->alarm, src.alarm, sizeof(out->alarm));

    out->rx_count = src.rx_count;
    out->last_update_ms = esp_time_us_to_lv_tick_ms(src.last_update_us);
    return true;
}

/* 传感器还在初始化或已离线时返回 false，界面显示 "--" */
bool imu_data_get_latest(imu_data_t *out)
{
    app_imu_data_t data;

    if (!imu_service_get_latest(&data)) {
        return false;
    }
    out->pitch_deg = data.pitch;
    out->roll_deg = data.roll;
    out->yaw_deg = data.yaw;
    return true;
}

bool mqtt_data_is_connected(void)
{
    return mqtt_service_is_connected();
}

bool mqtt_data_get_latest_cmd(mqtt_cmd_data_t *out)
{
    app_device_control_t src;

    if (out == NULL || !mqtt_service_get_last_control(&src)) {
        return false;
    }
    convert_control_cmd(&src, out);
    return true;
}

bool mqtt_data_get_device_cmd(uint8_t device_id, mqtt_cmd_data_t *out)
{
    app_device_control_t src;

    if (out == NULL || !mqtt_service_get_latest_control(device_id, &src)) {
        return false;
    }
    convert_control_cmd(&src, out);
    return true;
}

#endif /* !DASHBOARD_DATA_MOCK */
