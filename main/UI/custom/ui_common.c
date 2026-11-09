/*
 * Widget helpers shared by the home page and the machine detail page.
 */

#include <string.h>
#include <time.h>

#include "ui_common.h"
#include "dashboard_theme.h"

/* LVGL 9.6 replaced lv_obj_add_flag() / lv_obj_remove_flag() with per-flag setters. */
#define UI_HAS_FLAG_SETTERS (LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6))

/* Before 2024-01-01 the RTC has not been set; show uptime instead. */
#define CLOCK_VALID_AFTER ((time_t)1704067200)

static lv_style_t s_style_card;
static bool s_style_card_ready;

void ui_set_clickable(lv_obj_t *obj, bool en)
{
#if UI_HAS_FLAG_SETTERS
    lv_obj_set_clickable(obj, en);
#else
    if(en) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
#endif
}

void ui_set_scrollable(lv_obj_t *obj, bool en)
{
#if UI_HAS_FLAG_SETTERS
    lv_obj_set_scrollable(obj, en);
#else
    if(en) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }
#endif
}

void ui_set_hidden(lv_obj_t *obj, bool en)
{
#if UI_HAS_FLAG_SETTERS
    lv_obj_set_hidden(obj, en);
#else
    if(en) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void ui_make_static(lv_obj_t *obj)
{
    ui_set_clickable(obj, false);
    ui_set_scrollable(obj, false);
}

lv_obj_t *ui_create_box(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    ui_make_static(obj);
    return obj;
}

lv_obj_t *ui_create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *ui_create_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_size(screen, UI_SCREEN_W, UI_SCREEN_H);
    /* Stays clickable so it can receive gestures. */
    ui_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    return screen;
}

void ui_style_card(lv_obj_t *obj)
{
    if(!s_style_card_ready) {
        lv_style_init(&s_style_card);
        lv_style_set_bg_color(&s_style_card, lv_color_hex(DASH_COLOR_SURFACE));
        lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
        lv_style_set_border_color(&s_style_card, lv_color_hex(DASH_COLOR_BORDER));
        lv_style_set_border_width(&s_style_card, 1);
        lv_style_set_radius(&s_style_card, UI_CARD_RADIUS);
        s_style_card_ready = true;
    }
    lv_obj_add_style(obj, &s_style_card, LV_PART_MAIN);
}

void ui_style_status_bar(lv_obj_t *bar)
{
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, UI_SCREEN_W, UI_STATUS_BAR_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
}

void ui_tint_icon(lv_obj_t *image, uint32_t color)
{
    lv_obj_set_style_image_recolor(image, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
}

void ui_make_icon_button(lv_obj_t *icon, lv_event_cb_t on_click)
{
    ui_set_clickable(icon, true);
    /* The icon is 30 px in a 36 px bar (at 800 x 480); the extra area makes it easy to hit with a finger. */
    lv_obj_set_ext_click_area(icon, UI_SU(10));
    lv_obj_add_event_cb(icon, on_click, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *ui_create_divider(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *line = ui_create_box(parent);

    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_set_style_bg_color(line, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    return line;
}

lv_obj_t *ui_create_row(lv_obj_t *parent, int32_t x, int32_t y, lv_flex_align_t cross_align)
{
    lv_obj_t *row = ui_create_box(parent);

    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, cross_align, cross_align);
    lv_obj_set_style_pad_column(row, UI_SU(4), LV_PART_MAIN);
    return row;
}

void ui_match_baseline(lv_obj_t *label, const lv_font_t *own, const lv_font_t *reference)
{
    lv_obj_set_style_translate_y(label, own->base_line - reference->base_line, LV_PART_MAIN);
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    const char *current = lv_label_get_text(label);

    if(current != NULL && strcmp(current, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
}

void ui_set_text_color(lv_obj_t *label, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);

    if(lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), c)) {
        return;
    }
    lv_obj_set_style_text_color(label, c, LV_PART_MAIN);
}

void ui_update_clock(lv_obj_t *label)
{
    char buf[16];
    time_t now = time(NULL);
    const struct tm *local = now >= CLOCK_VALID_AFTER ? localtime(&now) : NULL;

    if(local != NULL) {
        lv_snprintf(buf, sizeof(buf), "%02d:%02d:%02d", local->tm_hour, local->tm_min, local->tm_sec);
    }
    else {
        unsigned long up = (unsigned long)(lv_tick_get() / 1000U);
        lv_snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (up / 3600UL) % 100UL, (up / 60UL) % 60UL, up % 60UL);
    }
    ui_set_text(label, buf);
}
