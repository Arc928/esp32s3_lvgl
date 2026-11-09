/*
 * board_config.h
 * 板级配置入口：按目标芯片选择对应的引脚和屏幕参数，换板子只改对应的 board_config_<芯片>.h
 *
 *   esp32s3  800x480 RGB 并口屏 + CH422G IO 扩展 + GT911（微雪 ESP32-S3-Touch-LCD-4.3）
 *   esp32p4  1024x600 MIPI DSI 屏（EK79007）+ GT911（微雪 ESP32-P4-WIFI6-Touch-LCD-7B）
 *
 * 各头文件都要提供 board_lcd / board_touch / board_display / board_i2c 用到的这些宏：
 *   BOARD_LCD_IS_DSI            1=MIPI DSI 屏，0=RGB 并口屏
 *   BOARD_LCD_H_RES / V_RES     屏幕分辨率
 *   BOARD_DISPLAY_*             LVGL 适配层的旋转、防撕裂模式、局部绘制缓冲行数
 *   BOARD_I2C_*                 I2C 总线
 *   BOARD_HAS_EXIO              1=屏幕复位、背光、触摸复位接在 CH422G 上
 *   BOARD_TOUCH_*               触摸的 I2C 速率和坐标镜像
 */

#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "board_config_esp32s3.h"
#elif CONFIG_IDF_TARGET_ESP32P4
#include "board_config_esp32p4.h"
#else
#error "board: unsupported target, add a board_config_<target>.h"
#endif
