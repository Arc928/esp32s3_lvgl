#include "lvgl_init.h"

#include <stddef.h>

#include "esp_check.h"
#include "rgb_init.h"

esp_err_t lvgl_init_display(esp_lcd_panel_handle_t panel_handle, lv_display_t **display)
{
    if (panel_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&cfg), "lvgl_init", "failed to init LVGL adapter");

    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        RGB_INIT_H_RES,
        RGB_INIT_V_RES,
        RGB_INIT_ROTATION);

    lv_display_t *registered_display = esp_lv_adapter_register_display(&disp_cfg);
    if (registered_display == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), "lvgl_init", "failed to start LVGL adapter");

    if (display != NULL) {
        *display = registered_display;
    }

    return ESP_OK;
}
