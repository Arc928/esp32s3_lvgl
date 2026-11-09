# 板级支持：引脚配置、屏幕、I2C 总线、触摸、LVGL 显示适配、LVGL 堆放 PSRAM（纯硬件，不含业务逻辑）
# 按目标芯片选实现：
#   esp32s3  RGB 并口屏（board_lcd.c）+ CH422G IO 扩展（board_exio.c）
#   esp32p4  MIPI DSI 屏（board_lcd_dsi.c），没有 IO 扩展芯片
if(IDF_TARGET STREQUAL "esp32p4")
    idf_component_register(SRCS "board_lcd_dsi.c" "board_i2c.c" "board_touch.c" "board_display.c" "board_lv_mem.c"
                           INCLUDE_DIRS "."
                           REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd esp_lcd_touch esp_lvgl_adapter
                           PRIV_REQUIRES esp_lcd_touch_gt911 esp_lcd_ek79007 esp_driver_ledc esp_hw_support heap)
else()
    idf_component_register(SRCS "board_lcd.c" "board_i2c.c" "board_exio.c" "board_touch.c" "board_display.c" "board_lv_mem.c"
                           INCLUDE_DIRS "."
                           REQUIRES esp_driver_gpio esp_driver_i2c esp_lcd esp_lcd_touch esp_lvgl_adapter
                           PRIV_REQUIRES esp_lcd_touch_gt911 heap)
endif()

# board_lv_mem.c 提供 LVGL 的分配函数，但 libboard.a 排在 liblvgl.a 前面，链接器处理它时还没有人引用
# lv_malloc_core，这个文件不会被拉进来；用 -u 强制链入
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,lv_malloc_core")
