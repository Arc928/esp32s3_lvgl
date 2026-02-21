#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

esp_err_t lvgl_init_display(esp_lcd_panel_handle_t panel_handle, lv_display_t **display);
