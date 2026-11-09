/*
 * Layout constants and widget helpers shared by the home page and the
 * machine detail page (800 x 480).
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/*********************
 *      LAYOUT
 *********************/
#define UI_SCREEN_W         800
#define UI_SCREEN_H         480
#define UI_STATUS_BAR_H     36
#define UI_GAP              8
#define UI_CARD_PAD         14
#define UI_CARD_RADIUS      8

/**********************
 *   WIDGETS
 **********************/
void ui_set_clickable(lv_obj_t *obj, bool en);
void ui_set_scrollable(lv_obj_t *obj, bool en);
void ui_set_hidden(lv_obj_t *obj, bool en);

/* Display-only object: touches pass through to the parent, no scrolling. */
void ui_make_static(lv_obj_t *obj);

/* Plain, unstyled, display-only container. */
lv_obj_t *ui_create_box(lv_obj_t *parent);

lv_obj_t *ui_create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);

/* New full-size screen with the page background. Not loaded. */
lv_obj_t *ui_create_screen(void);

/* Card background, border and radius. */
void ui_style_card(lv_obj_t *obj);

/* Status bar background and bottom border, sized to the full screen width. */
void ui_style_status_bar(lv_obj_t *bar);

/* Draws a single-color icon image (such as the Wi-Fi symbol) in the given color. */
void ui_tint_icon(lv_obj_t *image, uint32_t color);

/* Turns a status bar icon into a button: clickable, with a touch area larger than the drawn icon. */
void ui_make_icon_button(lv_obj_t *icon, lv_event_cb_t on_click);

/* Thin horizontal or vertical line. */
lv_obj_t *ui_create_divider(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/* Horizontal group sized to its content. */
lv_obj_t *ui_create_row(lv_obj_t *parent, int32_t x, int32_t y, lv_flex_align_t cross_align);

/* In a bottom-aligned row, moves a smaller label so its baseline matches the larger font. */
void ui_match_baseline(lv_obj_t *label, const lv_font_t *own, const lv_font_t *reference);

/**********************
 *   UPDATES
 **********************/
/* Both skip unchanged values so LVGL does not redraw them. */
void ui_set_text(lv_obj_t *label, const char *text);
void ui_set_text_color(lv_obj_t *label, uint32_t color);

/* Shows the wall-clock time, or the uptime while the RTC is not set. */
void ui_update_clock(lv_obj_t *label);

#ifdef __cplusplus
}
#endif

#endif /* UI_COMMON_H */
