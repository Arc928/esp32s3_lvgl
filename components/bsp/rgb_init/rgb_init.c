#include "rgb_init.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000)
#define EXAMPLE_RGB_DATA_WIDTH 16
#define EXAMPLE_RGB_BOUNCE_BUFFER_SIZE (RGB_INIT_H_RES * 20)

#define EXAMPLE_LCD_IO_RGB_HSYNC GPIO_NUM_46
#define EXAMPLE_LCD_IO_RGB_VSYNC GPIO_NUM_3
#define EXAMPLE_LCD_IO_RGB_DE GPIO_NUM_5
#define EXAMPLE_LCD_IO_RGB_PCLK GPIO_NUM_7
#define EXAMPLE_LCD_IO_RGB_DISP (-1)

#define EXAMPLE_LCD_IO_RGB_DATA0 GPIO_NUM_14
#define EXAMPLE_LCD_IO_RGB_DATA1 GPIO_NUM_38
#define EXAMPLE_LCD_IO_RGB_DATA2 GPIO_NUM_18
#define EXAMPLE_LCD_IO_RGB_DATA3 GPIO_NUM_17
#define EXAMPLE_LCD_IO_RGB_DATA4 GPIO_NUM_10
#define EXAMPLE_LCD_IO_RGB_DATA5 GPIO_NUM_39
#define EXAMPLE_LCD_IO_RGB_DATA6 GPIO_NUM_0
#define EXAMPLE_LCD_IO_RGB_DATA7 GPIO_NUM_45
#define EXAMPLE_LCD_IO_RGB_DATA8 GPIO_NUM_48
#define EXAMPLE_LCD_IO_RGB_DATA9 GPIO_NUM_47
#define EXAMPLE_LCD_IO_RGB_DATA10 GPIO_NUM_21
#define EXAMPLE_LCD_IO_RGB_DATA11 GPIO_NUM_1
#define EXAMPLE_LCD_IO_RGB_DATA12 GPIO_NUM_2
#define EXAMPLE_LCD_IO_RGB_DATA13 GPIO_NUM_42
#define EXAMPLE_LCD_IO_RGB_DATA14 GPIO_NUM_41
#define EXAMPLE_LCD_IO_RGB_DATA15 GPIO_NUM_40

esp_err_t rgb_init_panel(esp_lcd_panel_handle_t *panel_handle)
{
    if (panel_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel = 16,
        .dma_burst_size = 64,
        .num_fbs = esp_lv_adapter_get_required_frame_buffer_count(RGB_INIT_TEAR_AVOID_MODE, RGB_INIT_ROTATION),
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = RGB_INIT_H_RES,
            .v_res = RGB_INIT_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, panel_handle), "rgb_init", "failed to create RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_handle), "rgb_init", "failed to reset RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle), "rgb_init", "failed to init RGB panel");

    return ESP_OK;
}
