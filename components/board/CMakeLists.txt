# 板级支持：引脚配置、RGB 屏、I2C 总线、CH422G IO 扩展、GT911 触摸、LVGL 显示适配（纯硬件，不含业务逻辑）
idf_component_register(SRCS "board_lcd.c" "board_i2c.c" "board_exio.c" "board_touch.c" "board_display.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd esp_lcd_touch esp_lvgl_adapter
                       PRIV_REQUIRES esp_lcd_touch_gt911)
