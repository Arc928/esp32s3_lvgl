#ifndef _WIFI_INIT_H_
#define _WIFI_INIT_H_

#include "esp_event.h"

#define EXAMPLE_ESP_WIFI_SSID      "OnePlus Ace"
#define EXAMPLE_ESP_WIFI_PASS      "12345678"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

/* 事件ID定义 */
typedef enum {
    /* WiFi相关事件 */
    WIFI_CONNECTED_FOR_SNTP = 0,    /* WiFi连接成功，触发SNTP同步 */

    /* SNTP相关事件（也可以由其他模块发送） */
    SNTP_SYNC_STARTED,               /* SNTP同步已开始 */
    SNTP_SYNC_COMPLETED,             /* SNTP同步完成 */
    SNTP_SYNC_FAILED,                /* SNTP同步失败 */
} wifi_event_id_t;

/* 事件基定义 - 用于WiFi和其他模块之间的通信 */
ESP_EVENT_DECLARE_BASE(WIFI_EVENTS);

void wifi_init_sta(void);

#endif