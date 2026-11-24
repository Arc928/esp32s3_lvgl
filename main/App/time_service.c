/*
 * time_service.c
 * 时间服务 - 设置时区，拿到 IP 后通过 esp_netif_sntp 异步对时
 *
 * 首次拿到 IP 时初始化一次 esp_netif_sntp（此时 esp_netif / lwIP tcpip 线程已就绪）；
 * 之后重连拿到新 IP 时调用 esp_netif_sntp_start() 触发立即重同步，不再反复创建临时任务
 * 或 init/deinit SNTP。对时超时通过单次 esp_timer 上报 APP_TIME_SYNC_FAILED，lwIP 底层
 * 仍会按退避策略继续在后台校时。
 */

#include "time_service.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "app_event.h"

#define TAG                     "time_service"
#define SNTP_SERVER_HOST        "ntp.aliyun.com"
#define SNTP_SYNC_TIMEOUT_US    (30LL * 1000 * 1000)
#define TIME_VALID_MIN_YEAR     (2016 - 1900)

static bool s_started = false;
static bool s_sntp_inited = false;              /* 仅在默认事件循环任务里读写 */
static atomic_bool s_sync_in_progress = false;
static esp_timer_handle_t s_timeout_timer = NULL;

/* 运行在 lwIP tcpip 线程 */
static void time_sync_notification_cb(struct timeval *tv)
{
    if (s_timeout_timer != NULL) {
        (void)esp_timer_stop(s_timeout_timer);
    }

    atomic_store_explicit(&s_sync_in_progress, false, memory_order_release);

    time_t now = (tv != NULL) ? tv->tv_sec : time(NULL);
    struct tm timeinfo;
    char strftime_buf[64];
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Synchronized date/time in Shanghai: %s", strftime_buf);

    app_event_publish_time_synced();
}

/* 运行在 esp_timer 任务 */
static void on_sync_timeout(void *arg)
{
    (void)arg;

    if (atomic_exchange_explicit(&s_sync_in_progress, false, memory_order_acq_rel)) {
        ESP_LOGW(TAG, "SNTP sync timed out after 30 s (continuing retries in background)");
        app_event_publish_time_sync_failed();
    }
}

/* 运行在默认事件循环任务（串行执行） */
static void sntp_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    /* 先武装状态与超时定时器，再启动 SNTP，避免极快响应时回调先于置位执行 */
    (void)esp_timer_stop(s_timeout_timer);
    atomic_store_explicit(&s_sync_in_progress, true, memory_order_release);
    esp_err_t timer_err = esp_timer_start_once(s_timeout_timer, SNTP_SYNC_TIMEOUT_US);
    if (timer_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start SNTP timeout timer: %s", esp_err_to_name(timer_err));
    }

    esp_err_t err;
    if (!s_sntp_inited) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER_HOST);
        config.wait_for_sync = false;
        config.start = true;
        config.sync_cb = time_sync_notification_cb;

        err = esp_netif_sntp_init(&config);
        if (err != ESP_OK) {
            (void)esp_timer_stop(s_timeout_timer);
            atomic_store_explicit(&s_sync_in_progress, false, memory_order_release);
            ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
            app_event_publish_time_sync_failed();
            return;
        }
        s_sntp_inited = true;
        ESP_LOGI(TAG, "Got IP, SNTP started (server: %s)", SNTP_SERVER_HOST);
    } else {
        err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            (void)esp_timer_stop(s_timeout_timer);
            atomic_store_explicit(&s_sync_in_progress, false, memory_order_release);
            ESP_LOGW(TAG, "esp_netif_sntp_start failed: %s", esp_err_to_name(err));
            app_event_publish_time_sync_failed();
            return;
        }
        ESP_LOGI(TAG, "Got IP, SNTP re-sync triggered");
    }

    app_event_publish_time_sync_started();
}

esp_err_t time_service_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    /* 先设置时区，后续 localtime_r 均按东八区格式化 */
    setenv("TZ", "CST-8", 1);
    tzset();

    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= TIME_VALID_MIN_YEAR) {
        ESP_LOGI(TAG, "Time is already set");
    } else {
        ESP_LOGI(TAG, "Time is not set yet. Will sync when Wi-Fi connects.");
    }

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current date/time in Shanghai: %s", strftime_buf);

    const esp_timer_create_args_t timer_args = {
        .callback = on_sync_timeout,
        .name = "sntp_timeout",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_timeout_timer);
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to create SNTP timeout timer");

    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &sntp_event_handler,
                                              NULL,
                                              NULL);
    if (ret != ESP_OK) {
        (void)esp_timer_delete(s_timeout_timer);
        s_timeout_timer = NULL;
        ESP_LOGE(TAG, "failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    s_started = true;
    return ESP_OK;
}
