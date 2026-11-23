/*
 * mqtt_service.c
 * MQTT 服务实现 - 订阅大棚下行控制主题，校验并解析 device_control 命令
 */

#include "mqtt_service.h"

#if __has_include("mqtt_secrets.h")
#include "mqtt_secrets.h"
#else
#error "缺少 mqtt_secrets.h：请复制 mqtt_secrets.h.example 为 mqtt_secrets.h 并填写配置"
#endif

#if !defined(MQTT_GHCODE) || !defined(MQTT_GHCHECK)
#error "mqtt_secrets.h 缺少 MQTT_GHCODE 或 MQTT_GHCHECK，请参考 mqtt_secrets.h.example 补齐"
#endif

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "app_event.h"

#define TAG "MQTT_SERVICE"

#define MQTT_CONTROL_TOPIC_PREFIX   "greenhouse/machine/control/"
#define MQTT_CONTROL_SUB_QOS        1
#define MQTT_KEEPALIVE_S            30
#define MQTT_RX_MAX_LEN             1024    /* 单条下行 JSON 最大字节数（含结尾 '\0'） */
#define MQTT_DEVICE_COUNT           (APP_CONTROL_DEVICE_MAX - APP_CONTROL_DEVICE_MIN + 1)

static esp_mqtt_client_handle_t s_client = NULL;
static SemaphoreHandle_t s_lock = NULL;
static volatile bool s_connected = false;   /* 只在 esp-mqtt 任务里写 */
static bool s_started = false;              /* 只在默认事件循环任务里读写 */

static char s_client_id[24];                /* MQTT 3.1.1 要求 broker 至少支持 23 个字符 */
static char s_topic_control[96];

/* 分片接收缓冲：只在 esp-mqtt 任务里访问，不需要加锁 */
static char s_rx_buf[MQTT_RX_MAX_LEN];
static size_t s_rx_total = 0;
static size_t s_rx_received = 0;
static bool s_rx_active = false;

/* 按设备号 1~9 及全局缓存最新一条已校验通过的控制命令，由 s_lock 保护 */
static app_device_control_t s_latest_cmds[MQTT_DEVICE_COUNT];
static bool s_has_cmd[MQTT_DEVICE_COUNT];
static app_device_control_t s_last_cmd;
static bool s_has_last_cmd = false;
static uint32_t s_cmd_seq = 0;

static void reset_rx(void)
{
    s_rx_active = false;
    s_rx_total = 0;
    s_rx_received = 0;
}

static bool parse_direction_action(const char *str, app_control_action_t *out)
{
    if (strcmp(str, "forward") == 0) {
        *out = APP_CTRL_ACTION_FORWARD;
        return true;
    }
    if (strcmp(str, "backward") == 0) {
        *out = APP_CTRL_ACTION_BACKWARD;
        return true;
    }
    if (strcmp(str, "left") == 0) {
        *out = APP_CTRL_ACTION_LEFT;
        return true;
    }
    if (strcmp(str, "right") == 0) {
        *out = APP_CTRL_ACTION_RIGHT;
        return true;
    }
    return false;
}

static bool parse_action(const char *str, app_control_action_t *out)
{
    if (strcmp(str, "start") == 0) {
        *out = APP_CTRL_ACTION_START;
        return true;
    }
    if (strcmp(str, "stop") == 0) {
        *out = APP_CTRL_ACTION_STOP;
        return true;
    }
    if (strcmp(str, "reset") == 0) {
        *out = APP_CTRL_ACTION_RESET;
        return true;
    }
    if (strcmp(str, "maintenance") == 0) {
        *out = APP_CTRL_ACTION_MAINTENANCE;
        return true;
    }
    if (strcmp(str, "other") == 0) {
        *out = APP_CTRL_ACTION_OTHER;
        return true;
    }
    /* 兼容直接通过 action 字段下发方向指令的情况 */
    return parse_direction_action(str, out);
}

static bool parse_device_control(const char *payload, size_t len, app_device_control_t *out)
{
    bool ok = false;
    cJSON *root = cJSON_ParseWithLength(payload, len);

    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "drop message: invalid JSON");
        goto cleanup;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        ESP_LOGW(TAG, "drop message: missing 'type'");
        goto cleanup;
    }
    if (strcmp(type->valuestring, "device_control") != 0) {
        ESP_LOGW(TAG, "drop message: unsupported type \"%s\"", type->valuestring);
        goto cleanup;
    }

    const cJSON *ghcode = cJSON_GetObjectItemCaseSensitive(root, "ghcode");
    if (!cJSON_IsString(ghcode) || ghcode->valuestring == NULL ||
        strcmp(ghcode->valuestring, MQTT_GHCODE) != 0) {
        ESP_LOGW(TAG, "drop message: ghcode mismatch (got \"%s\")",
                 (cJSON_IsString(ghcode) && ghcode->valuestring != NULL) ? ghcode->valuestring : "");
        goto cleanup;
    }

    const cJSON *ghcheck = cJSON_GetObjectItemCaseSensitive(root, "ghcheck");
    if (!cJSON_IsString(ghcheck) || ghcheck->valuestring == NULL ||
        strcmp(ghcheck->valuestring, MQTT_GHCHECK) != 0) {
        ESP_LOGW(TAG, "drop message: ghcheck mismatch");
        goto cleanup;
    }

    memset(out, 0, sizeof(*out));

    /* timestamp 为毫秒级时间戳（超过 32 位，需用 valuedouble 读取） */
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    if (ts != NULL && !cJSON_IsNull(ts)) {
        if (!cJSON_IsNumber(ts) || !(ts->valuedouble >= 0.0 && ts->valuedouble <= 9007199254740991.0)) {
            ESP_LOGW(TAG, "drop message: invalid 'timestamp'");
            goto cleanup;
        }
        out->timestamp_ms = (int64_t)ts->valuedouble;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data)) {
        ESP_LOGW(TAG, "drop message: missing or invalid 'data' object");
        goto cleanup;
    }

    const cJSON *dev = cJSON_GetObjectItemCaseSensitive(data, "deviceId");
    if (!cJSON_IsString(dev) || dev->valuestring == NULL ||
        dev->valuestring[0] < ('0' + APP_CONTROL_DEVICE_MIN) ||
        dev->valuestring[0] > ('0' + APP_CONTROL_DEVICE_MAX) ||
        dev->valuestring[1] != '\0') {
        ESP_LOGW(TAG, "drop message: invalid 'deviceId' (expected \"1\"..\"9\")");
        goto cleanup;
    }
    out->device_id = (uint8_t)(dev->valuestring[0] - '0');

    const cJSON *action = cJSON_GetObjectItemCaseSensitive(data, "action");
    if (!cJSON_IsString(action) || action->valuestring == NULL ||
        !parse_action(action->valuestring, &out->action)) {
        ESP_LOGW(TAG, "drop message: invalid 'action' (\"%s\")",
                 (cJSON_IsString(action) && action->valuestring != NULL) ? action->valuestring : "");
        goto cleanup;
    }

    const cJSON *other = cJSON_GetObjectItemCaseSensitive(data, "other_cmd_info");
    if (other != NULL && !cJSON_IsNull(other)) {
        if (!cJSON_IsString(other) || other->valuestring == NULL) {
            ESP_LOGW(TAG, "drop message: invalid 'other_cmd_info'");
            goto cleanup;
        }
        size_t copied = strlcpy(out->other_cmd_info, other->valuestring, sizeof(out->other_cmd_info));
        if (copied >= sizeof(out->other_cmd_info)) {
            ESP_LOGW(TAG, "'other_cmd_info' truncated to %u bytes",
                     (unsigned)(sizeof(out->other_cmd_info) - 1));
        }
        /* 大屏方向指令复用 action="other" + other_cmd_info="forward"/"backward"/"left"/"right" */
        if (out->action == APP_CTRL_ACTION_OTHER) {
            (void)parse_direction_action(other->valuestring, &out->action);
        }
    }

    cJSON *params = cJSON_GetObjectItemCaseSensitive(data, "parameters");
    if (params == NULL || cJSON_IsNull(params)) {
        strlcpy(out->parameters, "{}", sizeof(out->parameters));
    } else if (!cJSON_IsObject(params)) {
        ESP_LOGW(TAG, "drop message: invalid 'parameters' (expected JSON object)");
        goto cleanup;
    } else if (!cJSON_PrintPreallocated(params, out->parameters, (int)sizeof(out->parameters), false)) {
        ESP_LOGW(TAG, "'parameters' exceeds %u bytes, dropped", (unsigned)sizeof(out->parameters));
        out->parameters[0] = '\0';
    }

    out->received_us = esp_timer_get_time();
    ok = true;

cleanup:
    cJSON_Delete(root);
    return ok;
}

static void handle_control_payload(const char *payload, size_t len)
{
    app_device_control_t cmd;

    if (!parse_device_control(payload, len, &cmd)) {
        return;
    }

    size_t idx = (size_t)(cmd.device_id - APP_CONTROL_DEVICE_MIN);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cmd_seq++;
    if (s_cmd_seq == 0U) {
        s_cmd_seq = 1U;
    }
    cmd.seq = s_cmd_seq;
    s_latest_cmds[idx] = cmd;
    s_has_cmd[idx] = true;
    s_last_cmd = cmd;
    s_has_last_cmd = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "control cmd #%lu: device=%u action=%s ts=%lld other=\"%s\" params=%s",
             (unsigned long)cmd.seq, (unsigned)cmd.device_id, app_control_action_to_str(cmd.action),
             (long long)cmd.timestamp_ms, cmd.other_cmd_info, cmd.parameters);

    app_event_publish_device_control(&cmd);
}

static void on_mqtt_data(const esp_mqtt_event_t *event)
{
    if (event->current_data_offset < 0 || event->data_len < 0 || event->total_data_len <= 0) {
        reset_rx();
        return;
    }

    size_t offset = (size_t)event->current_data_offset;
    size_t chunk_len = (size_t)event->data_len;
    size_t total_len = (size_t)event->total_data_len;

    if (offset == 0) {
        reset_rx();

        size_t expected_len = strlen(s_topic_control);
        if (event->topic == NULL || (size_t)event->topic_len != expected_len ||
            memcmp(event->topic, s_topic_control, expected_len) != 0) {
            ESP_LOGW(TAG, "ignore message on unexpected topic \"%.*s\"",
                     event->topic_len, event->topic != NULL ? event->topic : "");
            return;
        }
        if (total_len >= sizeof(s_rx_buf)) {
            ESP_LOGW(TAG, "drop oversized message (%u bytes, max %u)",
                     (unsigned)total_len, (unsigned)(sizeof(s_rx_buf) - 1));
            return;
        }
        if (chunk_len > total_len) {
            return;
        }

        s_rx_total = total_len;
        s_rx_active = true;
    } else if (!s_rx_active) {
        return;
    }

    if (offset != s_rx_received || offset + chunk_len > s_rx_total) {
        ESP_LOGW(TAG, "drop malformed chunk (offset=%u len=%u expected=%u total=%u)",
                 (unsigned)offset, (unsigned)chunk_len, (unsigned)s_rx_received, (unsigned)s_rx_total);
        reset_rx();
        return;
    }

    if (chunk_len > 0) {
        if (event->data == NULL) {
            reset_rx();
            return;
        }
        memcpy(s_rx_buf + offset, event->data, chunk_len);
        s_rx_received += chunk_len;
    }

    if (s_rx_received == s_rx_total) {
        size_t msg_len = s_rx_total;
        s_rx_buf[msg_len] = '\0';
        reset_rx();
        handle_control_payload(s_rx_buf, msg_len);
    }
}

static void log_error(const esp_mqtt_error_codes_t *err)
{
    if (err->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        ESP_LOGW(TAG, "transport error: esp-tls %s, tls stack 0x%x, socket errno %d",
                 esp_err_to_name(err->esp_tls_last_esp_err), err->esp_tls_stack_err,
                 err->esp_transport_sock_errno);
    } else if (err->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ESP_LOGW(TAG, "broker refused connection, code 0x%x (check username/password in mqtt_secrets.h)",
                 err->connect_return_code);
    }
}

/* 在 esp-mqtt 自己的任务里运行 */
static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        reset_rx();
        int msg_id = esp_mqtt_client_subscribe(s_client, s_topic_control, MQTT_CONTROL_SUB_QOS);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "failed to subscribe to %s", s_topic_control);
        } else {
            ESP_LOGI(TAG, "subscribing to %s (msg_id=%d)", s_topic_control, msg_id);
        }
        app_event_publish_mqtt_status(true);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        reset_rx();
        /* 每次重连失败都会收到这个事件，只在状态变化时发事件，避免刷屏 */
        if (s_connected) {
            s_connected = false;
            app_event_publish_mqtt_status(false);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "subscribed to %s (msg_id=%d)", s_topic_control, event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        on_mqtt_data(event);
        break;

    case MQTT_EVENT_ERROR:
        log_error(event->error_handle);
        break;

    default:
        break;
    }
}

/* 只在第一次拿到 IP 时启动客户端；之后断网再连上由 esp-mqtt 自动重连 */
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_started) {
        return;
    }

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start client: %s", esp_err_to_name(err));
        return;
    }

    s_started = true;
    ESP_LOGI(TAG, "connecting to %s as %s", MQTT_BROKER_URI, s_client_id);
}

esp_err_t mqtt_service_start(void)
{
    if (s_client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (MQTT_GHCODE[0] == '\0' || MQTT_GHCHECK[0] == '\0') {
        ESP_LOGE(TAG, "MQTT_GHCODE and MQTT_GHCHECK must be non-empty in mqtt_secrets.h");
        return ESP_ERR_INVALID_ARG;
    }

    int n = snprintf(s_topic_control, sizeof(s_topic_control),
                     MQTT_CONTROL_TOPIC_PREFIX "%s", MQTT_GHCODE);
    if (n < 0 || (size_t)n >= sizeof(s_topic_control)) {
        ESP_LOGE(TAG, "control topic too long for ghcode \"%s\"", MQTT_GHCODE);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 用基础 MAC 做设备 ID：ESP32-P4 没有片上 Wi-Fi，读 ESP_MAC_WIFI_STA 会失败；
     * S3 和 P4 上基础 MAC 均可用且唯一 */
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_BASE), TAG, "failed to read MAC");
    snprintf(s_client_id, sizeof(s_client_id), "gh-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        /* mqtts:// 和 wss:// 用根证书包校验服务器，mqtt:// 和 ws:// 会忽略这一项 */
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = s_client_id,
        .credentials.username = MQTT_USERNAME[0] ? MQTT_USERNAME : NULL,
        .credentials.authentication.password = MQTT_PASSWORD[0] ? MQTT_PASSWORD : NULL,
        .session.keepalive = MQTT_KEEPALIVE_S,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        ESP_LOGE(TAG, "failed to create client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register MQTT events: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_got_ip, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to subscribe IP event: %s", esp_err_to_name(err));
        goto fail;
    }

    ESP_LOGI(TAG, "control topic -> %s (ghcode=%s)", s_topic_control, MQTT_GHCODE);
    return ESP_OK;

fail:
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    return err;
}

bool mqtt_service_is_connected(void)
{
    return s_connected;
}

bool mqtt_service_get_last_control(app_device_control_t *out)
{
    if (out == NULL || s_lock == NULL) {
        return false;
    }

    bool has = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_has_last_cmd) {
        *out = s_last_cmd;
        has = true;
    }
    xSemaphoreGive(s_lock);

    return has;
}

bool mqtt_service_get_latest_control(uint8_t device_id, app_device_control_t *out)
{
    if (out == NULL || s_lock == NULL ||
        device_id < APP_CONTROL_DEVICE_MIN || device_id > APP_CONTROL_DEVICE_MAX) {
        return false;
    }

    size_t idx = (size_t)(device_id - APP_CONTROL_DEVICE_MIN);
    bool has = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_has_cmd[idx]) {
        *out = s_latest_cmds[idx];
        has = true;
    }
    xSemaphoreGive(s_lock);

    return has;
}
