#include "net_service.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "net_service"

#define BOOT_MAX_RETRY      5   /* 开机自动连接、掉线重连：断开后最多再重试几次 */
#define USER_MAX_RETRY      2   /* 用户在界面上连接：密码错误时尽快给出结果 */
#define SCAN_RECORDS        32  /* 一次最多取多少条扫描记录（放在堆上，事件任务栈很小） */
/* 扫描时驱动要在内部 RAM 里存结果、收包，射频唤醒也要分配；内部 RAM 低于这个值就不扫描，
 * 否则 esp_phy_enable 里 esp_timer_create 会因 ESP_ERR_NO_MEM 直接 abort（整机重启） */
#define SCAN_MIN_FREE_INTERNAL  (12 * 1024)

/* 状态由三方访问：Wi-Fi/IP 事件任务、调用 API 的任务（LVGL）、持久化任务，全部用 s_lock 保护。
 * 持锁期间不调用 esp_wifi_*，避免和 Wi-Fi 任务互相等待。 */
static SemaphoreHandle_t s_lock;
static volatile bool s_started;

static net_status_t s_status;
static int s_retry_num;
static int s_max_retry;

/* 用户新输入的凭据：连上之后才写进 NVS，连不上就丢掉，开机仍连回原来的网络 */
static wifi_config_t s_pending_cfg;
static bool s_persist_pending;

static net_scan_state_t s_scan_state;
static net_ap_t s_aps[NET_SCAN_MAX_APS];
static size_t s_ap_count;

static inline void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static inline void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void copy_ssid(char *dst, const uint8_t *src)
{
    /* wifi_config_t 里的 ssid 刚好 32 字节时没有结尾的 '\0' */
    memcpy(dst, src, NET_SSID_MAX_LEN);
    dst[NET_SSID_MAX_LEN] = '\0';
}

/* ==================== 连接 ==================== */

static net_fail_t classify_reason(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return NET_FAIL_WRONG_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return NET_FAIL_NOT_FOUND;
    default:
        return NET_FAIL_OTHER;
    }
}

/* 事件任务的栈只有 2KB，写 NVS 放到单独的短任务里（和 time_service 的做法一样） */
static void persist_task(void *arg)
{
    wifi_config_t *cfg = arg;
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, cfg);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved credentials of \"%s\"", (const char *)cfg->sta.ssid);
    } else {
        ESP_LOGW(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
    }
    memset(cfg, 0, sizeof(*cfg));
    free(cfg);
    vTaskDelete(NULL);
}

static void persist_credentials(const wifi_config_t *cfg)
{
    wifi_config_t *copy = malloc(sizeof(*copy));

    if (copy == NULL) {
        return;
    }
    *copy = *cfg;
    if (xTaskCreate(persist_task, "wifi_save", 4096, copy, 4, NULL) != pdPASS) {
        memset(copy, 0, sizeof(*copy));
        free(copy);
    }
}

static void on_sta_start(void)
{
    bool connect;

    lock();
    connect = (s_status.state == NET_STATE_CONNECTING);
    unlock();
    if (connect) {
        esp_wifi_connect();
    }
}

static void on_sta_disconnected(const wifi_event_sta_disconnected_t *ev)
{
    bool retry = false;

    lock();
    if (ev->reason == WIFI_REASON_ASSOC_LEAVE && s_status.state != NET_STATE_CONNECTED) {
        /* 我们自己调用 esp_wifi_disconnect() 产生的事件 */
        unlock();
        return;
    }

    switch (s_status.state) {
    case NET_STATE_CONNECTING:
        if (s_retry_num < s_max_retry) {
            s_retry_num++;
            retry = true;
        } else {
            s_status.state = NET_STATE_FAILED;
            s_status.fail = classify_reason(ev->reason);
            s_persist_pending = false;
            memset(&s_pending_cfg, 0, sizeof(s_pending_cfg));
        }
        break;
    case NET_STATE_CONNECTED:
        /* 掉线：按开机的重试次数重连 */
        s_status.state = NET_STATE_CONNECTING;
        s_status.fail = NET_FAIL_NONE;
        s_retry_num = 1;
        s_max_retry = BOOT_MAX_RETRY;
        retry = true;
        break;
    default:
        /* 用户已经取消或已经失败，不再重连 */
        break;
    }
    unlock();

    ESP_LOGI(TAG, "Disconnected (reason %u)%s", (unsigned)ev->reason, retry ? ", retrying" : "");
    if (retry) {
        esp_wifi_connect();
    }
}

static void on_got_ip(const ip_event_got_ip_t *ev)
{
    wifi_config_t to_save = { 0 };
    bool save;

    lock();
    s_status.state = NET_STATE_CONNECTED;
    s_status.fail = NET_FAIL_NONE;
    s_retry_num = 0;
    save = s_persist_pending;
    if (save) {
        to_save = s_pending_cfg;
        s_persist_pending = false;
        memset(&s_pending_cfg, 0, sizeof(s_pending_cfg));
    }
    unlock();

    ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&ev->ip_info.ip));
    if (save) {
        persist_credentials(&to_save);
        memset(&to_save, 0, sizeof(to_save));
    }
}

/* ==================== 扫描 ==================== */

static net_auth_t map_auth(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return NET_AUTH_OPEN;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return NET_AUTH_PSK;
    default:
        return NET_AUTH_UNSUPPORTED;
    }
}

/* 同名网络（多个路由器、2.4G/5G 同名）只保留信号最强的；列表满了就替换掉最弱的 */
static void add_ap(const wifi_ap_record_t *rec)
{
    net_ap_t *slot = NULL;
    size_t i;

    if (rec->ssid[0] == '\0') {
        return;     /* 隐藏网络 */
    }
    for (i = 0; i < s_ap_count; i++) {
        if (strcmp(s_aps[i].ssid, (const char *)rec->ssid) == 0) {
            if (rec->rssi > s_aps[i].rssi) {
                slot = &s_aps[i];
            } else {
                return;
            }
            break;
        }
    }
    if (slot == NULL) {
        if (s_ap_count < NET_SCAN_MAX_APS) {
            slot = &s_aps[s_ap_count++];
        } else {
            slot = &s_aps[0];
            for (i = 1; i < s_ap_count; i++) {
                if (s_aps[i].rssi < slot->rssi) {
                    slot = &s_aps[i];
                }
            }
            if (rec->rssi <= slot->rssi) {
                return;
            }
        }
    }
    strlcpy(slot->ssid, (const char *)rec->ssid, sizeof(slot->ssid));
    slot->rssi = rec->rssi;
    slot->auth = map_auth(rec->authmode);
}

static void sort_aps_by_rssi(void)
{
    for (size_t i = 1; i < s_ap_count; i++) {
        net_ap_t key = s_aps[i];
        size_t j = i;

        while (j > 0 && s_aps[j - 1].rssi < key.rssi) {
            s_aps[j] = s_aps[j - 1];
            j--;
        }
        s_aps[j] = key;
    }
}

static void on_scan_done(const wifi_event_sta_scan_done_t *ev)
{
    wifi_ap_record_t *recs = NULL;
    uint16_t num = 0;
    size_t found;
    bool ok = (ev->status == 0);
    bool running;

    lock();
    running = (s_scan_state == NET_SCAN_RUNNING);
    unlock();
    if (!running) {
        return;
    }

    if (ok) {
        esp_wifi_scan_get_ap_num(&num);
        if (num > SCAN_RECORDS) {
            num = SCAN_RECORDS;
        }
        if (num > 0) {
            /* 临时数组放 PSRAM，内部 RAM 留给 Wi-Fi 驱动 */
            recs = heap_caps_malloc(num * sizeof(*recs), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (recs == NULL) {
                recs = malloc(num * sizeof(*recs));
            }
        }
        if (recs != NULL && esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
            num = 0;
            ok = false;
        }
        if (recs == NULL) {
            /* 没有记录或内存不足：也要清掉驱动里的列表，否则内存不会释放 */
            esp_wifi_clear_ap_list();
            if (num > 0) {
                num = 0;
                ok = false;
            }
        }
    }

    lock();
    s_ap_count = 0;
    for (uint16_t i = 0; i < num; i++) {
        add_ap(&recs[i]);
    }
    sort_aps_by_rssi();
    s_scan_state = ok ? NET_SCAN_DONE : NET_SCAN_FAILED;
    found = s_ap_count;
    unlock();

    free(recs);
    ESP_LOGI(TAG, "Scan %s, %u networks, internal free %u KB", ok ? "done" : "failed", (unsigned)found,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
}

/* ==================== 事件入口 ==================== */

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            on_sta_start();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            on_sta_disconnected(event_data);
            break;
        case WIFI_EVENT_SCAN_DONE:
            on_scan_done(event_data);
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        on_got_ip(event_data);
    }
}

/* ==================== 开机凭据 ==================== */

/* 优先用 NVS 里保存的（界面上连过的），没有再用 wifi_secrets.h */
static bool load_boot_credentials(wifi_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    if (esp_wifi_get_config(WIFI_IF_STA, cfg) == ESP_OK && cfg->sta.ssid[0] != '\0') {
        return true;
    }

#ifdef WIFI_STA_SSID
    memset(cfg, 0, sizeof(*cfg));
    strlcpy((char *)cfg->sta.ssid, WIFI_STA_SSID, sizeof(cfg->sta.ssid));
    strlcpy((char *)cfg->sta.password, WIFI_STA_PASSWORD, sizeof(cfg->sta.password));
    /* 低于 WPA2 的网络（含开放网络）一律不连，和之前的行为一致 */
    cfg->sta.threshold.authmode = (cfg->sta.password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, cfg));
    return true;
#else
    return false;
#endif
}

/* ==================== 公开接口 ==================== */

void net_service_start(void)
{
    wifi_config_t boot_cfg;

    if (s_started) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (load_boot_credentials(&boot_cfg)) {
        /* 状态要在 esp_wifi_start() 之前设好：STA_START 事件里据此决定是否开始连接 */
        s_status.state = NET_STATE_CONNECTING;
        copy_ssid(s_status.ssid, boot_cfg.sta.ssid);
        s_max_retry = BOOT_MAX_RETRY;
        ESP_LOGI(TAG, "Connecting to \"%s\"", s_status.ssid);
    } else {
        ESP_LOGI(TAG, "No saved Wi-Fi, waiting for the user to pick one");
    }
    memset(&boot_cfg, 0, sizeof(boot_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());
    s_started = true;
    ESP_LOGI(TAG, "Wi-Fi STA started");
}

esp_err_t net_service_scan_start(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Scan start, internal free %u KB (largest block %u KB)", (unsigned)(free_internal / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
    if (free_internal < SCAN_MIN_FREE_INTERNAL) {
        return ESP_ERR_NO_MEM;
    }

    lock();
    if (s_scan_state == NET_SCAN_RUNNING) {
        unlock();
        return ESP_OK;
    }
    if (s_status.state == NET_STATE_CONNECTING) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_state = NET_SCAN_RUNNING;
    unlock();

    /* 全部使用默认参数：全信道主动扫描，不含隐藏网络 */
    wifi_scan_config_t scan_cfg = { 0 };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        lock();
        s_scan_state = NET_SCAN_FAILED;
        unlock();
    }
    return err;
}

net_scan_state_t net_service_scan_get(net_ap_t *aps, size_t max, size_t *count)
{
    net_scan_state_t state;
    size_t n = 0;

    if (!s_started) {
        if (count != NULL) {
            *count = 0;
        }
        return NET_SCAN_IDLE;
    }

    lock();
    state = s_scan_state;
    if (state == NET_SCAN_DONE) {
        n = (s_ap_count < max) ? s_ap_count : max;
        if (aps != NULL) {
            memcpy(aps, s_aps, n * sizeof(*aps));
        }
    }
    unlock();

    if (count != NULL) {
        *count = n;
    }
    return state;
}

esp_err_t net_service_connect(const char *ssid, const char *password)
{
    size_t ssid_len = (ssid != NULL) ? strlen(ssid) : 0;
    size_t pass_len = (password != NULL) ? strlen(password) : 0;

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid_len == 0 || ssid_len > NET_SSID_MAX_LEN ||
        pass_len > NET_PASSWORD_MAX_LEN || (pass_len > 0 && pass_len < NET_PASSWORD_MIN_LEN)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    if (pass_len > 0) {
        memcpy(cfg.sta.password, password, pass_len);
    }
    /* 有密码的网络必须至少是 WPA2，防止被同名的开放热点骗过去 */
    cfg.sta.threshold.authmode = (pass_len > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = NET_STATE_CONNECTING;
    memcpy(s_status.ssid, ssid, ssid_len);
    s_retry_num = 0;
    s_max_retry = USER_MAX_RETRY;
    s_pending_cfg = cfg;
    s_persist_pending = true;
    unlock();

    ESP_LOGI(TAG, "Connecting to \"%s\"", ssid);

    /* 当前网络可能没连上，disconnect 报错可以忽略 */
    esp_wifi_disconnect();
    /* 先只改内存里的配置；连上之后再写 NVS（见 on_got_ip） */
    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    memset(&cfg, 0, sizeof(cfg));

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Connect failed to start: %s", esp_err_to_name(err));
        lock();
        s_status.state = NET_STATE_FAILED;
        s_status.fail = NET_FAIL_OTHER;
        s_persist_pending = false;
        memset(&s_pending_cfg, 0, sizeof(s_pending_cfg));
        unlock();
    }
    return err;
}

void net_service_disconnect(void)
{
    if (!s_started) {
        return;
    }
    lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = NET_STATE_DISCONNECTED;
    s_persist_pending = false;
    memset(&s_pending_cfg, 0, sizeof(s_pending_cfg));
    unlock();

    esp_wifi_disconnect();
}

void net_service_get_status(net_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        out->state = NET_STATE_DISCONNECTED;
        return;
    }
    lock();
    *out = s_status;
    unlock();
}
