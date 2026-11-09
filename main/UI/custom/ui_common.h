/*
 * Layout constants and widget helpers shared by all pages.
 *
 * The pages are drawn for an 800 x 480 screen and scale to the panel: sizes and
 * positions go through UI_SX / UI_SY / UI_SU, which are the identity on 800 x 480
 * and grow with the resolution elsewhere. The fonts come in two sets (see
 * dashboard_theme.h) that follow the scale. The 30 px Wi-Fi icon is a bitmap of a
 * fixed size and does not scale, so the offsets next to it are left unscaled.
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
/* The panel size comes from the board; a simulator can define UI_SCREEN_W / UI_SCREEN_H instead. */
#ifndef UI_SCREEN_W
#include "board_config.h"
#define UI_SCREEN_W         BOARD_LCD_H_RES
#define UI_SCREEN_H         BOARD_LCD_V_RES
#endif

/* The screen the pages were designed for. */
#define UI_BASE_W           800
#define UI_BASE_H           480

/* A horizontal / vertical distance of the 800 x 480 design, scaled to the panel. */
#define UI_SX(v)            ((int32_t)((v) * UI_SCREEN_W / UI_BASE_W))
#define UI_SY(v)            ((int32_t)((v) * UI_SCREEN_H / UI_BASE_H))
/* A size that must keep its shape (circles, paddings, radii): the smaller of the two scale factors. */
#define UI_SU(v)            ((UI_SCREEN_W * UI_BASE_H <= UI_SCREEN_H * UI_BASE_W) ? UI_SX(v) : UI_SY(v))
/* LVGL image zoom for photos drawn for the 800 x 480 design (256 = 1x). */
#define UI_IMG_SCALE        UI_SU(256)
/* The scale in percent as a plain constant (100 on 800 x 480), usable in #if; dashboard_theme.h picks the fonts by it. */
#define UI_SCALE_PCT        ((UI_SCREEN_W * 100 / UI_BASE_W) < (UI_SCREEN_H * 100 / UI_BASE_H) ? \
                             (UI_SCREEN_W * 100 / UI_BASE_W) : (UI_SCREEN_H * 100 / UI_BASE_H))

/*
 * How a page change is animated. 1 = both pages move sideways together, like pages being
 * pushed; 0 = the new page covers the old one, which stays put, so about half as much is
 * redrawn per frame (for boards that are short of speed). The board decides; a simulator
 * can define it.
 */
#ifndef UI_PUSH_TRANSITION
#ifdef BOARD_UI_PUSH_TRANSITION
#define UI_PUSH_TRANSITION      BOARD_UI_PUSH_TRANSITION
#else
#define UI_PUSH_TRANSITION      0
#endif
#endif

#define UI_STATUS_BAR_H     UI_SY(36)
#define UI_GAP              UI_SU(8)
#define UI_CARD_PAD         UI_SU(14)
#define UI_CARD_RADIUS      UI_SU(8)

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
