/*
 * Home page: farm machine gallery (800 x 480).
 *
 *  +-- hint ---------------- 农机设备 ------------ clock  wifi --+
 *  | photo 1          | photo 2          | photo 3            |
 *  | photo 4          | photo 5          | photo 6            |
 *  | photo 7          | photo 8          | photo 9            |
 *  +----------------------------------------------------------+
 *
 * Each card is a button: tapping it calls the select callback.
 */

#include "gallery_view.h"
#include "gui_guider.h"
#include "dashboard_theme.h"
#include "machine_list.h"
#include "ui_common.h"

/*********************
 *      LAYOUT
 *********************/
#define COLS            3
#define ROWS            3
#define CELL_W          ((UI_SCREEN_W - (COLS + 1) * UI_GAP) / COLS)
#define CELL_H          ((UI_SCREEN_H - UI_STATUS_BAR_H - (ROWS + 1) * UI_GAP) / ROWS)

/* Must match BOX_W / BOX_H in tools/gen_images.sh. */
#define IMG_BOX_H       106
#define IMG_TOP         3
#define CAPTION_BOTTOM  5

/**********************
 *  STATIC VARIABLES
 **********************/
static gallery_select_cb_t s_on_select;
static lv_obj_t *s_clock;

/**********************
 *   BUILD
 **********************/
static void build_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = ui_create_box(screen);
    lv_obj_t *label;
    lv_obj_t *wifi;

    ui_style_status_bar(bar);

    label = ui_create_label(bar, DASH_FONT_SMALL, DASH_COLOR_MUTED, "点击农机查看详情");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);

    label = ui_create_label(bar, DASH_FONT_BODY, DASH_COLOR_TEXT, "农机设备");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    s_clock = ui_create_label(bar, DASH_FONT_BODY, DASH_COLOR_TEXT, "--:--:--");
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, -44, 0);

    wifi = lv_image_create(bar);
    lv_image_set_src(wifi, &_wifi1_RGB565A8_30x30);
    ui_tint_icon(wifi, DASH_COLOR_TEXT);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -4, 0);
}

static void on_card_clicked(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    if(s_on_select != NULL) {
        s_on_select(index);
    }
}

static void build_card(lv_obj_t *screen, uint32_t index)
{
    const machine_info_t *m = machine_get(index);
    int32_t x = UI_GAP + (int32_t)(index % COLS) * (CELL_W + UI_GAP);
    int32_t y = UI_STATUS_BAR_H + UI_GAP + (int32_t)(index / COLS) * (CELL_H + UI_GAP);
    lv_obj_t *card = ui_create_box(screen);
    lv_obj_t *image;
    lv_obj_t *caption;
    char number[4];

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, CELL_W, CELL_H);
    ui_style_card(card);

    /* The whole card is the button; its children let touches through. */
    ui_set_clickable(card, true);
    lv_obj_set_style_bg_color(card, lv_color_hex(DASH_COLOR_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(card, lv_color_hex(DASH_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, on_card_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    /* Center the photo inside its box; the box sits at the top of the card. */
    image = lv_image_create(card);
    lv_image_set_src(image, m->image);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, IMG_TOP + (IMG_BOX_H - (int32_t)m->image->header.h) / 2);

    /* "01  旋耕起垄机" */
    caption = ui_create_row(card, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(caption, 8, LV_PART_MAIN);
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, -CAPTION_BOTTOM);

    lv_snprintf(number, sizeof(number), "%02u", (unsigned)(index + 1U));
    ui_create_label(caption, DASH_FONT_SMALL, DASH_COLOR_ACCENT, number);
    ui_create_label(caption, DASH_FONT_BODY, DASH_COLOR_TEXT, m->name);
}

lv_obj_t *gallery_view_create(gallery_select_cb_t on_select)
{
    lv_obj_t *screen = ui_create_screen();
    uint32_t i;

    s_on_select = on_select;
    build_status_bar(screen);
    for(i = 0; i < MACHINE_COUNT; i++) {
        build_card(screen, i);
    }
    gallery_view_update();
    return screen;
}

void gallery_view_update(void)
{
    if(s_clock != NULL) {
        ui_update_clock(s_clock);
    }
}
