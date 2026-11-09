/*
 * mqtt_service.c
 * MQTT 服务实现
 */

#include "mqtt_service.h"

#if __has_include("mqtt_secrets.h")
#include "mqtt_secrets.h"
#else
#error "缺少 mqtt_secrets.h：请复制 mqtt_secrets.h.example 为 mqtt_secrets.h 并填写 broker 地址和账号"
#endif

#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "app_event.h"

#define TAG "MQTT_SERVICE"

#define MQTT_TOPIC_ROOT         "esp32s3_lvgl"
#define MQTT_PUBLISH_PERIOD_MS  1000    /* IMU 是 10Hz，上传降到 1Hz */
#define MQTT_KEEPALIVE_S        30      /* broker 在 1.5 倍 keepalive（约 45 秒）收不到心跳就发遗嘱 */
#define MQTT_TASK_STACK         4096
#define MQTT_TASK_PRIO          2       /* 低于 IMU（3）和 LVGL（6） */

/* 2016-01-01 00:00:00 UTC，早于这个值说明还没对时（和 time_service 的判断一致） */
#define TIME_VALID_AFTER        1451606400

static esp_mqtt_client_handle_t s_client = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_connected = false;   /* 只在 esp-mqtt 任务里写 */
static bool s_started = false;              /* 只在默认事件循环任务里读写 */

static char s_client_id[24];        /* MQTT 3.1.1 要求 broker 至少支持 23 个字符 */
static char s_topic_imu[48];
static char s_topic_status[48];

/* IMU 事件处理函数写，上传任务读；数据只有十几个字节，用自旋锁即可 */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_imu_data_t s_imu = {0};
static bool s_imu_fresh = false;

/* 在默认事件循环里运行，只拷贝数据，不做网络操作，避免卡住其他事件 */
static void on_imu_data(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_imu_data_t *imu = data;

    portENTER_CRITICAL(&s_lock);
    s_imu = *imu;
    s_imu_fresh = true;
    portEXIT_CRITICAL(&s_lock);
}

/* 取出上次上传之后的最新一帧；传感器离线时没有新帧，返回 false */
static bool take_fresh_imu(app_imu_data_t *out)
{
    portENTER_CRITICAL(&s_lock);
    bool fresh = s_imu_fresh;
    if (fresh) {
        *out = s_imu;
        s_imu_fresh = false;
    }
    portEXIT_CRITICAL(&s_lock);

    return fresh;
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
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        /* 覆盖遗嘱留下的 offline；保留消息让之后订阅的人马上知道设备在线 */
        esp_mqtt_client_publish(s_client, s_topic_status, "online", 0, 1, 1);
        app_event_publish_mqtt_status(true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        /* 每次重连失败都会收到这个事件，只在状态变化时发事件，避免刷屏 */
        if (s_connected) {
            s_connected = false;
            app_event_publish_mqtt_status(false);
        }
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

static int format_imu_json(char *buf, size_t size, const app_imu_data_t *imu)
{
    time_t now = time(NULL);

    if (now >= TIME_VALID_AFTER) {
        return snprintf(buf, size, "{\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f,\"ts\":%lld}",
                        imu->pitch, imu->roll, imu->yaw, (long long)now);
    }
    return snprintf(buf, size, "{\"pitch\":%.2f,\"roll\":%.2f,\"yaw\":%.2f}",
                    imu->pitch, imu->roll, imu->yaw);
}

/* 上传任务：发布会等网络写完，所以放在自己的任务里，不在事件循环里做 */
static void mqtt_publish_task(void *arg)
{
    char payload[96];
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MQTT_PUBLISH_PERIOD_MS));

        /* 先取数据再判断连接，断线期间的旧帧不会在重连后补发 */
        app_imu_data_t imu;
        bool fresh = take_fresh_imu(&imu);
        if (!fresh || !s_connected) {
            continue;
        }

        int len = format_imu_json(payload, sizeof(payload), &imu);
        int msg_id = esp_mqtt_client_publish(s_client, s_topic_imu, payload, len, 0, 0);
        app_event_publish_mqtt_publish_result(msg_id >= 0, s_topic_imu);
    }
}

esp_err_t mqtt_service_start(void)
{
    if (s_client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 用基础 MAC 做设备 ID：ESP32-P4 没有片上 Wi-Fi，读 ESP_MAC_WIFI_STA 会失败；
     * 在 S3 上 Wi-Fi STA 的 MAC 就是基础 MAC，所以 S3 的 client id 和主题保持不变 */
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_BASE), TAG, "failed to read MAC");

    char id[13];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_client_id, sizeof(s_client_id), "esp32s3-%s", id);
    snprintf(s_topic_imu, sizeof(s_topic_imu), MQTT_TOPIC_ROOT "/%s/imu", id);
    snprintf(s_topic_status, sizeof(s_topic_status), MQTT_TOPIC_ROOT "/%s/status", id);

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        /* mqtts:// 和 wss:// 用根证书包校验服务器，mqtt:// 和 ws:// 会忽略这一项 */
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = s_client_id,
        .credentials.username = MQTT_USERNAME[0] ? MQTT_USERNAME : NULL,
        .credentials.authentication.password = MQTT_PASSWORD[0] ? MQTT_PASSWORD : NULL,
        .session.keepalive = MQTT_KEEPALIVE_S,
        .session.last_will = {
            .topic = s_topic_status,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "failed to create client");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, NULL),
                        TAG, "failed to register MQTT events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(APP_EVENTS, APP_IMU_DATA_UPDATED,
                                                            on_imu_data, NULL, NULL),
                        TAG, "failed to subscribe IMU data");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            on_got_ip, NULL, NULL),
                        TAG, "failed to subscribe IP event");

    if (xTaskCreate(mqtt_publish_task, "mqtt_pub", MQTT_TASK_STACK,
                    NULL, MQTT_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "IMU data -> %s, status -> %s", s_topic_imu, s_topic_status);
    return ESP_OK;
}
