/*
* Copyright 2024 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

/*
 * Entry point called after setup_ui().
 *
 * Home page: the machine gallery. Tapping a machine opens its detail page
 * (BMS on the left half, other information on the right half). "返回" or a
 * swipe to the left or right goes back home.
 */

#include "lvgl.h"
#include "custom.h"
#include "dashboard_data.h"
#include "gallery_view.h"
#include "machine_view.h"

#define REFRESH_MS      500U
#define PAGE_SWITCH_MS  300U

static lv_obj_t *s_home;
static lv_obj_t *s_detail;

static bool page_switching(void)
{
    return lv_display_get_screen_loading(lv_display_get_default()) != NULL;
}

static void refresh(lv_timer_t *timer)
{
    bms_data_t bms;
    imu_data_t imu;
    bool has_bms = bms_data_get_latest(&bms);
    bool has_imu = imu_data_get_latest(&imu);

    LV_UNUSED(timer);
    gallery_view_update();
    machine_view_update(has_bms ? &bms : NULL, has_imu ? &imu : NULL);
}

static void open_machine(uint32_t machine_index)
{
    if(page_switching() || lv_screen_active() != s_home) {
        return;
    }
    machine_view_set_machine(machine_index);
    refresh(NULL);
    lv_screen_load_anim(s_detail, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, PAGE_SWITCH_MS, 0, false);
}

/* The home page slides in following the finger (or from the left for "返回"). */
static void slide_home(lv_screen_load_anim_t anim)
{
    if(page_switching() || lv_screen_active() != s_detail) {
        return;
    }
    lv_screen_load_anim(s_home, anim, PAGE_SWITCH_MS, 0, false);
}

static void go_home(void)
{
    slide_home(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

/* A swipe to the left or to the right goes back home. */
static void on_detail_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    LV_UNUSED(e);
    if(dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    slide_home(dir == LV_DIR_LEFT ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

void custom_init(lv_ui *ui)
{
    machine_view_create(ui, go_home);
    s_detail = ui->screen;
    lv_obj_add_event_cb(s_detail, on_detail_gesture, LV_EVENT_GESTURE, NULL);

    s_home = gallery_view_create(open_machine);
    lv_screen_load(s_home);

    refresh(NULL);
    lv_timer_create(refresh, REFRESH_MS, NULL);
}
