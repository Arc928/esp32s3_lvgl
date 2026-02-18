/*
 * Copyright 2024 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */


/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include "lvgl.h"
#include "custom.h"
#include "misc/lv_timer.h"
#include "mpu6050.h"
#include "time.h"
#include "sys/time.h"
#include "wifi_init.h"
#include "lv_port.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "App/app_event.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "CUSTOM"

/**********************
 *      TYPEDEFS
 **********************/
typedef app_event_mpu6050_data_t  mpu6050_data_t;
/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static mpu6050_data_t latest_mpu6050_data = {0};
static SemaphoreHandle_t mpu6050_data_mutex = NULL;
static char buf[32];
extern lv_ui guider_ui;

/* ==================== 事件处理函数 ==================== */

static void mpu6050_event_handler(void *event_handler_arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    if (event_base == APP_EVENTS && event_id == APP_MPU6050_DATA_UPDATED) {
        app_event_mpu6050_data_t *data = (app_event_mpu6050_data_t *)event_data;
        
        if (xSemaphoreTake(mpu6050_data_mutex, portMAX_DELAY) == pdTRUE) {
            latest_mpu6050_data.pitch = data->pitch;
            latest_mpu6050_data.roll = data->roll;
            latest_mpu6050_data.yaw = data->yaw;
            xSemaphoreGive(mpu6050_data_mutex);
        }
        
        lvgl_port_lock(0);
        snprintf(buf, sizeof(buf), "%.1f°", data->pitch);
        lv_label_set_text(guider_ui.screen_pitch_data, buf);
        
        snprintf(buf, sizeof(buf), "%.1f°", data->roll);
        lv_label_set_text(guider_ui.screen_roll_data, buf);
        
        snprintf(buf, sizeof(buf), "%.1f°", data->yaw);
        lv_label_set_text(guider_ui.screen_yaw_data, buf);
        lvgl_port_unlock();
    }
}

/* ==================== 内部函数 ==================== */

void mpu6050_get_data(mpu6050_data_t *data)
{
    if (xSemaphoreTake(mpu6050_data_mutex, portMAX_DELAY) == pdTRUE) {
        *data = latest_mpu6050_data;
        xSemaphoreGive(mpu6050_data_mutex);
    }
}

static void mpu6050_collect_data(void)
{
    float pitch, roll, yaw;
    
    if (MPU6050_DMP_Get_Data(&pitch, &roll, &yaw) == ESP_OK) {
        app_event_publish_mpu6050_data(pitch, roll, yaw);
    }
}

static void mpu6050_update_timer_callback(lv_timer_t * timer)
{
    mpu6050_collect_data();
}

void update_time_display(void)
{
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (timeinfo.tm_year >= (2016 - 1900)) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", 
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        lvgl_port_lock(0);
        lv_label_set_text(guider_ui.screen_time, buf);
        lvgl_port_unlock();
    }
}

static void sntp_sync_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENTS && event_id == SNTP_SYNC_COMPLETED) {
        ESP_LOGI(TAG, "SNTP sync completed");
        update_time_display();
        app_event_publish_time_initialized();
    }
}

static void time_update_timer_callback(lv_timer_t *timer)
{
    update_time_display();
}

void custom_init(lv_ui *ui)
{
    mpu6050_data_mutex = xSemaphoreCreateMutex();
    if (mpu6050_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        APP_EVENTS, APP_MPU6050_DATA_UPDATED, &mpu6050_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENTS, SNTP_SYNC_COMPLETED, &sntp_sync_event_handler, NULL, NULL));
    
    lv_timer_create(mpu6050_update_timer_callback, 100, ui);
    lv_timer_create(time_update_timer_callback, 1000, NULL);
}
