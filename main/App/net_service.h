/*
 * net_service.h
 * 网络服务 - Wi-Fi STA：扫描、连接、状态
 *
 * 凭据来源（按优先级）：
 *  1. 在界面上连接成功的网络，由 ESP-IDF 存进 NVS，下次开机自动连接
 *  2. NVS 里没有凭据时，使用 wifi_secrets.h 里的名称和密码（可选，不提交到 git，
 *     复制 wifi_secrets.h.example 为 wifi_secrets.h 后填写）
 *  两者都没有时只启动 Wi-Fi，等用户在界面上选择网络。
 *
 * 扫描和连接都是异步的：调用立即返回，界面在 LVGL 定时器里读取 net_service_scan_get() /
 * net_service_get_status()。连接状态也可以订阅 ESP-IDF 自带的 WIFI_EVENT / IP_EVENT
 * （如 IP_EVENT_STA_GOT_IP）。
 * 本模块的函数可以在任意任务里调用。
 */

#ifndef _NET_SERVICE_H_
#define _NET_SERVICE_H_

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NET_SSID_MAX_LEN        32
#define NET_PASSWORD_MIN_LEN    8       /* WPA2/WPA3 口令长度 8~63 */
#define NET_PASSWORD_MAX_LEN    63
#define NET_SCAN_MAX_APS        24

/* ==================== 扫描 ==================== */

typedef enum {
    NET_AUTH_OPEN,          /* 开放网络，不需要密码 */
    NET_AUTH_PSK,           /* WPA2 / WPA3 个人版，需要密码 */
    NET_AUTH_UNSUPPORTED,   /* WEP、WPA、企业版等，本服务不支持连接 */
} net_auth_t;

typedef struct {
    char ssid[NET_SSID_MAX_LEN + 1];
    int8_t rssi;            /* dBm，越大信号越强 */
    net_auth_t auth;
} net_ap_t;

typedef enum {
    NET_SCAN_IDLE,          /* 还没扫描过 */
    NET_SCAN_RUNNING,
    NET_SCAN_DONE,
    NET_SCAN_FAILED,
} net_scan_state_t;

/* ==================== 连接 ==================== */

typedef enum {
    NET_STATE_DISCONNECTED,
    NET_STATE_CONNECTING,   /* 包括开机自动连接、掉线后的重连 */
    NET_STATE_CONNECTED,    /* 已拿到 IP */
    NET_STATE_FAILED,       /* 重试用完仍未连上，原因见 fail */
} net_state_t;

typedef enum {
    NET_FAIL_NONE,
    NET_FAIL_WRONG_PASSWORD,
    NET_FAIL_NOT_FOUND,
    NET_FAIL_OTHER,
} net_fail_t;

typedef struct {
    net_state_t state;
    net_fail_t fail;                    /* 仅 state == NET_STATE_FAILED 时有意义 */
    char ssid[NET_SSID_MAX_LEN + 1];    /* 正在连接或已连接的网络，没有时为空 */
} net_status_t;

/**
 * @brief 初始化并启动 Wi-Fi（STA 模式），有凭据时自动开始连接
 * @note 不阻塞，连接结果用 net_service_get_status() 查询。需在 app_event_init() 和 NVS 初始化之后调用
 */
void net_service_start(void);

/**
 * @brief 开始扫描附近的 Wi-Fi（异步）
 * @return ESP_OK 已开始（或正在扫描）；ESP_ERR_INVALID_STATE 服务未启动或正在连接，
 *         连接过程中扫描会打断连接，所以拒绝；ESP_ERR_NO_MEM 内部 RAM 太少，扫描会让驱动分配失败而重启
 */
esp_err_t net_service_scan_start(void);

/**
 * @brief 读取最近一次扫描的结果
 * @param aps   接收结果的数组，可为 NULL；按信号从强到弱排序，同名网络只保留最强的，隐藏网络不包含
 * @param max   aps 的容量
 * @param count 实际拷贝的个数，可为 NULL
 * @return 扫描状态，只有 NET_SCAN_DONE 时结果才有效
 */
net_scan_state_t net_service_scan_get(net_ap_t *aps, size_t max, size_t *count);

/**
 * @brief 连接指定网络（异步），成功后会保存凭据，下次开机自动连接
 * @param ssid     网络名称，1~32 字节
 * @param password 密码，8~63 字节；开放网络传 NULL 或空串
 * @return ESP_OK 已开始连接；ESP_ERR_INVALID_ARG 参数不合法；ESP_ERR_INVALID_STATE 服务未启动
 * @note 会先断开当前网络。连接失败时不保存凭据，开机仍连回原来的网络
 */
esp_err_t net_service_connect(const char *ssid, const char *password);

/**
 * @brief 断开当前连接，或取消正在进行的连接
 * @note 只影响本次运行；已保存的凭据还在，重启后会再次自动连接
 */
void net_service_disconnect(void);

/**
 * @brief 读取当前连接状态
 */
void net_service_get_status(net_status_t *out);

#endif /* _NET_SERVICE_H_ */
