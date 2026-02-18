/*
 * app_event.h
 * 统一事件管理 - 所有应用层事件在这里定义
 */

#ifndef _APP_EVENT_H_
#define _APP_EVENT_H_

#include "esp_event.h"

/* ==================== 事件基类 ==================== */
ESP_EVENT_DECLARE_BASE(APP_EVENTS);

/* ==================== 事件ID定义 ==================== */
typedef enum {
    /* MPU6050 相关事件 */
    APP_MPU6050_DATA_UPDATED = 0,   // MPU6050 数据已更新（包含 pitch/roll/yaw）
    
    /* 时间相关事件 */
    APP_TIME_INITIALIZED,          // 时间首次同步完成（SNTP 完成后触发）
    
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

/* MPU6050 数据事件 - 通过事件传递数据 */
typedef struct {
    float pitch;
    float roll;
    float yaw;
} app_event_mpu6050_data_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化应用事件系统
 * @note 内部注册默认事件处理程序，初始化 Mutex 等
 */
void app_event_init(void);

/**
 * @brief 发布 MPU6050 数据更新事件
 * @param pitch 俯仰角
 * @param roll 横滚角
 * @param yaw 偏航角
 */
void app_event_publish_mpu6050_data(float pitch, float roll, float yaw);

/**
 * @brief 发布时间初始化完成事件
 */
void app_event_publish_time_initialized(void);

/**
 * @brief 发布 MQTT 连接状态事件
 * @param connected true=连接成功, false=断开连接
 */
void app_event_publish_mqtt_status(bool connected);

/**
 * @brief 发布 MQTT 发布结果事件
 * @param success true=成功, false=失败
 * @param topic 发布的主题（仅用于日志）
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
