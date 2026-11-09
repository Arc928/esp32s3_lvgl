#include "board_touch.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "board_config.h"
#if BOARD_HAS_EXIO
#include "board_exio.h"
#endif

#define TAG "board_touch"

#define PROBE_TIMEOUT_MS    50

#if BOARD_HAS_EXIO
/*
 * GT911 在复位脚释放的那一刻看 INT 脚的电平来决定 I2C 地址：低=0x5D，高=0x14。
 * 复位脚接在 CH422G 上，GT911 驱动自带的复位用不了，所以在这里做：
 * INT 拉低 → 复位拉低 10ms → 释放复位，INT 再保持 10ms → INT 改回输入交给 GT911，等 50ms 后才能通信。
 */
static esp_err_t gt911_reset(void)
{
    const gpio_config_t int_output = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    const gpio_config_t int_input = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_INT,
        .mode = GPIO_MODE_INPUT,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&int_output), TAG, "failed to config INT");
    gpio_set_level(BOARD_TOUCH_PIN_INT, 0);

    ESP_RETURN_ON_ERROR(board_exio_set(BOARD_EXIO_TP_RST, false), TAG, "failed to reset GT911");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(board_exio_set(BOARD_EXIO_TP_RST, true), TAG, "failed to release GT911 reset");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(gpio_config(&int_input), TAG, "failed to config INT");
    vTaskDelay(pdMS_TO_TICKS(50));

    return ESP_OK;
}
#endif /* BOARD_HAS_EXIO */

/* 一般在 0x5D；万一地址没选上，再试一下另一个 */
static esp_err_t gt911_find_address(i2c_master_bus_handle_t bus, uint8_t *addr)
{
    static const uint8_t candidates[] = {
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (i2c_master_probe(bus, candidates[i], PROBE_TIMEOUT_MS) == ESP_OK) {
            *addr = candidates[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t board_touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *touch)
{
    uint8_t addr = 0;
    esp_lcd_panel_io_handle_t io = NULL;

    if (bus == NULL || touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if BOARD_HAS_EXIO
    ESP_RETURN_ON_ERROR(gt911_reset(), TAG, "GT911 reset failed");
#endif
    ESP_RETURN_ON_ERROR(gt911_find_address(bus, &addr), TAG, "GT911 not found on I2C bus");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = addr;
    io_cfg.scl_speed_hz = BOARD_TOUCH_I2C_SPEED_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "failed to create touch IO");

    /* 复位已经在上面做过，这里不再把引脚交给驱动；不用中断，由 LVGL 定时读取 */
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .flags = {
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
    };
    esp_err_t ret = esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, touch);
    if (ret != ESP_OK) {
        esp_lcd_panel_io_del(io);
        ESP_LOGE(TAG, "failed to init GT911: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GT911 ready at 0x%02X", addr);
    return ESP_OK;
}
