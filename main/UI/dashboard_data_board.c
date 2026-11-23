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
#include "imu_service.h"
#include "mqtt_service.h"

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
    /* esp_timer_get_time() 从上电起算，lv_tick_get() 从 LVGL 适配层启动起算，
     * 两者存在固定偏移；先算已过去时长再换算到 LVGL tick 基准，避免相对时间下溢。 */
    int64_t age_us = esp_timer_get_time() - src->received_us;
    uint32_t age_ms;

    if (age_us <= 0) {
        age_ms = 0U;
    } else if (age_us >= (int64_t)UINT32_MAX * 1000LL) {
        age_ms = UINT32_MAX;
    } else {
        age_ms = (uint32_t)(age_us / 1000LL);
    }

    memset(dst, 0, sizeof(*dst));
    dst->seq = src->seq;
    dst->device_id = src->device_id;
    dst->action = map_control_action(src->action);
    dst->timestamp_ms = src->timestamp_ms;
    dst->received_tick_ms = lv_tick_get() - age_ms;
    strlcpy(dst->other_cmd_info, src->other_cmd_info, sizeof(dst->other_cmd_info));
    strlcpy(dst->parameters, src->parameters, sizeof(dst->parameters));
}

/*
 * 本项目还没有 BMS CAN 接收服务，先一直返回 false，界面显示“CAN 离线”。
 * 接入后在这里把最新一帧拷到 out，字段名与 bms_can_data_t 一致，
 * last_update_ms 要用 lv_tick_get() 的时间基准（界面靠它判断 3 秒离线）。
 */
bool bms_data_get_latest(bms_data_t *out)
{
    (void)out;
    return false;
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
