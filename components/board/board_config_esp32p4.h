/*
 * board_config_esp32p4.h
 * 微雪 ESP32-P4-WIFI6-Touch-LCD-7B：7 寸 1024x600 MIPI DSI 屏（EK79007，2 通道）+ GT911 触摸
 *
 * 引脚和时序来自微雪官方示例（waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B）和它的 BSP
 * waveshare/esp32_p4_wifi6_touch_lcd_7b。
 * 这块板子没有 IO 扩展芯片：屏幕复位直接接 GPIO33，背光是 GPIO32 的 PWM，触摸的复位和中断脚没引出。
 */

#pragma once

#include "driver/gpio.h"

#define BOARD_LCD_IS_DSI                1
#define BOARD_HAS_EXIO                  0
#define BOARD_UI_PUSH_TRANSITION        1       /* 页面切换用平移动画：新旧两页一起滑动 */

/* ==================== LCD：1024x600 MIPI DSI，RGB565 ==================== */
#define BOARD_LCD_H_RES                 1024
#define BOARD_LCD_V_RES                 600
#define BOARD_LCD_BITS_PER_PIXEL        16

#define BOARD_LCD_PIN_RST               GPIO_NUM_33
#define BOARD_LCD_DSI_LANE_NUM          2

/* DSI 的 PHY 由芯片内部 LDO 通道 3 供电，要先打开 */
#define BOARD_LCD_DSI_PHY_LDO_CHAN      3
#define BOARD_LCD_DSI_PHY_LDO_MV        2500

/* 背光：GPIO32 的 LEDC PWM，低电平点亮，所以输出取反后占空比越大越亮 */
#define BOARD_LCD_PIN_BACKLIGHT         GPIO_NUM_32
#define BOARD_LCD_BACKLIGHT_FREQ_HZ     5000
#define BOARD_LCD_BACKLIGHT_DUTY_BITS   LEDC_TIMER_10_BIT

/* ==================== 显示（LVGL 适配层） ==================== */
#define BOARD_DISPLAY_ROTATION          ESP_LV_ADAPTER_ROTATE_0
#define BOARD_DISPLAY_TEAR_AVOID_MODE   ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL
/* 局部绘制缓冲放内部 RAM：1024 x 50 x 2 字节 = 100KB。P4 的 Wi-Fi 跑在 C6 上，内部 RAM 不紧张 */
#define BOARD_DISPLAY_BUFFER_LINES      50

/* LVGL 的堆（控件、样式、绘制数据）先放内部 RAM，内部 RAM 剩余不足这个数就改放 PSRAM（见 board_lv_mem.c）。
 * 留给 Wi-Fi 远程栈、网络、MQTT、任务栈和 DMA；S3 不定义，仍然全放 PSRAM。要改回全放 PSRAM，删掉这一行 */
#define BOARD_LV_HEAP_INTERNAL_RESERVE  (128 * 1024)

/* ==================== I2C 总线（GT911 和板载音频芯片挂在这条总线上；外接 MPU6050 也接这里） ==================== */
#define BOARD_I2C_PORT                  I2C_NUM_0
#define BOARD_I2C_PIN_SDA               GPIO_NUM_7
#define BOARD_I2C_PIN_SCL               GPIO_NUM_8

/* ==================== 触摸 GT911（没有复位脚和中断脚，轮询读取，地址 0x5D 或 0x14） ==================== */
#define BOARD_TOUCH_I2C_SPEED_HZ        400000
/* 触摸坐标相对屏幕是 180 度翻转的（微雪 BSP 里 mirror_x 和 mirror_y 都是 1） */
#define BOARD_TOUCH_MIRROR_X            1
#define BOARD_TOUCH_MIRROR_Y            1
