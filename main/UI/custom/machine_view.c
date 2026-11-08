/*
 * Machine detail page (800 x 480).
 *
 *  +-- ‹ 返回 ------------- 03  覆膜机 ------------- clock  wifi --+
 *  | BMS: SOC ring, pack metrics    | Machine photo and name        |
 *  +--------------------------------+-------------------------------+
 *  | Cells          | Temperatures  | Other information             |
 *  +--------------------------------+   IMU                          |
 *  | Relays, alarm, fault, data age |   (more to be provided)        |
 *  +--------------------------------+-------------------------------+
 *
 * The left half shows the BMS, the right half everything else.
 */

#include <string.h>

#include "machine_view.h"
#include "machine_list.h"
#include "dashboard_theme.h"
#include "ui_common.h"

/*********************
 *      LAYOUT
 *********************/
#define CARD_PAD        UI_CARD_PAD
#define TITLE_Y         10

#define HALF_W          ((UI_SCREEN_W - 3 * UI_GAP) / 2)
#define LEFT_X          UI_GAP
#define RIGHT_X         (2 * UI_GAP + HALF_W)
#define BODY_Y          (UI_STATUS_BAR_H + UI_GAP)
#define BODY_BOTTOM     (UI_SCREEN_H - UI_GAP)

/* Left half: BMS */
#define PACK_H          184
#define CELLS_Y         (BODY_Y + PACK_H + UI_GAP)
#define CELLS_H         128
#define RELAY_Y         (CELLS_Y + CELLS_H + UI_GAP)
#define RELAY_H         (BODY_BOTTOM - RELAY_Y)

/* Right half: machine and other information */
#define MACHINE_H       PACK_H
#define OTHER_Y         CELLS_Y
#define OTHER_H         (BODY_BOTTOM - OTHER_Y)

/* Pack card: SOC ring on the left, 3 x 2 metrics on the right. */
#define SOC_SIZE        140
#define SOC_ARC_W       10
#define METRIC_X        (CARD_PAD + SOC_SIZE + 20)
#define METRIC_COL_W    ((HALF_W - CARD_PAD - METRIC_X) / 2)
#define METRIC_Y        34
#define METRIC_ROW_H    48

/* Cells card: two columns split by a vertical line. */
#define SPLIT_X         (HALF_W / 2)
#define SPLIT_COL_W     (SPLIT_X - 2 * CARD_PAD)

/* Relay card */
#define RELAY_SLOT_W    60
#define FIELD_W         120

/* Back button in the status bar */
#define BACK_W          84
#define BACK_H          26

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t *value;
    lv_obj_t *unit;
} metric_t;

typedef struct {
    metric_t max;
    metric_t min;
    metric_t delta;
    lv_obj_t *max_pos;
    lv_obj_t *min_pos;
} extremes_t;

typedef struct {
    lv_obj_t *led;
    lv_obj_t *label;
    uint32_t color; /* color currently shown; 0 until first set */
} lamp_t;

typedef enum {
    RELAY_CHARGE,
    RELAY_DISCHARGE,
    RELAY_PRECHARGE,
    RELAY_HEAT,
    RELAY_NEGATIVE,
    RELAY_STICK,
    RELAY_COUNT
} relay_id_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static const char *const k_relay_names[RELAY_COUNT] = {
    "充电", "放电", "预充", "加热", "负极", "粘连"
};

/* Shown while no fresh data is available; every value renders as "--". */
static const bms_data_t k_no_bms;

static struct {
    bool created;
    machine_view_back_cb_t on_back;

    /* Status bar */
    lv_obj_t *title_number;
    lv_obj_t *title_name;
    lv_obj_t *clock;

    /* BMS */
    lamp_t can;
    lv_obj_t *health;

    lv_obj_t *soc_arc;
    lv_obj_t *soc_value;
    lv_obj_t *soc_state;
    uint32_t soc_color;

    metric_t voltage;
    metric_t current;
    metric_t power;
    metric_t soh;
    metric_t capacity;
    metric_t remaining;

    extremes_t cells;
    extremes_t temps;

    lamp_t relays[RELAY_COUNT];
    lv_obj_t *alarm_state;
    lv_obj_t *fault_code;
    lv_obj_t *update_age;

    /* Machine and other information */
    lv_obj_t *machine_image;
    lv_obj_t *machine_name;

    metric_t pitch;
    metric_t roll;
    metric_t yaw;
} s_view;

static void set_lamp(lamp_t *lamp, uint32_t color);

/**********************
 *   WIDGET HELPERS
 **********************/
static lv_obj_t *create_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *title)
{
    lv_obj_t *card = ui_create_box(parent);

    ui_style_card(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);

    if(title != NULL) {
        lv_obj_t *label = ui_create_label(card, DASH_FONT_BODY, DASH_COLOR_TEXT, title);
        lv_obj_set_pos(label, CARD_PAD, TITLE_Y);
    }
    return card;
}

/* Caption on top, value and unit below. */
static metric_t create_stacked_metric(lv_obj_t *parent, int32_t x, int32_t y, const char *caption, const char *unit)
{
    metric_t m;
    lv_obj_t *row;
    lv_obj_t *label = ui_create_label(parent, DASH_FONT_SMALL, DASH_COLOR_MUTED, caption);

    lv_obj_set_pos(label, x, y);
    row = ui_create_row(parent, x, y + 20, LV_FLEX_ALIGN_END);
    m.value = ui_create_label(row, DASH_FONT_VALUE, DASH_COLOR_TEXT, "--");
    m.unit = ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_MUTED, unit);
    ui_match_baseline(m.unit, DASH_FONT_SMALL, DASH_FONT_VALUE);
    return m;
}

/* Caption, value and unit on one line. */
static metric_t create_inline_metric(lv_obj_t *parent, int32_t x, int32_t y, const char *caption,
                                     int32_t caption_w, const lv_font_t *value_font, const char *unit)
{
    metric_t m;
    lv_obj_t *row = ui_create_row(parent, x, y, LV_FLEX_ALIGN_END);
    lv_obj_t *label = ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_MUTED, caption);

    lv_obj_set_width(label, caption_w);
    ui_match_baseline(label, DASH_FONT_SMALL, value_font);
    m.value = ui_create_label(row, value_font, DASH_COLOR_TEXT, "--");
    m.unit = ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_MUTED, unit);
    ui_match_baseline(m.unit, DASH_FONT_SMALL, value_font);
    return m;
}

/* Small caption followed by a plain value on one line. Returns the value label. */
static lv_obj_t *create_field(lv_obj_t *parent, int32_t x, int32_t y, const char *caption)
{
    lv_obj_t *row = ui_create_row(parent, x, y, LV_FLEX_ALIGN_END);
    lv_obj_t *label = ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_MUTED, caption);
    lv_obj_t *value;

    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    ui_match_baseline(label, DASH_FONT_SMALL, DASH_FONT_BODY);
    value = ui_create_label(row, DASH_FONT_BODY, DASH_COLOR_TEXT, "--");
    return value;
}

static lamp_t create_lamp(lv_obj_t *parent, lv_obj_t *led, const lv_font_t *font, const char *text)
{
    lamp_t lamp;

    if(led == NULL) {
        led = lv_led_create(parent);
    }
    else {
        lv_obj_set_parent(led, parent);
    }
    lv_obj_set_size(led, 10, 10);
    lv_led_on(led);
    /* Flat dot: the theme default gives every LED a blurry glow. */
    lv_obj_set_style_shadow_width(led, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(led, 0, LV_PART_MAIN);
    lamp.led = led;
    lamp.label = ui_create_label(parent, font, DASH_COLOR_MUTED, text);
    lamp.color = 0;
    set_lamp(&lamp, DASH_COLOR_OFF);
    return lamp;
}

/**********************
 *   UPDATE HELPERS
 **********************/
static void set_lamp(lamp_t *lamp, uint32_t color)
{
    if(lamp->color == color) {
        return;
    }
    lamp->color = color;
    lv_led_set_color(lamp->led, lv_color_hex(color));
}

/* Formats a fixed-point number without needing float support in printf. */
static void format_fixed(char *buf, size_t size, float value, uint32_t decimals)
{
    static const uint32_t scales[] = {1U, 10U, 100U, 1000U};
    uint32_t scale = scales[decimals > 3U ? 3U : decimals];
    float magnitude = value < 0.0f ? -value : value;
    uint32_t scaled;
    unsigned long whole;
    unsigned long frac;
    const char *sign;

    if(magnitude > 1000000.0f) {
        magnitude = 1000000.0f;
    }
    scaled = (uint32_t)(magnitude * (float)scale + 0.5f);
    whole = (unsigned long)(scaled / scale);
    frac = (unsigned long)(scaled % scale);
    sign = (value < 0.0f && scaled != 0U) ? "-" : "";

    switch(scale) {
        case 10U:
            lv_snprintf(buf, size, "%s%lu.%01lu", sign, whole, frac);
            break;
        case 100U:
            lv_snprintf(buf, size, "%s%lu.%02lu", sign, whole, frac);
            break;
        case 1000U:
            lv_snprintf(buf, size, "%s%lu.%03lu", sign, whole, frac);
            break;
        default:
            lv_snprintf(buf, size, "%s%lu", sign, whole);
            break;
    }
}

static void show_fixed(lv_obj_t *label, bool valid, float value, uint32_t decimals, uint32_t color)
{
    char buf[16];

    if(!valid) {
        ui_set_text(label, "--");
        ui_set_text_color(label, DASH_COLOR_MUTED);
        return;
    }
    format_fixed(buf, sizeof(buf), value, decimals);
    ui_set_text(label, buf);
    ui_set_text_color(label, color);
}

static void show_position(lv_obj_t *label, bool valid, uint8_t position)
{
    char buf[8];

    if(!valid) {
        ui_set_text(label, "--");
        return;
    }
    lv_snprintf(buf, sizeof(buf), "#%u", (unsigned)position);
    ui_set_text(label, buf);
}

static void show_status(lv_obj_t *label, const char *text, uint32_t color)
{
    ui_set_text(label, text);
    ui_set_text_color(label, color);
}

static uint32_t soc_color(float soc)
{
    if(soc < DASH_SOC_LOW_PERCENT) {
        return DASH_COLOR_DANGER;
    }
    if(soc < DASH_SOC_MID_PERCENT) {
        return DASH_COLOR_WARN;
    }
    return DASH_COLOR_ACCENT;
}

static bool any_relay_stuck(const bms_data_t *d)
{
    return d->relay_charge_stick || d->relay_discharge_stick || d->relay_negative_stick;
}

/**********************
 *   BUILD: STATUS BAR
 **********************/
/* The IMU values move into the "other information" card. */
static void remove_guider_imu_panel(lv_ui *ui)
{
    if(ui->screen_IMU == NULL) {
        return;
    }
    lv_obj_delete(ui->screen_IMU);
    ui->screen_IMU = NULL;
    ui->screen_imu_status = NULL;
    ui->screen_pitch = NULL;
    ui->screen_pitch_data = NULL;
    ui->screen_roll = NULL;
    ui->screen_roll_data = NULL;
    ui->screen_yaw = NULL;
    ui->screen_yaw_data = NULL;
}

static void on_back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_view.on_back != NULL) {
        s_view.on_back();
    }
}

static void build_back_button(lv_obj_t *bar)
{
    lv_obj_t *button = ui_create_box(bar);
    lv_obj_t *row;

    lv_obj_set_size(button, BACK_W, BACK_H);
    lv_obj_align(button, LV_ALIGN_LEFT_MID, UI_GAP, 0);
    /* Outline pill, like DJI's secondary buttons. */
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(DASH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    ui_set_clickable(button, true);
    /* Easier to hit with a finger than the drawn button. */
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_add_event_cb(button, on_back_clicked, LV_EVENT_CLICKED, NULL);

    row = ui_create_row(button, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_center(row);
    ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_TEXT, LV_SYMBOL_LEFT);
    ui_create_label(row, DASH_FONT_BODY, DASH_COLOR_TEXT, "返回");
}

static void build_status_bar(lv_ui *ui)
{
    lv_obj_t *bar = ui->screen_StatusBar;
    lv_obj_t *title;

    ui_make_static(bar);
    ui_style_status_bar(bar);

    build_back_button(bar);

    /* "03  覆膜机", filled in by machine_view_set_machine(). */
    title = ui_create_row(bar, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title, 8, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    s_view.title_number = ui_create_label(title, DASH_FONT_SMALL, DASH_COLOR_ACCENT, "--");
    s_view.title_name = ui_create_label(title, DASH_FONT_BODY, DASH_COLOR_TEXT, "--");

    s_view.clock = ui->screen_time;
    lv_obj_set_size(s_view.clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_view.clock, DASH_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_view.clock, lv_color_hex(DASH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_view.clock, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_view.clock, "--:--:--");
    lv_obj_align(s_view.clock, LV_ALIGN_RIGHT_MID, -44, 0);

    ui_make_static(ui->screen_wifi);
    ui_tint_icon(ui->screen_wifi, DASH_COLOR_TEXT);
    lv_obj_align(ui->screen_wifi, LV_ALIGN_RIGHT_MID, -4, 0);
}

/**********************
 *   BUILD: BMS (LEFT)
 **********************/
static void build_pack_card(lv_ui *ui)
{
    lv_obj_t *card = create_card(ui->screen, LEFT_X, BODY_Y, HALF_W, PACK_H, "电池 BMS");
    lv_obj_t *status;
    lv_obj_t *spacer;
    lv_obj_t *arc;
    lv_obj_t *row;
    lv_obj_t *percent;
    int32_t col2 = METRIC_X + METRIC_COL_W;

    /* Title row, right side: "● CAN 在线   ✓ 正常". Reuses the GUI Guider LED. */
    status = ui_create_row(card, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, LV_PART_MAIN);
    s_view.can = create_lamp(status, ui->screen_led_1, DASH_FONT_SMALL, "CAN 离线");
    spacer = ui_create_box(status);
    lv_obj_set_size(spacer, 6, 1);
    s_view.health = ui_create_label(status, DASH_FONT_SMALL, DASH_COLOR_MUTED, "--");
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, -CARD_PAD, TITLE_Y + 2);

    /* SOC ring */
    arc = lv_arc_create(card);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    ui_make_static(arc);
    lv_obj_set_size(arc, SOC_SIZE, SOC_SIZE);
    lv_obj_set_pos(arc, CARD_PAD, 36);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, SOC_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, SOC_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(DASH_COLOR_OFF), LV_PART_INDICATOR);
    s_view.soc_arc = arc;
    s_view.soc_color = DASH_COLOR_OFF;

    row = ui_create_row(arc, 0, 0, LV_FLEX_ALIGN_END);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -8);
    s_view.soc_value = ui_create_label(row, DASH_FONT_HERO, DASH_COLOR_TEXT, "--");
    percent = ui_create_label(row, DASH_FONT_BODY, DASH_COLOR_MUTED, "%");
    ui_match_baseline(percent, DASH_FONT_BODY, DASH_FONT_HERO);

    s_view.soc_state = ui_create_label(arc, DASH_FONT_SMALL, DASH_COLOR_MUTED, "SOC");
    lv_obj_align(s_view.soc_state, LV_ALIGN_CENTER, 0, 28);

    /* Metrics, 3 rows x 2 columns */
    s_view.voltage = create_stacked_metric(card, METRIC_X, METRIC_Y, "总电压", "V");
    s_view.current = create_stacked_metric(card, col2, METRIC_Y, "总电流", "A");
    s_view.power = create_stacked_metric(card, METRIC_X, METRIC_Y + METRIC_ROW_H, "功率", "kW");
    s_view.soh = create_stacked_metric(card, col2, METRIC_Y + METRIC_ROW_H, "健康度", "%");
    s_view.capacity = create_stacked_metric(card, METRIC_X, METRIC_Y + 2 * METRIC_ROW_H, "满充容量", "Ah");
    s_view.remaining = create_stacked_metric(card, col2, METRIC_Y + 2 * METRIC_ROW_H, "剩余容量", "Ah");
}

static void build_extremes(lv_obj_t *card, int32_t x, const char *title, const char *unit,
                           const char *delta_caption, const char *delta_unit, extremes_t *out)
{
    static const int32_t row_y[] = {34, 64, 94};
    /* TOP_RIGHT offset that puts the right edge at the end of this column. */
    int32_t right_ofs = x + SPLIT_COL_W - HALF_W;
    /* Lines up the small position text with the value baseline. */
    int32_t pos_dy = (DASH_FONT_VALUE->line_height - DASH_FONT_VALUE->base_line) -
                     (DASH_FONT_SMALL->line_height - DASH_FONT_SMALL->base_line);
    lv_obj_t *label = ui_create_label(card, DASH_FONT_BODY, DASH_COLOR_TEXT, title);

    lv_obj_set_pos(label, x, TITLE_Y);
    out->max = create_inline_metric(card, x, row_y[0], "最高", 36, DASH_FONT_VALUE, unit);
    out->min = create_inline_metric(card, x, row_y[1], "最低", 36, DASH_FONT_VALUE, unit);
    out->delta = create_inline_metric(card, x, row_y[2], delta_caption, 36, DASH_FONT_VALUE, delta_unit);

    out->max_pos = ui_create_label(card, DASH_FONT_SMALL, DASH_COLOR_MUTED, "--");
    lv_obj_align(out->max_pos, LV_ALIGN_TOP_RIGHT, right_ofs, row_y[0] + pos_dy);
    out->min_pos = ui_create_label(card, DASH_FONT_SMALL, DASH_COLOR_MUTED, "--");
    lv_obj_align(out->min_pos, LV_ALIGN_TOP_RIGHT, right_ofs, row_y[1] + pos_dy);
}

static void build_cells_card(lv_obj_t *screen)
{
    lv_obj_t *card = create_card(screen, LEFT_X, CELLS_Y, HALF_W, CELLS_H, NULL);

    build_extremes(card, CARD_PAD, "电芯电压", "V", "压差", "mV", &s_view.cells);
    ui_create_divider(card, SPLIT_X, 12, 1, CELLS_H - 24);
    build_extremes(card, SPLIT_X + CARD_PAD, "温度", "°C", "温差", "°C", &s_view.temps);
}

static void build_relay_card(lv_obj_t *screen)
{
    lv_obj_t *card = create_card(screen, LEFT_X, RELAY_Y, HALF_W, RELAY_H, "继电器与告警");
    uint32_t i;

    for(i = 0; i < RELAY_COUNT; i++) {
        int32_t x = CARD_PAD + (int32_t)i * RELAY_SLOT_W;
        lv_obj_t *row = ui_create_row(card, x, 36, LV_FLEX_ALIGN_CENTER);

        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        s_view.relays[i] = create_lamp(row, NULL, DASH_FONT_BODY, k_relay_names[i]);
    }

    s_view.alarm_state = create_field(card, CARD_PAD, 68, "告警");
    s_view.fault_code = create_field(card, CARD_PAD + FIELD_W, 68, "故障码");
    s_view.update_age = create_field(card, CARD_PAD + 2 * FIELD_W, 68, "更新");
}

/**********************
 *   BUILD: OTHER (RIGHT)
 **********************/
static void build_machine_card(lv_obj_t *screen)
{
    lv_obj_t *card = create_card(screen, RIGHT_X, BODY_Y, HALF_W, MACHINE_H, "设备");

    s_view.machine_image = lv_image_create(card);
    lv_obj_align(s_view.machine_image, LV_ALIGN_TOP_MID, 0, 30);

    s_view.machine_name = ui_create_label(card, DASH_FONT_VALUE, DASH_COLOR_TEXT, "--");
    lv_obj_align(s_view.machine_name, LV_ALIGN_TOP_MID, 0, 142);
}

static void build_other_card(lv_obj_t *screen)
{
    lv_obj_t *card = create_card(screen, RIGHT_X, OTHER_Y, HALF_W, OTHER_H, "其他信息");
    lv_obj_t *label;
    int32_t placeholder_y;

    label = ui_create_label(card, DASH_FONT_SMALL, DASH_COLOR_MUTED, "姿态 IMU");
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -CARD_PAD, TITLE_Y + 2);

    s_view.pitch = create_stacked_metric(card, CARD_PAD, 36, "俯仰", "°");
    s_view.roll = create_stacked_metric(card, CARD_PAD + FIELD_W, 36, "横滚", "°");
    s_view.yaw = create_stacked_metric(card, CARD_PAD + 2 * FIELD_W, 36, "偏航", "°");

    ui_create_divider(card, CARD_PAD, 96, HALF_W - 2 * CARD_PAD, 1);

    /* Space reserved for the information that will be added later. */
    placeholder_y = 96 + (OTHER_H - 96) / 2 - OTHER_H / 2;
    label = ui_create_label(card, DASH_FONT_BODY, DASH_COLOR_MUTED, "更多信息待提供");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, placeholder_y);
}

void machine_view_create(lv_ui *ui, machine_view_back_cb_t on_back)
{
    lv_obj_t *screen = ui->screen;

    if(s_view.created) {
        return;
    }
    s_view.on_back = on_back;

    lv_obj_set_style_bg_color(screen, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    ui_set_scrollable(screen, false);
    remove_guider_imu_panel(ui);

    build_status_bar(ui);
    build_pack_card(ui);
    build_cells_card(screen);
    build_relay_card(screen);
    build_machine_card(screen);
    build_other_card(screen);

    s_view.created = true;
    machine_view_set_machine(0);
}

void machine_view_set_machine(uint32_t machine_index)
{
    const machine_info_t *m = machine_get(machine_index);
    char number[4];

    if(!s_view.created || m == NULL) {
        return;
    }
    lv_snprintf(number, sizeof(number), "%02u", (unsigned)(machine_index + 1U));
    ui_set_text(s_view.title_number, number);
    ui_set_text(s_view.title_name, m->name);
    ui_set_text(s_view.machine_name, m->name);
    lv_image_set_src(s_view.machine_image, m->image);
}

/**********************
 *   UPDATE
 **********************/
static void update_status(const bms_data_t *d, bool online)
{
    set_lamp(&s_view.can, online ? DASH_COLOR_OK : DASH_COLOR_DANGER);
    show_status(s_view.can.label, online ? "CAN 在线" : "CAN 离线", online ? DASH_COLOR_TEXT : DASH_COLOR_DANGER);

    if(!online) {
        show_status(s_view.health, "--", DASH_COLOR_MUTED);
    }
    else if(d->has_alarm || d->fault_code != 0U || any_relay_stuck(d)) {
        show_status(s_view.health, LV_SYMBOL_WARNING " 异常", DASH_COLOR_WARN);
    }
    else {
        show_status(s_view.health, LV_SYMBOL_OK " 正常", DASH_COLOR_OK);
    }
}

static void update_soc(const bms_data_t *d, bool online)
{
    float soc = d->soc_percent < 0.0f ? 0.0f : (d->soc_percent > 100.0f ? 100.0f : d->soc_percent);
    uint32_t color = online ? soc_color(soc) : DASH_COLOR_OFF;

    /* "100" instead of "100.0" so a full pack still fits inside the ring. */
    show_fixed(s_view.soc_value, online, soc, soc >= 99.95f ? 0U : 1U, DASH_COLOR_TEXT);
    lv_arc_set_value(s_view.soc_arc, online ? (int32_t)(soc * 10.0f + 0.5f) : 0);
    if(s_view.soc_color != color) {
        s_view.soc_color = color;
        lv_obj_set_style_arc_color(s_view.soc_arc, lv_color_hex(color), LV_PART_INDICATOR);
    }

    if(!online) {
        show_status(s_view.soc_state, "SOC", DASH_COLOR_MUTED);
    }
    else if(soc < DASH_SOC_LOW_PERCENT) {
        show_status(s_view.soc_state, "电量过低", color);
    }
    else if(soc < DASH_SOC_MID_PERCENT) {
        show_status(s_view.soc_state, "电量偏低", color);
    }
    else {
        show_status(s_view.soc_state, "电量正常", color);
    }
}

static void update_pack(const bms_data_t *d, bool online)
{
    show_fixed(s_view.voltage.value, online, d->pack_voltage_v, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.current.value, online, d->pack_current_a, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.power.value, online, d->pack_voltage_v * d->pack_current_a / 1000.0f, 2, DASH_COLOR_TEXT);
    show_fixed(s_view.soh.value, online, d->soh_percent, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.capacity.value, online, d->full_charge_capacity_ah, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.remaining.value, online, d->full_charge_capacity_ah * d->soc_percent / 100.0f, 1,
               DASH_COLOR_TEXT);
}

static void update_cells(const bms_data_t *d, bool online)
{
    float delta_mv = (d->max_cell_voltage_v - d->min_cell_voltage_v) * 1000.0f;
    bool warn;

    if(delta_mv < 0.0f) {
        delta_mv = 0.0f;
    }
    warn = delta_mv > (float)DASH_CELL_DELTA_WARN_MV;

    show_fixed(s_view.cells.max.value, online, d->max_cell_voltage_v, 3, DASH_COLOR_TEXT);
    show_fixed(s_view.cells.min.value, online, d->min_cell_voltage_v, 3, DASH_COLOR_TEXT);
    show_fixed(s_view.cells.delta.value, online, delta_mv, 0, warn ? DASH_COLOR_WARN : DASH_COLOR_TEXT);
    show_position(s_view.cells.max_pos, online, d->max_cell_position);
    show_position(s_view.cells.min_pos, online, d->min_cell_position);
}

static void update_temperatures(const bms_data_t *d, bool online)
{
    float delta = d->max_temperature_c - d->min_temperature_c;
    bool warn;

    if(delta < 0.0f) {
        delta = 0.0f;
    }
    warn = delta > DASH_TEMP_DELTA_WARN_C;

    show_fixed(s_view.temps.max.value, online, d->max_temperature_c, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.temps.min.value, online, d->min_temperature_c, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.temps.delta.value, online, delta, 1, warn ? DASH_COLOR_WARN : DASH_COLOR_TEXT);
    show_position(s_view.temps.max_pos, online, d->max_temperature_position);
    show_position(s_view.temps.min_pos, online, d->min_temperature_position);
}

static void update_relays(const bms_data_t *d)
{
    const bool active[RELAY_COUNT] = {
        d->relay_charge, d->relay_discharge, d->relay_precharge,
        d->relay_heat, d->relay_negative, any_relay_stuck(d)
    };
    uint32_t i;

    for(i = 0; i < RELAY_COUNT; i++) {
        bool fault = (i == RELAY_STICK);
        uint32_t lamp = active[i] ? (fault ? DASH_COLOR_DANGER : DASH_COLOR_OK) : DASH_COLOR_OFF;
        uint32_t text = active[i] ? (fault ? DASH_COLOR_DANGER : DASH_COLOR_TEXT) : DASH_COLOR_MUTED;

        set_lamp(&s_view.relays[i], lamp);
        ui_set_text_color(s_view.relays[i].label, text);
    }
}

static void update_alarm(const bms_data_t *bms, bool online)
{
    char buf[24];

    if(online) {
        show_status(s_view.alarm_state, bms->has_alarm ? "有告警" : "无告警",
                    bms->has_alarm ? DASH_COLOR_WARN : DASH_COLOR_OK);
        lv_snprintf(buf, sizeof(buf), "0x%02X", (unsigned)bms->fault_code);
        show_status(s_view.fault_code, buf, bms->fault_code != 0U ? DASH_COLOR_WARN : DASH_COLOR_TEXT);
    }
    else {
        show_status(s_view.alarm_state, "--", DASH_COLOR_MUTED);
        show_status(s_view.fault_code, "--", DASH_COLOR_MUTED);
    }

    /* The data age stays visible while offline to show how long the link is down. */
    if(bms == NULL) {
        show_status(s_view.update_age, "--", DASH_COLOR_MUTED);
        return;
    }
    {
        uint32_t age_ms = lv_tick_get() - bms->last_update_ms;
        if(age_ms < 1000U) {
            lv_snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)age_ms);
        }
        else {
            format_fixed(buf, sizeof(buf) - 2U, (float)age_ms / 1000.0f, 1);
            strcat(buf, " s");
        }
        show_status(s_view.update_age, buf, online ? DASH_COLOR_TEXT : DASH_COLOR_DANGER);
    }
}

static void update_imu(const imu_data_t *imu)
{
    bool valid = imu != NULL;

    show_fixed(s_view.pitch.value, valid, valid ? imu->pitch_deg : 0.0f, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.roll.value, valid, valid ? imu->roll_deg : 0.0f, 1, DASH_COLOR_TEXT);
    show_fixed(s_view.yaw.value, valid, valid ? imu->yaw_deg : 0.0f, 1, DASH_COLOR_TEXT);
}

void machine_view_update(const bms_data_t *bms, const imu_data_t *imu)
{
    bool online;
    const bms_data_t *d;

    if(!s_view.created) {
        return;
    }

    online = bms != NULL && (uint32_t)(lv_tick_get() - bms->last_update_ms) <= DASH_OFFLINE_TIMEOUT_MS;
    d = online ? bms : &k_no_bms;

    ui_update_clock(s_view.clock);
    update_status(d, online);
    update_soc(d, online);
    update_pack(d, online);
    update_cells(d, online);
    update_temperatures(d, online);
    update_relays(d);
    update_alarm(bms, online);
    update_imu(imu);
}
