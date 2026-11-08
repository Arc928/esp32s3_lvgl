/*
 * Copyright 2024 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */

/*
 * UI 只负责“数据 → 控件”：
 * 在 LVGL 定时器里向各服务读取最新数据，再刷新控件。
 * 定时器回调运行在 LVGL 任务里，适配层已经持有 LVGL 锁，这里不用再加锁。
 * UI 不直接访问驱动，也不订阅事件，服务层不知道 UI 的存在。
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "custom.h"
#include "imu_service.h"

/*********************
 *      DEFINES
 *********************/
#define IMU_REFRESH_PERIOD_MS   100
#define TIME_REFRESH_PERIOD_MS  1000

/*
 * 传感器离线时的显示。
 * GUI Guider 生成的字体只包含界面上用到的汉字，没有“传感器离线”，
 * 所以先用英文 + LVGL 自带的警告图标（标题框宽 195px，这串约 160px）。
 * 想换成中文：在 GUI Guider 里给 30 号字体加上这几个字，再改这里即可。
 */
#define IMU_OFFLINE_TITLE       LV_SYMBOL_WARNING " OFFLINE"
#define IMU_OFFLINE_VALUE       "--"
#define IMU_OFFLINE_COLOR       0xE5484D

/**********************
 *  STATIC VARIABLES
 **********************/
static char s_imu_title[64];            /* GUI Guider 里的原标题，恢复在线时用 */
static lv_color_t s_imu_title_color;
static bool s_imu_offline_shown = false;

/**********************
 *  STATIC FUNCTIONS
 **********************/

/* 文本没变就不更新，避免无意义的重绘 */
static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void imu_show_offline(lv_ui *ui)
{
    if (s_imu_offline_shown) {
        return;
    }
    s_imu_offline_shown = true;

    lv_label_set_text(ui->screen_imu_status, IMU_OFFLINE_TITLE);
    lv_obj_set_style_text_color(ui->screen_imu_status, lv_color_hex(IMU_OFFLINE_COLOR),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    label_set_text_if_changed(ui->screen_pitch_data, IMU_OFFLINE_VALUE);
    label_set_text_if_changed(ui->screen_roll_data, IMU_OFFLINE_VALUE);
    label_set_text_if_changed(ui->screen_yaw_data, IMU_OFFLINE_VALUE);
}

static void imu_show_online(lv_ui *ui)
{
    if (!s_imu_offline_shown) {
        return;
    }
    s_imu_offline_shown = false;

    lv_label_set_text(ui->screen_imu_status, s_imu_title);
    lv_obj_set_style_text_color(ui->screen_imu_status, s_imu_title_color,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void imu_refresh_cb(lv_timer_t *timer)
{
    lv_ui *ui = lv_timer_get_user_data(timer);
    app_imu_data_t data;
    char buf[16];

    if (!imu_service_get_latest(&data)) {
        // 离线就显示离线；还在初始化时保持界面上的默认值
        if (imu_service_get_status() == IMU_STATUS_OFFLINE) {
            imu_show_offline(ui);
        }
        return;
    }

    imu_show_online(ui);

    snprintf(buf, sizeof(buf), "%.1f°", data.pitch);
    label_set_text_if_changed(ui->screen_pitch_data, buf);

    snprintf(buf, sizeof(buf), "%.1f°", data.roll);
    label_set_text_if_changed(ui->screen_roll_data, buf);

    snprintf(buf, sizeof(buf), "%.1f°", data.yaw);
    label_set_text_if_changed(ui->screen_yaw_data, buf);
}

static void time_refresh_cb(lv_timer_t *timer)
{
    lv_ui *ui = lv_timer_get_user_data(timer);
    time_t now;
    struct tm timeinfo;
    char buf[16];

    time(&now);
    localtime_r(&now, &timeinfo);

    // 还没对过时，保持“校准中”
    if (timeinfo.tm_year < (2016 - 1900)) {
        return;
    }

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    label_set_text_if_changed(ui->screen_time, buf);
}

/**********************
 *  GLOBAL FUNCTIONS
 **********************/

void custom_init(lv_ui *ui)
{
    // 记下标题原样，离线恢复后还原，不在代码里写死 GUI Guider 的文字和颜色
    snprintf(s_imu_title, sizeof(s_imu_title), "%s", lv_label_get_text(ui->screen_imu_status));
    s_imu_title_color = lv_obj_get_style_text_color(ui->screen_imu_status, LV_PART_MAIN);

    lv_timer_create(imu_refresh_cb, IMU_REFRESH_PERIOD_MS, ui);
    lv_timer_create(time_refresh_cb, TIME_REFRESH_PERIOD_MS, ui);
}
