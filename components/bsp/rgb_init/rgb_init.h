#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter_display.h"

#define RGB_INIT_H_RES 800
#define RGB_INIT_V_RES 480
#define RGB_INIT_ROTATION ESP_LV_ADAPTER_ROTATE_0
#define RGB_INIT_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL

esp_err_t rgb_init_panel(esp_lcd_panel_handle_t *panel_handle);
