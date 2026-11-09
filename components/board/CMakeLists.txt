# 板级支持：引脚配置、RGB 屏、I2C 总线、CH422G IO 扩展、GT911 触摸、LVGL 显示适配、LVGL 堆放 PSRAM（纯硬件，不含业务逻辑）
idf_component_register(SRCS "board_lcd.c" "board_i2c.c" "board_exio.c" "board_touch.c" "board_display.c" "board_lv_mem.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd esp_lcd_touch esp_lvgl_adapter
                       PRIV_REQUIRES esp_lcd_touch_gt911 heap)

# board_lv_mem.c 提供 LVGL 的分配函数，但 libboard.a 排在 liblvgl.a 前面，链接器处理它时还没有人引用
# lv_malloc_core，这个文件不会被拉进来；用 -u 强制链入
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,lv_malloc_core")
