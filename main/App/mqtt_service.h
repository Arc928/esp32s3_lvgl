/*
 * mqtt_service.h
 * MQTT 服务 - 拿到 IP 后连接 broker，定期上传姿态数据
 *
 * broker 地址和账号放在 mqtt_secrets.h（不提交到 git），
 * 新环境请复制 mqtt_secrets.h.example 为 mqtt_secrets.h 后填写。
 *
 * 主题（<id> 是 Wi-Fi MAC 的 12 位十六进制，多台设备共用一个 broker 也不会冲突）：
 *  - esp32s3_lvgl/<id>/imu     姿态 JSON，每秒一条：{"pitch":1.23,"roll":-4.56,"yaw":78.90,"ts":1731123456}
 *                              ts 是 Unix 时间（秒），SNTP 对时前没有这个字段；传感器离线时不发
 *  - esp32s3_lvgl/<id>/status  "online" / "offline"，保留消息；设备断电或断网时由 broker 代发 offline（遗嘱）
 *
 * 断网、broker 重启后由 esp-mqtt 自动重连。
 * 连接状态和发布结果通过 APP_EVENTS 发布：
 * APP_MQTT_CONNECTED / APP_MQTT_DISCONNECTED / APP_MQTT_PUBLISH_SUCCESS / APP_MQTT_PUBLISH_FAILED
 */

#ifndef _MQTT_SERVICE_H_
#define _MQTT_SERVICE_H_

#include "esp_err.h"

/**
 * @brief 创建 MQTT 客户端并订阅 IP_EVENT_STA_GOT_IP，第一次拿到 IP 后开始连接
 * @note 需在联网（net_service_start）之前调用
 * @return ESP_OK；客户端或任务创建失败时返回错误。broker 连不上不会让这里返回错误
 */
esp_err_t mqtt_service_start(void);

#endif /* _MQTT_SERVICE_H_ */
