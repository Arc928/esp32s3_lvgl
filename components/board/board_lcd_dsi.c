/*
 * MIPI DSI 屏（EK79007，1024x600，2 通道）。和 board_lcd.c 提供同一个 board_lcd_init()，
 * 由 CMakeLists.txt 按目标芯片二选一。
 */

#include "board_lcd.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ek79007.h"
#include "esp_lv_adapter_display.h"
#include "driver/ledc.h"
#include "board_config.h"

#define TAG "board_lcd_dsi"

#define BACKLIGHT_TIMER     LEDC_TIMER_0
#define BACKLIGHT_CHANNEL   LEDC_CHANNEL_0
#define BACKLIGHT_MODE      LEDC_LOW_SPEED_MODE

/* DSI 的 PHY 供电：通道要一直开着，句柄留着不释放 */
static esp_err_t dsi_phy_power_on(void)
{
    static esp_ldo_channel_handle_t s_ldo;

    if (s_ldo != NULL) {
        return ESP_OK;
    }
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_LCD_DSI_PHY_LDO_CHAN,
        .voltage_mv = BOARD_LCD_DSI_PHY_LDO_MV,
    };
    return esp_ldo_acquire_channel(&ldo_cfg, &s_ldo);
}

/* 背光低电平点亮：LEDC 输出取反，占空比拉满 = 最亮 */
static esp_err_t backlight_on(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BACKLIGHT_MODE,
        .duty_resolution = BOARD_LCD_BACKLIGHT_DUTY_BITS,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = BOARD_LCD_BACKLIGHT_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "failed to config backlight timer");

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = BOARD_LCD_PIN_BACKLIGHT,
        .speed_mode = BACKLIGHT_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = (1U << BOARD_LCD_BACKLIGHT_DUTY_BITS) - 1U,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    return ledc_channel_config(&channel_cfg);
}

esp_err_t board_lcd_init(esp_lcd_panel_handle_t *panel_handle)
{
    if (panel_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(dsi_phy_power_on(), TAG, "failed to power DSI PHY");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_cfg = EK79007_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "failed to create DSI bus");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io), TAG, "failed to create DBI IO");

    esp_lcd_dpi_panel_config_t dpi_cfg = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    /* 帧缓冲数量取决于 LVGL 适配层的防撕裂模式，两边要一致 */
    dpi_cfg.num_fbs = esp_lv_adapter_get_required_frame_buffer_count(BOARD_DISPLAY_TEAR_AVOID_MODE,
                                                                     BOARD_DISPLAY_ROTATION);

    /* init_cmds 留空：驱动用自带的 EK79007 初始化序列（2 通道） */
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num = BOARD_LCD_DSI_LANE_NUM,
        },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(io, &panel_cfg, panel_handle), TAG, "failed to create EK79007 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_handle), TAG, "failed to reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle), TAG, "failed to init panel");

    /* 面板初始化好以后再开背光，免得先亮出一屏杂色 */
    ESP_RETURN_ON_ERROR(backlight_on(), TAG, "failed to turn on backlight");

    return ESP_OK;
}
