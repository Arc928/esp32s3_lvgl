/*
 * mqtt_service.h
 * MQTT 服务 - 拿到 IP 后连接 broker，订阅并接收下行设备控制命令
 *
 * broker 地址、账号、大棚编号（MQTT_GHCODE）和校验码（MQTT_GHCHECK）放在
 * mqtt_secrets.h（不提交到 git）；新环境请复制 mqtt_secrets.h.example 后填写。
 *
 * 订阅主题：
 *  - greenhouse/machine/control/<MQTT_GHCODE>
 *
 * 下行 JSON（type == "device_control"）经 ghcode、ghcheck、deviceId("1"~"9")、
 * action(start/stop/reset/maintenance/other) 校验通过后：
 *  - 按设备号（1~9）缓存最新一条命令，可用 mqtt_service_get_latest_control() 读取
 *  - 发布 APP_EVENTS / APP_MQTT_DEVICE_CONTROL 事件（数据为 app_device_control_t）
 *
 * 断网、broker 重启后由 esp-mqtt 自动重连并重新订阅。
 * 连接状态通过 APP_EVENTS 发布：APP_MQTT_CONNECTED / APP_MQTT_DISCONNECTED
 */

#ifndef _MQTT_SERVICE_H_
#define _MQTT_SERVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_event.h"

/**
 * @brief 创建 MQTT 客户端并订阅 IP_EVENT_STA_GOT_IP，第一次拿到 IP 后开始连接
 * @note 需在联网（net_service_start）之前调用
 * @return ESP_OK；配置缺失或客户端创建失败时返回错误。broker 连不上不会让这里返回错误
 */
esp_err_t mqtt_service_start(void);

/**
 * @brief 查询当前是否已连接到 MQTT broker（线程安全）
 */
bool mqtt_service_is_connected(void);

/**
 * @brief 读取指定设备最近收到的一条控制命令（线程安全）
 * @param device_id 设备序号（APP_CONTROL_DEVICE_MIN ~ APP_CONTROL_DEVICE_MAX，即 1~9）
 * @param out       输出的控制命令
 * @return true=该设备已收到过有效命令并拷贝到 out；false=参数越界或尚未收到命令
 */
bool mqtt_service_get_latest_control(uint8_t device_id, app_device_control_t *out);

#endif /* _MQTT_SERVICE_H_ */
