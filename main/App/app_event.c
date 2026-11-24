/*
 * app_event.c
 * 统一事件管理实现
 */

#include "app_event.h"
#include "esp_log.h"
#include "esp_event.h"
#include <stdio.h>
#include <string.h>

#define TAG "APP_EVENT"
#define APP_EVENT_POST_TIMEOUT_MS   100

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

static esp_err_t post_event(app_event_id_t id, const void *data, size_t size, const char *name)
{
    esp_err_t err = esp_event_post(APP_EVENTS, id, data, size, pdMS_TO_TICKS(APP_EVENT_POST_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to post %s: %s", name, esp_err_to_name(err));
    }
    return err;
}

/* ==================== 公开接口 ==================== */

void app_event_init(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "App event system initialized");
}

void app_event_publish_time_sync_started(void)
{
    (void)post_event(APP_TIME_SYNC_STARTED, NULL, 0, "time sync started");
}

void app_event_publish_time_synced(void)
{
    (void)post_event(APP_TIME_SYNCED, NULL, 0, "time synced");
}

void app_event_publish_time_sync_failed(void)
{
    (void)post_event(APP_TIME_SYNC_FAILED, NULL, 0, "time sync failed");
}

void app_event_publish_mqtt_status(bool connected)
{
    (void)post_event(connected ? APP_MQTT_CONNECTED : APP_MQTT_DISCONNECTED,
                     NULL, 0, connected ? "MQTT connected" : "MQTT disconnected");

    ESP_LOGI(TAG, "MQTT %s", connected ? "connected" : "disconnected");
}

const char *app_control_action_to_str(app_control_action_t action)
{
    switch (action) {
    case APP_CTRL_ACTION_START:
        return "start";
    case APP_CTRL_ACTION_STOP:
        return "stop";
    case APP_CTRL_ACTION_RESET:
        return "reset";
    case APP_CTRL_ACTION_MAINTENANCE:
        return "maintenance";
    case APP_CTRL_ACTION_FORWARD:
        return "forward";
    case APP_CTRL_ACTION_BACKWARD:
        return "backward";
    case APP_CTRL_ACTION_LEFT:
        return "left";
    case APP_CTRL_ACTION_RIGHT:
        return "right";
    case APP_CTRL_ACTION_OTHER:
        return "other";
    default:
        return "unknown";
    }
}

void app_event_publish_device_control(const app_device_control_t *cmd)
{
    if (cmd == NULL) {
        return;
    }

    if (post_event(APP_MQTT_DEVICE_CONTROL, cmd, sizeof(*cmd), "device control") != ESP_OK) {
        return;
    }

    ESP_LOGD(TAG, "Device control posted: device=%u action=%s",
             (unsigned)cmd->device_id, app_control_action_to_str(cmd->action));
}

void app_event_publish_sensor_error(const char *sensor_name, int error_code)
{
    if (sensor_name == NULL) {
        sensor_name = "unknown";
    }

    char error_msg[64];
    snprintf(error_msg, sizeof(error_msg), "%s error: %d", sensor_name, error_code);

    (void)post_event(APP_SENSOR_ERROR, error_msg, strlen(error_msg) + 1, "sensor error");

    ESP_LOGE(TAG, "%s", error_msg);
}
