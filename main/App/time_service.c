#include "time_service.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "app_event.h"

static const char *TAG = "time_service";

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

// 同步状态标志
static bool sntp_sync_in_progress = false;
static bool time_is_set = false;

// 前向声明
static void time_sync_notification_cb(struct timeval *tv);
static void sntp_time_sync(void);
static void sntp_sync_task(void *pvParameters);
static void sntp_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
static void print_servers(void);
static esp_err_t sntp_sync_with_retry(int timeout_ms);

// 网络事件处理函数 - 拿到 IP 后触发时间同步
// 直接订阅 ESP-IDF 的 IP_EVENT，Wi-Fi 驱动不需要知道 SNTP 的存在
static void sntp_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP, triggering time sync");
        // 如果没有正在进行的同步，触发时间同步
        if (!sntp_sync_in_progress) {
            // sntp_time_sync();    //不可以在这里直接调用这个函数，因为这个函数会阻塞
            // 创建任务
            xTaskCreate(
                sntp_sync_task,   // 1. 刚才写的任务函数名
                "sntp_sync_task", // 2. 任务的名字（调试用）
                4096,             // 3. 栈空间大小（SNTP/网络操作建议 4096）
                NULL,             // 4. 传递给任务的参数（这里没有，传 NULL）
                5,                // 5. 任务优先级（中等即可）
                NULL              // 6. 任务句柄（不需要控制它，传 NULL）
            );
        } else {
            ESP_LOGI(TAG, "SNTP sync already in progress, skipping");
        }
    }
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event notification");

    // 更新同步状态
    sntp_sync_in_progress = false;
    time_is_set = true;

    // 发送同步完成事件
    app_event_publish_time_synced();
}

static void print_servers(void)
{
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

// 可重入的时间同步函数 - 支持多次调用
static esp_err_t sntp_sync_with_retry(int timeout_ms)
{
    // 如果已经在同步中，返回错误
    if (sntp_sync_in_progress) {
        ESP_LOGW(TAG, "SNTP sync already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.sync_cb = time_sync_notification_cb;

    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_netif_sntp_init(&config);
    print_servers();

    // 发送同步开始事件
    app_event_publish_time_sync_started();

    sntp_sync_in_progress = true;

    // 等待时间同步
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retries = timeout_ms / 2000;  // 每2秒检查一次

    ESP_LOGI(TAG, "Waiting for time synchronization...");

    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT &&
           ++retry < max_retries) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, max_retries);
    }

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2016 - 1900)) {
        ESP_LOGI(TAG, "Time synchronized successfully");
        esp_netif_sntp_deinit();
        sntp_sync_in_progress = false;
        time_is_set = true;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Time synchronization failed after %d retries", max_retries);
        sntp_sync_in_progress = false;
        time_is_set = false;
        app_event_publish_time_sync_failed();
        return ESP_ERR_TIMEOUT;
    }
}

void time_service_start(void)
{
    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &sntp_event_handler,
        NULL,
        NULL));

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 检查时间是否已经设置
    if (timeinfo.tm_year >= (2016 - 1900)) {
        time_is_set = true;
        ESP_LOGI(TAG, "Time is already set");
    } else {
        time_is_set = false;
        ESP_LOGI(TAG, "Time is not set yet. Will sync when WiFi connects.");
    }

    // 设置时区
    setenv("TZ", "CST-8", 1);
    tzset();

    // 打印当前时间
    char strftime_buf[64];
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current date/time in Shanghai: %s", strftime_buf);
}

static void sntp_time_sync(void)
{
    // 如果时间已设置且不在同步中，这里可以选择是否重新同步
    // 根据你的需求，这里设置为每次WiFi连接都同步
    if (time_is_set && !sntp_sync_in_progress) {
        ESP_LOGI(TAG, "Re-syncing time (time was previously set)");
    }

    if (sntp_sync_in_progress) {
        ESP_LOGW(TAG, "SNTP sync already in progress, skipping");
        return;
    }

    // 执行时间同步（等待最多30秒）
    esp_err_t result = sntp_sync_with_retry(30000);

    if (result == ESP_OK) {
        // 打印同步后的时间
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Synchronized date/time in Shanghai: %s", strftime_buf);
    }
}

// 任务函数：专门负责后台同步
static void sntp_sync_task(void *pvParameters) 
{
    ESP_LOGI("SNTP_TASK", "同步任务启动...");
    
    sntp_time_sync(); 

    ESP_LOGI("SNTP_TASK", "同步任务完成，正在自毁...");
    
    // 任务执行完后必须删除自己，否则会内存泄漏或报错
    vTaskDelete(NULL); 
}