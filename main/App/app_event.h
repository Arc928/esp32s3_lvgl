/*
 * app_event.h
 * 统一事件管理 - 所有应用层事件都在这里定义
 *
 * 约定：
 *  - 应用层模块之间只通过 APP_EVENTS 通信，事件 ID 和数据结构只在本文件定义
 *  - 底层驱动（components/board、components/mpu6050）不发布应用事件，网络状态直接使用 ESP-IDF 自带的
 *    WIFI_EVENT / IP_EVENT
 *  - UI 不订阅高频数据事件，而是在 LVGL 定时器里向各服务读取最新数据
 */

#ifndef _APP_EVENT_H_
#define _APP_EVENT_H_

#include <stdbool.h>
#include "esp_event.h"

/* ==================== 事件基类 ==================== */
ESP_EVENT_DECLARE_BASE(APP_EVENTS);

/* ==================== 事件ID定义 ==================== */
typedef enum {
    /* 姿态相关事件 */
    APP_IMU_DATA_UPDATED = 0,      // 姿态数据已更新，数据为 app_imu_data_t

    /* 时间相关事件 */
    APP_TIME_SYNC_STARTED,         // 开始 SNTP 对时
    APP_TIME_SYNCED,               // SNTP 对时完成，系统时间可用
    APP_TIME_SYNC_FAILED,          // SNTP 对时超时

    /* MQTT 相关事件（预留给后续使用） */
    APP_MQTT_CONNECTED,            // MQTT 连接成功
    APP_MQTT_DISCONNECTED,         // MQTT 断开连接
    APP_MQTT_PUBLISH_SUCCESS,      // MQTT 发布成功
    APP_MQTT_PUBLISH_FAILED,       // MQTT 发布失败

    /* 系统事件 */
    APP_SENSOR_ERROR,              // 传感器错误
    APP_NETWORK_ERROR,             // 网络错误
} app_event_id_t;

/* ==================== 事件数据结构 ==================== */

/* 姿态数据（单位：度） */
typedef struct {
    float pitch;
    float roll;
    float yaw;
} app_imu_data_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化应用事件系统（创建默认事件循环）
 */
void app_event_init(void);

/**
 * @brief 发布姿态数据更新事件
 * @param data 姿态数据，事件循环会拷贝一份
 */
void app_event_publish_imu_data(const app_imu_data_t *data);

/**
 * @brief 发布 SNTP 对时开始事件
 */
void app_event_publish_time_sync_started(void);

/**
 * @brief 发布 SNTP 对时完成事件
 */
void app_event_publish_time_synced(void);

/**
 * @brief 发布 SNTP 对时失败事件
 */
void app_event_publish_time_sync_failed(void);

/**
 * @brief 发布 MQTT 连接状态事件
 * @param connected true=连接成功, false=断开连接
 */
void app_event_publish_mqtt_status(bool connected);

/**
 * @brief 发布 MQTT 发布结果事件
 * @param success true=成功, false=失败
 * @param topic 发布的主题
 */
void app_event_publish_mqtt_publish_result(bool success, const char *topic);

/**
 * @brief 发布传感器错误事件
 * @param sensor_name 传感器名称
 * @param error_code 错误码
 */
void app_event_publish_sensor_error(const char *sensor_name, int error_code);

/**
 * @brief 发布网络错误事件
 * @param error_msg 错误信息
 */
void app_event_publish_network_error(const char *error_msg);

#endif /* _APP_EVENT_H_ */
