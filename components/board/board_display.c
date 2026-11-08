#include "board_display.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_lv_adapter.h"
#include "board_config.h"

#define TAG "board_display"

esp_err_t board_display_init(esp_lcd_panel_handle_t panel_handle, lv_display_t **display)
{
    if (panel_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&cfg), TAG, "failed to init LVGL adapter");

    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        BOARD_LCD_H_RES,
        BOARD_LCD_V_RES,
        BOARD_DISPLAY_ROTATION);
    /* 和 board_lcd 计算帧缓冲数量时用的模式保持一致 */
    disp_cfg.tear_avoid_mode = BOARD_DISPLAY_TEAR_AVOID_MODE;

    lv_display_t *registered_display = esp_lv_adapter_register_display(&disp_cfg);
    if (registered_display == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "failed to start LVGL adapter");

    if (display != NULL) {
        *display = registered_display;
    }

    return ESP_OK;
}
