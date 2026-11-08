/*
 * board_lcd.h
 * RGB 并口屏驱动（引脚和时序见 board_config.h）
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

/**
 * @brief 创建并初始化 RGB 屏面板，帧缓冲放在 PSRAM
 * @param[out] panel_handle 面板句柄
 */
esp_err_t board_lcd_init(esp_lcd_panel_handle_t *panel_handle);
