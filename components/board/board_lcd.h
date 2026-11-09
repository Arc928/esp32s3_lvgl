/*
 * board_lcd.h
 * 屏幕面板驱动（引脚和时序见 board_config.h）
 *   esp32s3  RGB 并口屏，实现在 board_lcd.c
 *   esp32p4  MIPI DSI 屏，实现在 board_lcd_dsi.c
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

/**
 * @brief 创建并初始化屏幕面板，帧缓冲放在 PSRAM；P4 上还会在这里打开背光
 * @param[out] panel_handle 面板句柄
 */
esp_err_t board_lcd_init(esp_lcd_panel_handle_t *panel_handle);
