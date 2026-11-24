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
#include "net_service.h"

_Static_assert(WIFI_DATA_SSID_MAX_LEN == NET_SSID_MAX_LEN, "SSID max length mismatch");
_Static_assert(WIFI_DATA_PASS_MIN_LEN == NET_PASSWORD_MIN_LEN, "Password min length mismatch");
_Static_assert(WIFI_DATA_PASS_MAX_LEN == NET_PASSWORD_MAX_LEN, "Password max length mismatch");
_Static_assert(WIFI_DATA_SCAN_MAX_APS == NET_SCAN_MAX_APS, "Scan max APs mismatch");

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

/* MQTT 服务由 main.c 在网络初始化阶段统一启动，此处留空以保持与模拟器接口一致。 */
void mqtt_data_init(void)
{
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

static wifi_data_auth_t map_wifi_auth(net_auth_t auth)
{
    switch (auth) {
    case NET_AUTH_OPEN:
        return WIFI_DATA_AUTH_OPEN;
    case NET_AUTH_PSK:
        return WIFI_DATA_AUTH_PSK;
    case NET_AUTH_UNSUPPORTED:
    default:
        return WIFI_DATA_AUTH_UNSUPPORTED;
    }
}

static wifi_data_scan_state_t map_wifi_scan_state(net_scan_state_t state)
{
    switch (state) {
    case NET_SCAN_IDLE:
        return WIFI_DATA_SCAN_IDLE;
    case NET_SCAN_RUNNING:
        return WIFI_DATA_SCAN_RUNNING;
    case NET_SCAN_DONE:
        return WIFI_DATA_SCAN_DONE;
    case NET_SCAN_FAILED:
    default:
        return WIFI_DATA_SCAN_FAILED;
    }
}

static wifi_data_state_t map_wifi_state(net_state_t state)
{
    switch (state) {
    case NET_STATE_DISCONNECTED:
        return WIFI_DATA_STATE_DISCONNECTED;
    case NET_STATE_CONNECTING:
        return WIFI_DATA_STATE_CONNECTING;
    case NET_STATE_CONNECTED:
        return WIFI_DATA_STATE_CONNECTED;
    case NET_STATE_FAILED:
    default:
        return WIFI_DATA_STATE_FAILED;
    }
}

static wifi_data_fail_t map_wifi_fail(net_fail_t fail)
{
    switch (fail) {
    case NET_FAIL_NONE:
        return WIFI_DATA_FAIL_NONE;
    case NET_FAIL_WRONG_PASSWORD:
        return WIFI_DATA_FAIL_WRONG_PASSWORD;
    case NET_FAIL_NOT_FOUND:
        return WIFI_DATA_FAIL_NOT_FOUND;
    case NET_FAIL_OTHER:
    default:
        return WIFI_DATA_FAIL_OTHER;
    }
}

wifi_data_err_t wifi_data_scan_start(void)
{
    esp_err_t err = net_service_scan_start();

    if (err == ESP_OK) {
        return WIFI_DATA_OK;
    }
    if (err == ESP_ERR_NO_MEM) {
        return WIFI_DATA_ERR_NO_MEM;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        net_status_t st;
        net_service_get_status(&st);
        return (st.state == NET_STATE_CONNECTING) ? WIFI_DATA_ERR_BUSY : WIFI_DATA_ERR_NOT_READY;
    }
    return WIFI_DATA_ERR_FAIL;
}

wifi_data_scan_state_t wifi_data_scan_get(wifi_data_ap_t *aps, size_t max, size_t *count)
{
    net_ap_t raw[NET_SCAN_MAX_APS];
    size_t want = (max < NET_SCAN_MAX_APS) ? max : NET_SCAN_MAX_APS;
    size_t n = 0;
    net_scan_state_t state = net_service_scan_get(aps != NULL ? raw : NULL, want, &n);

    if (state == NET_SCAN_DONE && aps != NULL) {
        for (size_t i = 0; i < n; i++) {
            strlcpy(aps[i].ssid, raw[i].ssid, sizeof(aps[i].ssid));
            aps[i].rssi = raw[i].rssi;
            aps[i].auth = map_wifi_auth(raw[i].auth);
        }
    }
    if (count != NULL) {
        *count = n;
    }
    return map_wifi_scan_state(state);
}

wifi_data_err_t wifi_data_connect(const char *ssid, const char *password)
{
    esp_err_t err = net_service_connect(ssid, password);

    if (err == ESP_OK) {
        return WIFI_DATA_OK;
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return WIFI_DATA_ERR_INVALID_ARG;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return WIFI_DATA_ERR_NOT_READY;
    }
    return WIFI_DATA_ERR_FAIL;
}

void wifi_data_disconnect(void)
{
    net_service_disconnect();
}

void wifi_data_get_status(wifi_data_status_t *out)
{
    net_status_t src;

    if (out == NULL) {
        return;
    }
    net_service_get_status(&src);
    memset(out, 0, sizeof(*out));
    out->state = map_wifi_state(src.state);
    out->fail = map_wifi_fail(src.fail);
    strlcpy(out->ssid, src.ssid, sizeof(out->ssid));
}

#endif /* !DASHBOARD_DATA_MOCK */
