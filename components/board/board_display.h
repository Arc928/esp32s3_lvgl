/*
 * board_display.h
 * 把 RGB 屏和触摸注册到 LVGL（通过 esp_lvgl_adapter），并启动 LVGL 任务
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

/**
 * @brief 初始化 LVGL 适配层，注册显示和触摸并启动 LVGL 任务
 * @param panel_handle  board_lcd_init() 得到的面板句柄
 * @param touch         board_touch_init() 得到的触摸句柄；传 NULL 表示不带触摸
 * @param[out] display  注册好的 LVGL 显示，可传 NULL
 */
esp_err_t board_display_init(esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch,
                             lv_display_t **display);
