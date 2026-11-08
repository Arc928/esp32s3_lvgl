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

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

/* ==================== 公开接口 ==================== */

void app_event_init(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "App event system initialized");
}

void app_event_publish_imu_data(const app_imu_data_t *data)
{
    esp_event_post(APP_EVENTS, APP_IMU_DATA_UPDATED,
                   data, sizeof(*data), portMAX_DELAY);
}

void app_event_publish_time_sync_started(void)
{
    esp_event_post(APP_EVENTS, APP_TIME_SYNC_STARTED, NULL, 0, portMAX_DELAY);
}

void app_event_publish_time_synced(void)
{
    esp_event_post(APP_EVENTS, APP_TIME_SYNCED, NULL, 0, portMAX_DELAY);
}

void app_event_publish_time_sync_failed(void)
{
    esp_event_post(APP_EVENTS, APP_TIME_SYNC_FAILED, NULL, 0, portMAX_DELAY);
}

void app_event_publish_mqtt_status(bool connected)
{
    esp_event_post(APP_EVENTS, 
                   connected ? APP_MQTT_CONNECTED : APP_MQTT_DISCONNECTED, 
                   NULL, 0, portMAX_DELAY);
    
    ESP_LOGI(TAG, "MQTT %s", connected ? "connected" : "disconnected");
}

void app_event_publish_mqtt_publish_result(bool success, const char *topic)
{
    if (topic == NULL) {
        topic = "";
    }

    // 事件循环会拷贝数据，长度要包含结尾的 '\0'
    esp_event_post(APP_EVENTS,
                   success ? APP_MQTT_PUBLISH_SUCCESS : APP_MQTT_PUBLISH_FAILED,
                   topic, strlen(topic) + 1, portMAX_DELAY);

    ESP_LOGD(TAG, "MQTT publish %s: %s", success ? "success" : "failed", topic);
}

void app_event_publish_sensor_error(const char *sensor_name, int error_code)
{
    char error_msg[64];
    snprintf(error_msg, sizeof(error_msg), "%s error: %d", sensor_name, error_code);
    
    esp_event_post(APP_EVENTS, APP_SENSOR_ERROR, 
                   error_msg, sizeof(error_msg), portMAX_DELAY);
    
    ESP_LOGE(TAG, "%s", error_msg);
}

void app_event_publish_network_error(const char *error_msg)
{
    if (error_msg == NULL) {
        error_msg = "";
    }

    // 事件循环会拷贝数据，长度要包含结尾的 '\0'
    esp_event_post(APP_EVENTS, APP_NETWORK_ERROR,
                   error_msg, strlen(error_msg) + 1, portMAX_DELAY);
    
    ESP_LOGE(TAG, "Network error: %s", error_msg);
}
