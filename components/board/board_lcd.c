#include "board_lcd.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lv_adapter_display.h"
#include "board_config.h"

#define TAG "board_lcd"

esp_err_t board_lcd_init(esp_lcd_panel_handle_t *panel_handle)
{
    if (panel_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = BOARD_LCD_DATA_WIDTH,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .dma_burst_size = 64,
        /* 帧缓冲数量取决于 LVGL 适配层的防撕裂模式，两边要一致 */
        .num_fbs = esp_lv_adapter_get_required_frame_buffer_count(BOARD_DISPLAY_TEAR_AVOID_MODE,
                                                                  BOARD_DISPLAY_ROTATION),
        .bounce_buffer_size_px = BOARD_LCD_BOUNCE_BUFFER_PX,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = BOARD_LCD_PIN_DISP,
        .pclk_gpio_num = BOARD_LCD_PIN_PCLK,
        .vsync_gpio_num = BOARD_LCD_PIN_VSYNC,
        .hsync_gpio_num = BOARD_LCD_PIN_HSYNC,
        .de_gpio_num = BOARD_LCD_PIN_DE,
        .data_gpio_nums = {
            BOARD_LCD_PIN_DATA0,
            BOARD_LCD_PIN_DATA1,
            BOARD_LCD_PIN_DATA2,
            BOARD_LCD_PIN_DATA3,
            BOARD_LCD_PIN_DATA4,
            BOARD_LCD_PIN_DATA5,
            BOARD_LCD_PIN_DATA6,
            BOARD_LCD_PIN_DATA7,
            BOARD_LCD_PIN_DATA8,
            BOARD_LCD_PIN_DATA9,
            BOARD_LCD_PIN_DATA10,
            BOARD_LCD_PIN_DATA11,
            BOARD_LCD_PIN_DATA12,
            BOARD_LCD_PIN_DATA13,
            BOARD_LCD_PIN_DATA14,
            BOARD_LCD_PIN_DATA15,
        },
        .timings = {
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG,
            },
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, panel_handle), TAG, "failed to create RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_handle), TAG, "failed to reset RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle), TAG, "failed to init RGB panel");

    return ESP_OK;
}
