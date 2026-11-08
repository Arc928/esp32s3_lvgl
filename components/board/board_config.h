/*
 * board_config.h
 * 板级配置 - 本板所有引脚、屏幕参数集中在这里，换板子只需要改这个文件
 */

#pragma once

#include "driver/gpio.h"

/* ==================== LCD：800x480 RGB565 16 位并口屏 ==================== */
#define BOARD_LCD_H_RES                 800
#define BOARD_LCD_V_RES                 480
#define BOARD_LCD_PIXEL_CLOCK_HZ        (16 * 1000 * 1000)
#define BOARD_LCD_DATA_WIDTH            16
#define BOARD_LCD_BITS_PER_PIXEL        16
#define BOARD_LCD_BOUNCE_BUFFER_PX      (BOARD_LCD_H_RES * 20)

/* 时序 */
#define BOARD_LCD_HSYNC_PULSE_WIDTH     4
#define BOARD_LCD_HSYNC_BACK_PORCH      8
#define BOARD_LCD_HSYNC_FRONT_PORCH     8
#define BOARD_LCD_VSYNC_PULSE_WIDTH     4
#define BOARD_LCD_VSYNC_BACK_PORCH      8
#define BOARD_LCD_VSYNC_FRONT_PORCH     8
#define BOARD_LCD_PCLK_ACTIVE_NEG       1

/* 控制引脚 */
#define BOARD_LCD_PIN_HSYNC             GPIO_NUM_46
#define BOARD_LCD_PIN_VSYNC             GPIO_NUM_3
#define BOARD_LCD_PIN_DE                GPIO_NUM_5
#define BOARD_LCD_PIN_PCLK              GPIO_NUM_7
#define BOARD_LCD_PIN_DISP              GPIO_NUM_NC     /* 未使用 */

/* 数据引脚 D0 ~ D15 */
#define BOARD_LCD_PIN_DATA0             GPIO_NUM_14
#define BOARD_LCD_PIN_DATA1             GPIO_NUM_38
#define BOARD_LCD_PIN_DATA2             GPIO_NUM_18
#define BOARD_LCD_PIN_DATA3             GPIO_NUM_17
#define BOARD_LCD_PIN_DATA4             GPIO_NUM_10
#define BOARD_LCD_PIN_DATA5             GPIO_NUM_39
#define BOARD_LCD_PIN_DATA6             GPIO_NUM_0
#define BOARD_LCD_PIN_DATA7             GPIO_NUM_45
#define BOARD_LCD_PIN_DATA8             GPIO_NUM_48
#define BOARD_LCD_PIN_DATA9             GPIO_NUM_47
#define BOARD_LCD_PIN_DATA10            GPIO_NUM_21
#define BOARD_LCD_PIN_DATA11            GPIO_NUM_1
#define BOARD_LCD_PIN_DATA12            GPIO_NUM_2
#define BOARD_LCD_PIN_DATA13            GPIO_NUM_42
#define BOARD_LCD_PIN_DATA14            GPIO_NUM_41
#define BOARD_LCD_PIN_DATA15            GPIO_NUM_40

/* ==================== 显示（LVGL 适配层） ==================== */
#define BOARD_DISPLAY_ROTATION          ESP_LV_ADAPTER_ROTATE_0
#define BOARD_DISPLAY_TEAR_AVOID_MODE   ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL

/* ==================== I2C 总线（MPU6050 挂在这条总线上） ==================== */
#define BOARD_I2C_PORT                  I2C_NUM_0
#define BOARD_I2C_PIN_SDA               GPIO_NUM_8
#define BOARD_I2C_PIN_SCL               GPIO_NUM_9
