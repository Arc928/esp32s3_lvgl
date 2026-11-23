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
 * The home page (the machine gallery) and the detail page (machine photo on the
 * left half, 8 MQTT control commands on the right half) are the left and right
 * page of a pager. The home page picks the machine: the picked card is highlighted
 * and shown on the detail page. Tapping a card picks it and opens the detail page;
 * a swipe to the left on the home page opens the detail page of the highlighted
 * machine; a swipe to the right on the detail page, or "返回", goes back. Swipes
 * the other way do nothing, there is no page there.
 *
 * The Wi-Fi icon in the status bar of both pages opens the Wi-Fi page; "返回"
 * there returns to the page it was opened from.
 *
 * Incoming MQTT device control commands automatically switch to the target
 * machine's detail page and magnify the matching command in place.
 */

#include "lvgl.h"
#include "custom.h"
#include "dashboard_data.h"
#include "gallery_view.h"
#include "machine_view.h"
#include "wifi_view.h"
#include "ui_common.h"

#define REFRESH_MS      200U

/*
 * Page transitions, see UI_PUSH_TRANSITION: MOVE_* pushes both pages sideways together,
 * OVER_* lets the new page cover the old one.
 */
#if UI_PUSH_TRANSITION
#define PAGE_SWITCH_MS  250U
#define ANIM_FORWARD    LV_SCREEN_LOAD_ANIM_MOVE_LEFT   /* new page comes in from the right */
#define ANIM_BACK       LV_SCREEN_LOAD_ANIM_MOVE_RIGHT  /* new page comes in from the left */
#else
#define PAGE_SWITCH_MS  200U
#define ANIM_FORWARD    LV_SCREEN_LOAD_ANIM_OVER_LEFT
#define ANIM_BACK       LV_SCREEN_LOAD_ANIM_OVER_RIGHT
#endif

static lv_obj_t *s_home;
static lv_obj_t *s_detail;
static lv_obj_t *s_wifi;
static lv_obj_t *s_wifi_return;  /* page the Wi-Fi page was opened from */
static uint32_t s_selected;      /* machine shown on the detail page */
static uint32_t s_last_cmd_seq;

static bool page_switching(void)
{
    return lv_display_get_screen_loading(lv_display_get_default()) != NULL;
}

static void handle_incoming_cmd(void)
{
    mqtt_cmd_data_t cmd;
    uint32_t target_idx;
    lv_obj_t *active;

    if(!mqtt_data_get_latest_cmd(&cmd) || cmd.seq == s_last_cmd_seq) {
        return;
    }

    if(cmd.device_id < MQTT_CMD_DEVICE_MIN || cmd.device_id > MQTT_CMD_DEVICE_MAX) {
        s_last_cmd_seq = cmd.seq;
        return;
    }

    /* Defer while a screen transition animation is in flight so no command is dropped. */
    if(page_switching()) {
        return;
    }

    s_last_cmd_seq = cmd.seq;
    target_idx = (uint32_t)(cmd.device_id - MQTT_CMD_DEVICE_MIN);
    active = lv_screen_active();

    if(active == s_detail) {
        if(s_selected != target_idx) {
            s_selected = target_idx;
            gallery_view_set_selected(target_idx);
            machine_view_set_machine(target_idx);
        }
        machine_view_set_cmd(&cmd, true);
        return;
    }

    s_selected = target_idx;
    gallery_view_set_selected(target_idx);
    machine_view_set_machine(target_idx);
    machine_view_set_cmd(&cmd, true);
    machine_view_update(NULL, NULL, mqtt_data_is_connected());

    if(active == s_wifi) {
        wifi_view_leave();
        lv_screen_load_anim(s_detail, ANIM_BACK, PAGE_SWITCH_MS, 0, false);
        return;
    }
    lv_screen_load_anim(s_detail, ANIM_FORWARD, PAGE_SWITCH_MS, 0, false);
}

static void refresh(lv_timer_t *timer)
{
    bool mqtt_ok = mqtt_data_is_connected();

    LV_UNUSED(timer);
    gallery_view_update(mqtt_ok);
    machine_view_update(NULL, NULL, mqtt_ok);
    handle_incoming_cmd();
}

static void open_machine(uint32_t machine_index)
{
    mqtt_cmd_data_t dev_cmd;

    if(page_switching() || lv_screen_active() != s_home) {
        return;
    }
    s_selected = machine_index;
    gallery_view_set_selected(machine_index);
    machine_view_set_machine(machine_index);
    if(mqtt_data_get_device_cmd((uint8_t)(machine_index + MQTT_CMD_DEVICE_MIN), &dev_cmd)) {
        machine_view_set_cmd(&dev_cmd, false);
    }
    else {
        machine_view_set_cmd(NULL, false);
    }
    machine_view_update(NULL, NULL, mqtt_data_is_connected());
    lv_screen_load_anim(s_detail, ANIM_FORWARD, PAGE_SWITCH_MS, 0, false);
}

#if DASHBOARD_DATA_MOCK
static void on_mock_mqtt_pill_clicked(void)
{
    mqtt_data_mock_trigger_next();
    handle_incoming_cmd();
}
#endif

static void go_home(void)
{
    if(page_switching() || lv_screen_active() != s_detail) {
        return;
    }
    lv_screen_load_anim(s_home, ANIM_BACK, PAGE_SWITCH_MS, 0, false);
}

static void open_wifi(void)
{
    lv_obj_t *from = lv_screen_active();

    if(page_switching() || from == s_wifi) {
        return;
    }
    s_wifi_return = from;
    wifi_view_enter();
    lv_screen_load_anim(s_wifi, ANIM_FORWARD, PAGE_SWITCH_MS, 0, false);
}

static void close_wifi(void)
{
    if(page_switching() || lv_screen_active() != s_wifi) {
        return;
    }
    wifi_view_leave();
    lv_screen_load_anim(s_wifi_return, ANIM_BACK, PAGE_SWITCH_MS, 0, false);
}

/* Right page of the pager: only a swipe to the right (back to the left page) does anything. */
static void on_detail_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    LV_UNUSED(e);
    if(dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if(dir == LV_DIR_RIGHT) {
        go_home();
    }
}

/* Left page of the pager: a swipe to the left opens the detail page of the highlighted machine. */
static void on_home_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    LV_UNUSED(e);
    if(dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if(dir == LV_DIR_LEFT) {
        open_machine(s_selected);
    }
}

void custom_init(lv_ui *ui)
{
#if DASHBOARD_DATA_MOCK
    machine_view_mqtt_cb_t mqtt_cb = on_mock_mqtt_pill_clicked;
#else
    machine_view_mqtt_cb_t mqtt_cb = NULL;
#endif

    mqtt_data_init();
    machine_view_create(ui, go_home, open_wifi, mqtt_cb);
    s_detail = ui->screen;
    lv_obj_add_event_cb(s_detail, on_detail_gesture, LV_EVENT_GESTURE, NULL);

    s_wifi = wifi_view_create(close_wifi);
    s_home = gallery_view_create(open_machine, open_wifi, mqtt_cb);
    gallery_view_set_selected(s_selected);
    lv_obj_add_event_cb(s_home, on_home_gesture, LV_EVENT_GESTURE, NULL);

    lv_screen_load(s_home);

    refresh(NULL);
    lv_timer_create(refresh, REFRESH_MS, NULL);
}
