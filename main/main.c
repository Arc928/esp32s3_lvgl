#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port.h"
#include "mpu6050.h"
#include "iic_init.h"
#include "wifi_init.h"
#include "sntp.h"
#include "nvs_flash.h"
#include "gui_guider.h"
#include "custom.h"
#include "App/app_event.h"

// GUI Guider生成的UI结构体
lv_ui guider_ui;

void app_main(void)
{
    // 1. 初始化NVS（WiFi配置存储）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化事件系统
    app_event_init();

    // 3. 初始化硬件（I2C、MPU6050）
    i2c_bus_init();
    ESP_ERROR_CHECK(MPU6050_Init());
    DMP_Init();
    
    // 4. 初始化LVGL显示
    ESP_ERROR_CHECK(app_lvgl_init());
    
    // 5. 初始化UI（需加锁）
    lvgl_port_lock(0);
    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    lvgl_port_unlock();

    // 6. 初始化网络时间
    time_init();
    wifi_init_sta();

    // 7. 主循环
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}