/*
 * app_event.h
 * 统一事件管理 - 低频应用层状态与控制命令事件定义
 *
 * 约定：
 *  - 高频遥测数据（IMU 姿态、BMS CAN）由服务内部缓存最新快照，消费方按需调用 *_get_latest() 读取，
 *    不向系统默认事件循环高频投递，以免与 Wi-Fi / IP 事件竞争队列
 *  - 低频状态与命令事件（对时、MQTT 连接/下行控制命令、传感器掉线告警）通过 APP_EVENTS 发布
 *  - 底层驱动（components/board、components/mpu6050）不发布应用事件，网络状态直接使用 ESP-IDF 自带的
 *    WIFI_EVENT / IP_EVENT
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
    /* 时间相关事件 */
    APP_TIME_SYNC_STARTED = 0,     // 开始 SNTP 对时
    APP_TIME_SYNCED,               // SNTP 对时完成，系统时间可用
    APP_TIME_SYNC_FAILED,          // SNTP 对时超时

    /* MQTT 相关事件（由 mqtt_service 发布） */
    APP_MQTT_CONNECTED,            // MQTT 连接成功
    APP_MQTT_DISCONNECTED,         // MQTT 断开连接
    APP_MQTT_DEVICE_CONTROL,       // 收到下行设备控制命令，数据为 app_device_control_t

    /* 系统告警事件 */
    APP_SENSOR_ERROR,              // 传感器错误（字符串消息）
} app_event_id_t;

/* ==================== 事件数据结构 ==================== */

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
    APP_CTRL_ACTION_FORWARD,       // forward：向前行驶（大屏复用 action="other" + other_cmd_info="forward"）
    APP_CTRL_ACTION_BACKWARD,      // backward：向后行驶（大屏复用 action="other" + other_cmd_info="backward"）
    APP_CTRL_ACTION_LEFT,          // left：向左行驶（大屏复用 action="other" + other_cmd_info="left"）
    APP_CTRL_ACTION_RIGHT,         // right：向右行驶（大屏复用 action="other" + other_cmd_info="right"）
    APP_CTRL_ACTION_OTHER,         // other：其他自定义命令（内容见 other_cmd_info）
} app_control_action_t;

typedef struct {
    uint32_t seq;                                        // 单调递增序号（从 1 开始），用于判断是否有新命令
    uint8_t device_id;                                   // 设备序号 1~9
    app_control_action_t action;                         // 控制动作
    int64_t timestamp_ms;                                // 消息中的毫秒时间戳，没有时为 0
    int64_t received_us;                                 // 本机接收时间（esp_timer_get_time，微秒）
    char other_cmd_info[APP_CONTROL_OTHER_CMD_MAX_LEN];  // 原始 other_cmd_info（自定义命令或方向命令字符串）
    char parameters[APP_CONTROL_PARAMS_MAX_LEN];         // data.parameters 的紧凑 JSON 字符串，空对象为 "{}"
} app_device_control_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化应用事件系统（创建默认事件循环）
 */
void app_event_init(void);

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
 * @brief 将控制动作枚举转为协议字符串（"start" / "stop" / "reset" / "maintenance" /
 *        "forward" / "backward" / "left" / "right" / "other"）
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

#endif /* _APP_EVENT_H_ */
