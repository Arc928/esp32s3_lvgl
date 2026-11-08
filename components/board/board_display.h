/*
 * board_display.h
 * 把 RGB 屏注册到 LVGL（通过 esp_lvgl_adapter），并启动 LVGL 任务
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

/**
 * @brief 初始化 LVGL 适配层，注册显示并启动 LVGL 任务
 * @param panel_handle  board_lcd_init() 得到的面板句柄
 * @param[out] display  注册好的 LVGL 显示，可传 NULL
 */
esp_err_t board_display_init(esp_lcd_panel_handle_t panel_handle, lv_display_t **display);
