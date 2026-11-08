/*
 * board_touch.h
 * GT911 电容触摸（I2C，复位脚在 CH422G 上，先调用 board_exio_init）
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

/**
 * @brief 复位并初始化 GT911
 * @param bus GT911 所在的 I2C 总线（board_i2c_init 得到）
 * @param[out] touch 触摸句柄，交给 board_display_init 注册到 LVGL
 * @return ESP_OK；没接触摸屏或芯片不应答时返回错误，调用方可以不带触摸继续运行
 */
esp_err_t board_touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *touch);
