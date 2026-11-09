/*
 * board_config_esp32s3.h
 * 微雪 ESP32-S3-Touch-LCD-4.3：本板所有引脚、屏幕参数集中在这里
 */

#pragma once

#include "driver/gpio.h"

#define BOARD_LCD_IS_DSI                0
#define BOARD_HAS_EXIO                  1
#define BOARD_UI_PUSH_TRANSITION        0       /* 页面切换用覆盖动画：新页盖上来，每帧只重绘一半画面 */

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
/*
 * LVGL 局部绘制缓冲的行数。TRIPLE_PARTIAL 模式下适配层把这块缓冲固定放在内部 RAM
 * （800 x 行数 x 2 字节）：默认 50 行 = 80KB，会挤掉 Wi-Fi 初始化要的内部 RAM；
 * 20 行 = 32KB，整屏刷新分 24 块画。
 */
#define BOARD_DISPLAY_BUFFER_LINES      20

/* ==================== I2C 总线（CH422G、GT911、MPU6050 都挂在这条总线上） ==================== */
#define BOARD_I2C_PORT                  I2C_NUM_0
#define BOARD_I2C_PIN_SDA               GPIO_NUM_8
#define BOARD_I2C_PIN_SCL               GPIO_NUM_9

/* ==================== IO 扩展芯片 CH422G ==================== */
/* EXIO 脚编号（0~7），按微雪 ESP32-S3-Touch-LCD-4.3 的定义 */
#define BOARD_EXIO_TP_RST               1       /* 触摸复位，低有效 */
#define BOARD_EXIO_DISP                 2       /* 背光，高=亮 */
#define BOARD_EXIO_LCD_RST              3       /* 屏幕复位，低有效 */
#define BOARD_EXIO_SD_CS                4       /* SD 卡片选，低有效 */
#define BOARD_EXIO_USB_SEL              5       /* GPIO19/20 的去向：低=USB 口，高=CAN 收发器 */

/* 启动后的输出：背光亮，屏幕和触摸不复位，SD 不选中，GPIO19/20 走 USB */
#define BOARD_EXIO_DEFAULT_OUTPUT       ((1U << BOARD_EXIO_TP_RST) | (1U << BOARD_EXIO_DISP) | \
                                         (1U << BOARD_EXIO_LCD_RST) | (1U << BOARD_EXIO_SD_CS))

/* ==================== 触摸 GT911（复位脚是 CH422G 的 EXIO1） ==================== */
#define BOARD_TOUCH_PIN_INT             GPIO_NUM_4
#define BOARD_TOUCH_I2C_SPEED_HZ        400000
#define BOARD_TOUCH_MIRROR_X            0
#define BOARD_TOUCH_MIRROR_Y            0
