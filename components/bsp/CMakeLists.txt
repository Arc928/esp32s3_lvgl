idf_component_register(SRC_DIRS "mpu6050" "rgb_init" "myiic" "wifi_station" "lvgl_init"
                    INCLUDE_DIRS "mpu6050" "rgb_init" "myiic" "wifi_station" "lvgl_init"
                    REQUIRES driver esp_lcd esp_wifi nvs_flash esp_lvgl_adapter)