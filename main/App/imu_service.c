/*
 * imu_service.c
 * 姿态服务实现
 */

#include "imu_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mpu6050.h"

#define TAG "IMU_SERVICE"

#define IMU_READ_PERIOD_MS      100     /* 正常采集周期 */
#define IMU_RETRY_PERIOD_MS     3000    /* 离线时重新初始化的间隔 */
#define IMU_OFFLINE_FAIL_COUNT  10      /* 连续读失败次数达到这个值（约 1 秒）判定为掉线 */
#define IMU_TASK_STACK          4096
#define IMU_TASK_PRIO           3       /* 低于 LVGL 任务（6），不抢屏幕刷新 */

/* 数据只有十几个字节，用自旋锁做极短的临界区即可 */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static app_imu_data_t s_latest = {0};
static imu_status_t s_status = IMU_STATUS_STARTING;
static TaskHandle_t s_task = NULL;
static i2c_master_bus_handle_t s_bus = NULL;

static void set_status(imu_status_t status)
{
    portENTER_CRITICAL(&s_lock);
    s_status = status;
    portEXIT_CRITICAL(&s_lock);
}

static void set_latest(const app_imu_data_t *data)
{
    portENTER_CRITICAL(&s_lock);
    s_latest = *data;
    s_status = IMU_STATUS_ONLINE;
    portEXIT_CRITICAL(&s_lock);
}

/* 进入离线状态：只在状态变化时打日志和发事件，避免刷屏 */
static void go_offline(esp_err_t err, const char *reason)
{
    if (imu_service_get_status() == IMU_STATUS_OFFLINE) {
        return;
    }

    set_status(IMU_STATUS_OFFLINE);
    ESP_LOGW(TAG, "MPU6050 offline: %s (%s), retry every %d ms",
             reason, esp_err_to_name(err), IMU_RETRY_PERIOD_MS);
    app_event_publish_sensor_error("MPU6050", err);
}

static esp_err_t sensor_init(void)
{
    esp_err_t ret = MPU6050_Init(s_bus);
    if (ret == ESP_OK) {
        ret = DMP_Init();
    }
    return ret;
}

static void imu_task(void *arg)
{
    bool ready = false;
    int fail_count = 0;
    TickType_t last_wake = 0;

    while (1) {
        /* 初始化（含掉线后的重新初始化） */
        if (!ready) {
            esp_err_t err = sensor_init();
            if (err != ESP_OK) {
                go_offline(err, "init failed");
                vTaskDelay(pdMS_TO_TICKS(IMU_RETRY_PERIOD_MS));
                continue;
            }

            ESP_LOGI(TAG, "MPU6050 ready");
            ready = true;
            fail_count = 0;
            last_wake = xTaskGetTickCount();
        }

        /* 采集 */
        app_imu_data_t data;
        if (MPU6050_DMP_Get_Data(&data.pitch, &data.roll, &data.yaw) == 0) {
            if (imu_service_get_status() == IMU_STATUS_OFFLINE) {
                ESP_LOGI(TAG, "MPU6050 back online");
            }
            fail_count = 0;
            set_latest(&data);
            app_event_publish_imu_data(&data);
        } else if (++fail_count >= IMU_OFFLINE_FAIL_COUNT) {
            /* 连续读不到数据：可能断线或芯片掉电，DMP 配置已丢失，需要重新初始化 */
            go_offline(ESP_ERR_TIMEOUT, "no data");
            ready = false;
            vTaskDelay(pdMS_TO_TICKS(IMU_RETRY_PERIOD_MS));
            continue;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IMU_READ_PERIOD_MS));
    }
}

esp_err_t imu_service_start(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_bus = bus;

    BaseType_t ret = xTaskCreate(imu_task, "imu_task", IMU_TASK_STACK,
                                 NULL, IMU_TASK_PRIO, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

imu_status_t imu_service_get_status(void)
{
    portENTER_CRITICAL(&s_lock);
    imu_status_t status = s_status;
    portEXIT_CRITICAL(&s_lock);

    return status;
}

bool imu_service_get_latest(app_imu_data_t *out)
{
    bool online = false;

    if (out == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    if (s_status == IMU_STATUS_ONLINE) {
        *out = s_latest;
        online = true;
    }
    portEXIT_CRITICAL(&s_lock);

    return online;
}
