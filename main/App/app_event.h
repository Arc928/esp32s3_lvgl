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
#include <stdint.h>
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

    /* MQTT 相关事件（由 mqtt_service 发布） */
    APP_MQTT_CONNECTED,            // MQTT 连接成功
    APP_MQTT_DISCONNECTED,         // MQTT 断开连接
    APP_MQTT_PUBLISH_SUCCESS,      // MQTT 发布成功
    APP_MQTT_PUBLISH_FAILED,       // MQTT 发布失败
    APP_MQTT_DEVICE_CONTROL,       // 收到下行设备控制命令，数据为 app_device_control_t

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

/* 下行设备控制命令（对应协议 type = "device_control"） */
#define APP_CONTROL_DEVICE_MIN          1
#define APP_CONTROL_DEVICE_MAX          9
#define APP_CONTROL_OTHER_CMD_MAX_LEN   128
#define APP_CONTROL_PARAMS_MAX_LEN      256

typedef enum {
    APP_CTRL_ACTION_START = 0,     // start：开始作业
    APP_CTRL_ACTION_STOP,          // stop：停止作业
    APP_CTRL_ACTION_RESET,         // reset：回停车场
    APP_CTRL_ACTION_MAINTENANCE,   // maintenance：维护
    APP_CTRL_ACTION_OTHER,         // other：自定义命令（内容见 other_cmd_info）
} app_control_action_t;

typedef struct {
    uint8_t device_id;                                   // 设备序号 1~9
    app_control_action_t action;                         // 控制动作
    int64_t timestamp_ms;                                // 消息中的毫秒时间戳，没有时为 0
    int64_t received_us;                                 // 本机接收时间（esp_timer_get_time，微秒）
    char other_cmd_info[APP_CONTROL_OTHER_CMD_MAX_LEN];  // action == OTHER 时的命令内容
    char parameters[APP_CONTROL_PARAMS_MAX_LEN];         // data.parameters 的紧凑 JSON 字符串，空对象为 "{}"
} app_device_control_t;

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
 * @brief 将控制动作枚举转为协议字符串（"start" / "stop" / "reset" / "maintenance" / "other"）
 */
const char *app_control_action_to_str(app_control_action_t action);

/**
 * @brief 发布 MQTT 下行设备控制命令事件
 * @param cmd 已校验解析好的控制命令，事件循环会拷贝一份
 */
void app_event_publish_device_control(const app_device_control_t *cmd);

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
