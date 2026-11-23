/*
 * Machine detail page (800 x 480 base, scaled).
 *
 *  +-- ‹ 返回 ------------- 06  多功能作业车 ------ ● MQTT  clock  wifi --+
 *  | 设备                       #06 | 远程控制指令        10:51:05 · 刚刚 |
 *  |                                | +---------------+ +---------------+ |
 *  |                                | | ▶  开始作业    | | ■  停止作业    | |
 *  |          [ 大尺寸农机图 ]       | +---------------+ +---------------+ |
 *  |                                | | ⌂  回停车场    | | ⚙  维护模式    | |
 *  |                                | +---------------+ +---------------+ |
 *  |                                | | ↑  向前行驶    | | ↓  向后行驶    | |
 *  |                                | +---------------+ +---------------+ |
 *  |          多功能作业车           | | ←  向左行驶    | | →  向右行驶    | |
 *  +--------------------------------+-------------------------------------+
 *
 * Left half: agricultural machine photo and name.
 * Right half: 8 MQTT control commands in a 2 x 4 grid.
 * When a new MQTT command arrives, its card expands smoothly to a large hero
 * card (~656 x 384, centered over the body) and stays enlarged until tapped
 * (on the card or outside area), then shrinks back to its highlighted slot in
 * the 8-command grid. New commands arriving while enlarged switch directly.
 */

#include <string.h>
#include <time.h>

#include "machine_view.h"
#include "machine_list.h"
#include "dashboard_theme.h"
#include "ui_common.h"

/*********************
 *      LAYOUT
 *********************/
#define CARD_PAD            UI_CARD_PAD
#define TITLE_Y             UI_SY(11)

#define HALF_W              ((UI_SCREEN_W - 3 * UI_GAP) / 2)
#define LEFT_X              UI_GAP
#define RIGHT_X             (2 * UI_GAP + HALF_W)
#define BODY_Y              (UI_STATUS_BAR_H + UI_GAP)
#define BODY_BOTTOM         (UI_SCREEN_H - UI_GAP)
#define BODY_H              (BODY_BOTTOM - BODY_Y)

/* Back button in the status bar */
#define BACK_W              UI_SU(84)
#define BACK_H              UI_SY(26)

/* Right half: 2 columns x 4 rows MQTT command grid */
#define CMD_TILE_COUNT      8U
#define CMD_COLS            2
#define CMD_ROWS            4
#define CMD_PAD_X           UI_SU(14)
#define CMD_TOP             UI_SY(44)
#define CMD_BOTTOM          UI_SY(14)
#define CMD_GAP_X           UI_SU(10)
#define CMD_GAP_Y           UI_SY(10)

#define CMD_TILE_W          ((HALF_W - 2 * CMD_PAD_X - CMD_GAP_X) / CMD_COLS)
#define CMD_TILE_H          ((BODY_H - CMD_TOP - CMD_BOTTOM - (CMD_ROWS - 1) * CMD_GAP_Y) / CMD_ROWS)
#define CMD_ICON_SZ         UI_SU(38)

/* Large expanded hero card (centered over the body, below status bar) */
#define SCRIM_H             (UI_SCREEN_H - UI_STATUS_BAR_H)
#define HERO_W              UI_SX(656)
#define HERO_H              UI_SY(384)
#define HERO_X              ((UI_SCREEN_W - HERO_W) / 2)
#define HERO_Y              (UI_GAP + (BODY_H - HERO_H) / 2) /* relative to scrim top */
#define HERO_PAD_X          UI_SU(28)
#define HERO_ICON_SZ        UI_SU(132)
#define HERO_SCRIM_OPA      LV_OPA_20

/* Animation timing (ms) and normalized progress range */
#define HERO_ANIM_EXPAND_MS 320U
#define HERO_ANIM_SHRINK_MS 240U
#define HERO_ANIM_PULSE_MS  260U
#define HERO_ANIM_MAX       1000
#define HERO_PULSE_FROM     860

/* Timestamps before 2024-01-01 00:00:00 UTC (ms) are treated as unset. */
#define VALID_EPOCH_MS      1704067200000LL

/* FontAwesome 5 solid directional arrows (included by tools/gen_fonts.sh). */
#define SYM_ARROW_LEFT      "\xEF\x81\xA0" /* U+F060 */
#define SYM_ARROW_RIGHT     "\xEF\x81\xA1" /* U+F061 */
#define SYM_ARROW_UP        "\xEF\x81\xA2" /* U+F062 */
#define SYM_ARROW_DOWN      "\xEF\x81\xA3" /* U+F063 */

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    mqtt_cmd_action_t action;
    const char *title;
    const char *proto;
    const char *icon;
    uint32_t fg;
    uint32_t bg;
    uint32_t border;
} cmd_spec_t;

typedef enum {
    TILE_VISUAL_IDLE = 0,   /* No command received yet: balanced colored icon */
    TILE_VISUAL_DIM,        /* Another command is active: subdued neutral */
    TILE_VISUAL_ACTIVE,     /* Received command: highlighted in the 8-grid */
} tile_visual_state_t;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *icon_bg;
    lv_obj_t *icon_label;
    lv_obj_t *title_label;
    lv_obj_t *sub_label;
    int32_t base_x;
    int32_t base_y;
    tile_visual_state_t visual_state;
    bool visual_initialized;
} cmd_tile_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} rect_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static const cmd_spec_t k_cmd_specs[CMD_TILE_COUNT] = {
    {
        MQTT_CMD_ACTION_START, "开始作业", "start", LV_SYMBOL_PLAY,
        DASH_COLOR_OK, DASH_COLOR_OK_BG, DASH_COLOR_OK_BORDER
    },
    {
        MQTT_CMD_ACTION_STOP, "停止作业", "stop", LV_SYMBOL_STOP,
        DASH_COLOR_DANGER, DASH_COLOR_DANGER_BG, DASH_COLOR_DANGER_BORDER
    },
    {
        MQTT_CMD_ACTION_RESET, "回停车场", "reset", LV_SYMBOL_HOME,
        DASH_COLOR_ACCENT, DASH_COLOR_ACCENT_BG, DASH_COLOR_ACCENT_BORDER
    },
    {
        MQTT_CMD_ACTION_MAINTENANCE, "维护模式", "maintenance", LV_SYMBOL_SETTINGS,
        DASH_COLOR_WARN, DASH_COLOR_WARN_BG, DASH_COLOR_WARN_BORDER
    },
    {
        MQTT_CMD_ACTION_FORWARD, "向前行驶", "forward", SYM_ARROW_UP,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    {
        MQTT_CMD_ACTION_BACKWARD, "向后行驶", "backward", SYM_ARROW_DOWN,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    {
        MQTT_CMD_ACTION_LEFT, "向左行驶", "left", SYM_ARROW_LEFT,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    {
        MQTT_CMD_ACTION_RIGHT, "向右行驶", "right", SYM_ARROW_RIGHT,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
};

static struct {
    bool created;
    machine_view_back_cb_t on_back;
    machine_view_wifi_cb_t on_wifi;
    machine_view_mqtt_cb_t on_mqtt;
    uint32_t machine_index;

    /* Status bar */
    lv_obj_t *title_number;
    lv_obj_t *title_name;
    lv_obj_t *clock;
    ui_mqtt_pill_t mqtt_pill;

    /* Left half: machine showcase */
    lv_obj_t *machine_badge_label;
    lv_obj_t *machine_image;
    lv_obj_t *machine_name;

    /* Right half: 8 MQTT command tiles */
    lv_obj_t *cmd_card;
    lv_obj_t *cmd_status;
    cmd_tile_t tiles[CMD_TILE_COUNT];

    /* Large expanded command card overlay (covers body area, below status bar) */
    lv_obj_t *hero_scrim;
    lv_obj_t *hero_card;
    lv_obj_t *hero_content;
    lv_obj_t *hero_machine_badge;
    lv_obj_t *hero_machine_name;
    lv_obj_t *hero_time;
    lv_obj_t *hero_divider;
    lv_obj_t *hero_icon_bg;
    lv_obj_t *hero_icon;
    lv_obj_t *hero_title;
    lv_obj_t *hero_rx_pill;
    lv_obj_t *hero_rx_label;
    lv_obj_t *hero_proto;

    bool hero_expanded;
    bool hero_pulse_mode;
    int32_t hero_anim_t;
    rect_t hero_origin;
    rect_t hero_cur_rect;

    /* Active command state */
    bool has_cmd;
    int32_t active_tile; /* -1 when idle or custom OTHER action */
    mqtt_cmd_data_t cmd;
} s_view;

static void expand_hero(int32_t tile_idx, bool animate);
static void collapse_hero(bool animate);

/**********************
 *   HELPERS
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

static int32_t action_to_tile_index(mqtt_cmd_action_t action)
{
    uint32_t i;

    for(i = 0; i < CMD_TILE_COUNT; i++) {
        if(k_cmd_specs[i].action == action) {
            return (int32_t)i;
        }
    }
    return -1;
}

static rect_t tile_rect_in_scrim(int32_t tile_idx)
{
    rect_t r;

    if(tile_idx < 0 || (uint32_t)tile_idx >= CMD_TILE_COUNT) {
        r.x = HERO_X + HERO_W / 4;
        r.y = HERO_Y + HERO_H / 4;
        r.w = HERO_W / 2;
        r.h = HERO_H / 2;
        return r;
    }
    r.x = RIGHT_X + s_view.tiles[tile_idx].base_x;
    r.y = UI_GAP + s_view.tiles[tile_idx].base_y;
    r.w = CMD_TILE_W;
    r.h = CMD_TILE_H;
    return r;
}

static void format_age(uint32_t received_tick_ms, char *buf, size_t size)
{
    uint32_t age_s = (uint32_t)(lv_tick_get() - received_tick_ms) / 1000U;

    if(age_s < 3U) {
        lv_snprintf(buf, size, "刚刚");
    }
    else if(age_s < 60U) {
        lv_snprintf(buf, size, "%lu 秒前", (unsigned long)age_s);
    }
    else if(age_s < 3600U) {
        lv_snprintf(buf, size, "%lu 分钟前", (unsigned long)(age_s / 60U));
    }
    else {
        lv_snprintf(buf, size, "%lu 小时前", (unsigned long)(age_s / 3600U));
    }
}

static void format_cmd_time(char *buf, size_t size)
{
    char age_buf[24];

    format_age(s_view.cmd.received_tick_ms, age_buf, sizeof(age_buf));
    if(s_view.cmd.timestamp_ms >= VALID_EPOCH_MS) {
        time_t sec = (time_t)(s_view.cmd.timestamp_ms / 1000LL);
        const struct tm *lt = localtime(&sec);
        if(lt != NULL) {
            lv_snprintf(buf, size, "%02d:%02d:%02d · %s",
                        lt->tm_hour, lt->tm_min, lt->tm_sec, age_buf);
            return;
        }
    }
    lv_snprintf(buf, size, "%s", age_buf);
}

static void update_cmd_status_labels(void)
{
    char buf[64];

    if(!s_view.has_cmd) {
        ui_set_text(s_view.cmd_status, "暂未收到指令");
        ui_set_text_color(s_view.cmd_status, DASH_COLOR_MUTED);
        ui_set_text(s_view.hero_time, "");
        return;
    }

    if(s_view.active_tile < 0) {
        char age_buf[24];
        format_age(s_view.cmd.received_tick_ms, age_buf, sizeof(age_buf));
        lv_snprintf(buf, sizeof(buf), "自定义指令 · %s", age_buf);
        ui_set_text(s_view.cmd_status, buf);
        ui_set_text_color(s_view.cmd_status, DASH_COLOR_ACCENT);
        ui_set_text(s_view.hero_time, buf);
        return;
    }

    format_cmd_time(buf, sizeof(buf));
    ui_set_text(s_view.cmd_status, buf);
    ui_set_text_color(s_view.cmd_status, DASH_COLOR_MUTED);
    ui_set_text(s_view.hero_time, buf);
}

static void sync_hero_machine_labels(void)
{
    const machine_info_t *m = machine_get(s_view.machine_index);
    char badge[8];

    if(m == NULL) {
        return;
    }
    lv_snprintf(badge, sizeof(badge), "#%02u", (unsigned)(s_view.machine_index + 1U));
    ui_set_text(s_view.hero_machine_badge, badge);
    ui_set_text(s_view.hero_machine_name, m->name);
}

static void apply_tile_visual(uint32_t index, tile_visual_state_t state)
{
    cmd_tile_t *tile = &s_view.tiles[index];
    const cmd_spec_t *spec = &k_cmd_specs[index];

    if(tile->visual_initialized && tile->visual_state == state) {
        return;
    }
    tile->visual_initialized = true;
    tile->visual_state = state;

    if(state == TILE_VISUAL_ACTIVE) {
        lv_obj_set_style_bg_color(tile->box, lv_color_hex(spec->bg), LV_PART_MAIN);
        lv_obj_set_style_border_color(tile->box, lv_color_hex(spec->fg), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile->box, 2, LV_PART_MAIN);

        lv_obj_set_style_bg_color(tile->icon_bg, lv_color_hex(spec->fg), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile->icon_bg, 0, LV_PART_MAIN);
        ui_set_text_color(tile->icon_label, 0xFFFFFF);

        ui_set_text_color(tile->title_label, spec->fg);
        ui_set_text(tile->sub_label, "● 已接收");
        ui_set_text_color(tile->sub_label, spec->fg);
        ui_set_clickable(tile->box, true);
    }
    else {
        bool dimmed = (state == TILE_VISUAL_DIM);
        uint32_t icon_bg_col = dimmed ? DASH_COLOR_NEUTRAL_BG : spec->bg;
        uint32_t icon_bd_col = dimmed ? DASH_COLOR_NEUTRAL_BORDER : spec->border;
        uint32_t icon_fg_col = dimmed ? DASH_COLOR_MUTED : spec->fg;
        uint32_t title_col = dimmed ? DASH_COLOR_MUTED : DASH_COLOR_TEXT;

        lv_obj_set_style_bg_color(tile->box, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
        lv_obj_set_style_border_color(tile->box, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile->box, 1, LV_PART_MAIN);

        lv_obj_set_style_bg_color(tile->icon_bg, lv_color_hex(icon_bg_col), LV_PART_MAIN);
        lv_obj_set_style_border_color(tile->icon_bg, lv_color_hex(icon_bd_col), LV_PART_MAIN);
        lv_obj_set_style_border_width(tile->icon_bg, 1, LV_PART_MAIN);
        ui_set_text_color(tile->icon_label, icon_fg_col);

        ui_set_text_color(tile->title_label, title_col);
        ui_set_text(tile->sub_label, spec->proto);
        ui_set_text_color(tile->sub_label, DASH_COLOR_MUTED);
        ui_set_clickable(tile->box, false);
    }
}

static void apply_hero_spec(int32_t tile_idx)
{
    const cmd_spec_t *spec;

    if(tile_idx < 0 || (uint32_t)tile_idx >= CMD_TILE_COUNT) {
        return;
    }
    spec = &k_cmd_specs[tile_idx];

    lv_obj_set_style_bg_color(s_view.hero_card, lv_color_hex(spec->bg), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_view.hero_card, lv_color_hex(spec->fg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.hero_divider, lv_color_hex(spec->border), LV_PART_MAIN);

    lv_obj_set_style_bg_color(s_view.hero_icon_bg, lv_color_hex(spec->fg), LV_PART_MAIN);
    ui_set_text(s_view.hero_icon, spec->icon);

    ui_set_text(s_view.hero_title, spec->title);
    ui_set_text_color(s_view.hero_title, spec->fg);

    lv_obj_set_style_border_color(s_view.hero_rx_pill, lv_color_hex(spec->border), LV_PART_MAIN);
    ui_set_text_color(s_view.hero_rx_label, spec->fg);

    ui_set_text(s_view.hero_proto, spec->proto);
    sync_hero_machine_labels();
    update_cmd_status_labels();
}

/**********************
 *   HERO ANIMATION
 **********************/
static void apply_hero_progress(int32_t t)
{
    int32_t x, y, w, h;
    int32_t scrim_t = t;
    lv_opa_t scrim_opa;

    if(s_view.hero_pulse_mode) {
        /* Centered elastic pulse when switching directly between commands. */
        int32_t scale_w = (HERO_W * t) / HERO_ANIM_MAX;
        int32_t scale_h = (HERO_H * t) / HERO_ANIM_MAX;
        w = scale_w < UI_SX(160) ? UI_SX(160) : scale_w;
        h = scale_h < UI_SY(100) ? UI_SY(100) : scale_h;
        x = HERO_X + (HERO_W - w) / 2;
        y = HERO_Y + (HERO_H - h) / 2;
        scrim_opa = HERO_SCRIM_OPA;
        lv_obj_set_style_opa(s_view.hero_content, LV_OPA_COVER, LV_PART_MAIN);
    }
    else {
        /* Smooth morph from/to the exact command tile rectangle on the right half. */
        x = s_view.hero_origin.x + ((HERO_X - s_view.hero_origin.x) * t) / HERO_ANIM_MAX;
        y = s_view.hero_origin.y + ((HERO_Y - s_view.hero_origin.y) * t) / HERO_ANIM_MAX;
        w = s_view.hero_origin.w + ((HERO_W - s_view.hero_origin.w) * t) / HERO_ANIM_MAX;
        h = s_view.hero_origin.h + ((HERO_H - s_view.hero_origin.h) * t) / HERO_ANIM_MAX;
        if(w < CMD_TILE_W / 2) {
            w = CMD_TILE_W / 2;
        }
        if(h < CMD_TILE_H / 2) {
            h = CMD_TILE_H / 2;
        }

        if(scrim_t < 0) {
            scrim_t = 0;
        }
        if(scrim_t > HERO_ANIM_MAX) {
            scrim_t = HERO_ANIM_MAX;
        }
        scrim_opa = (lv_opa_t)((HERO_SCRIM_OPA * scrim_t) / HERO_ANIM_MAX);

        /* Crossfade hero typography cleanly so it never crowds the small tile box. */
        if(t <= 350) {
            lv_obj_set_style_opa(s_view.hero_content, LV_OPA_TRANSP, LV_PART_MAIN);
        }
        else if(t >= 780) {
            lv_obj_set_style_opa(s_view.hero_content, LV_OPA_COVER, LV_PART_MAIN);
        }
        else {
            lv_opa_t c_opa = (lv_opa_t)(((t - 350) * LV_OPA_COVER) / (780 - 350));
            lv_obj_set_style_opa(s_view.hero_content, c_opa, LV_PART_MAIN);
        }
    }

    s_view.hero_anim_t = t;
    s_view.hero_cur_rect.x = x;
    s_view.hero_cur_rect.y = y;
    s_view.hero_cur_rect.w = w;
    s_view.hero_cur_rect.h = h;

    lv_obj_set_style_bg_opa(s_view.hero_scrim, scrim_opa, LV_PART_MAIN);
    lv_obj_set_pos(s_view.hero_card, x, y);
    lv_obj_set_size(s_view.hero_card, w, h);
}

static void hero_anim_exec_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    apply_hero_progress(value);
}

static void hero_shrink_ready_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    if(!s_view.hero_expanded) {
        ui_set_hidden(s_view.hero_scrim, true);
    }
}

static void expand_hero(int32_t tile_idx, bool animate)
{
    lv_anim_t a;
    bool already_open = s_view.hero_expanded && !ui_is_hidden(s_view.hero_scrim);

    if(tile_idx < 0 || (uint32_t)tile_idx >= CMD_TILE_COUNT) {
        collapse_hero(animate);
        return;
    }

    lv_anim_delete(s_view.hero_card, hero_anim_exec_cb);
    apply_hero_spec(tile_idx);
    ui_set_hidden(s_view.hero_scrim, false);
    lv_obj_move_foreground(s_view.hero_scrim);

    s_view.hero_expanded = true;

    if(!animate) {
        s_view.hero_pulse_mode = false;
        s_view.hero_origin = tile_rect_in_scrim(tile_idx);
        apply_hero_progress(HERO_ANIM_MAX);
        return;
    }

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_view.hero_card);
    lv_anim_set_exec_cb(&a, hero_anim_exec_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);

    if(already_open && s_view.hero_anim_t >= 700) {
        /* Already enlarged: switch directly with a brisk centered spring pulse. */
        s_view.hero_pulse_mode = true;
        s_view.hero_origin = tile_rect_in_scrim(tile_idx);
        lv_anim_set_values(&a, HERO_PULSE_FROM, HERO_ANIM_MAX);
        lv_anim_set_duration(&a, HERO_ANIM_PULSE_MS);
    }
    else {
        /* Fly out from the exact command tile position to the large centered card. */
        s_view.hero_pulse_mode = false;
        s_view.hero_origin = tile_rect_in_scrim(tile_idx);
        apply_hero_progress(0);
        lv_anim_set_values(&a, 0, HERO_ANIM_MAX);
        lv_anim_set_duration(&a, HERO_ANIM_EXPAND_MS);
    }
    lv_anim_start(&a);
}

static void collapse_hero(bool animate)
{
    lv_anim_t a;

    lv_anim_delete(s_view.hero_card, hero_anim_exec_cb);

    if(!animate || ui_is_hidden(s_view.hero_scrim)) {
        s_view.hero_expanded = false;
        s_view.hero_pulse_mode = false;
        s_view.hero_anim_t = 0;
        ui_set_hidden(s_view.hero_scrim, true);
        return;
    }

    s_view.hero_expanded = false;
    s_view.hero_pulse_mode = false;
    s_view.hero_origin = tile_rect_in_scrim(s_view.active_tile);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_view.hero_card);
    lv_anim_set_exec_cb(&a, hero_anim_exec_cb);
    lv_anim_set_values(&a, HERO_ANIM_MAX, 0);
    lv_anim_set_duration(&a, HERO_ANIM_SHRINK_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, hero_shrink_ready_cb);
    lv_anim_start(&a);
}

/**********************
 *   EVENTS
 **********************/
static void on_back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_view.on_back != NULL) {
        s_view.on_back();
    }
}

static void on_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_view.on_wifi != NULL) {
        s_view.on_wifi();
    }
}

static void on_mqtt_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_view.on_mqtt != NULL) {
        s_view.on_mqtt();
    }
}

static void on_hero_scrim_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_view.hero_expanded) {
        collapse_hero(true);
    }
}

static void on_tile_clicked(lv_event_t *e)
{
    int32_t idx = (int32_t)(intptr_t)lv_event_get_user_data(e);

    if(s_view.has_cmd && idx == s_view.active_tile && !s_view.hero_expanded) {
        expand_hero(idx, true);
    }
}

/**********************
 *   BUILD: STATUS BAR
 **********************/
static void remove_guider_unused_widgets(lv_ui *ui)
{
    if(ui->screen_IMU != NULL) {
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
    if(ui->screen_led_1 != NULL) {
        lv_obj_delete(ui->screen_led_1);
        ui->screen_led_1 = NULL;
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
    lv_obj_set_ext_click_area(button, UI_SU(6));
    lv_obj_add_event_cb(button, on_back_clicked, LV_EVENT_CLICKED, NULL);

    row = ui_create_row(button, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_SU(6), LV_PART_MAIN);
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

    /* "06  多功能作业车", filled in by machine_view_set_machine(). */
    title = ui_create_row(bar, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title, UI_SU(8), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    s_view.title_number = ui_create_label(title, DASH_FONT_SMALL, DASH_COLOR_ACCENT, "--");
    s_view.title_name = ui_create_label(title, DASH_FONT_BODY, DASH_COLOR_TEXT, "--");

    s_view.mqtt_pill = ui_create_mqtt_pill(bar, s_view.on_mqtt != NULL ? on_mqtt_clicked : NULL);

    s_view.clock = ui->screen_time;
    lv_obj_set_size(s_view.clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_view.clock, DASH_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_view.clock, lv_color_hex(DASH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_view.clock, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_view.clock, "--:--:--");
    lv_obj_align(s_view.clock, LV_ALIGN_RIGHT_MID, -44, 0);

    ui_tint_icon(ui->screen_wifi, DASH_COLOR_TEXT);
    lv_obj_align(ui->screen_wifi, LV_ALIGN_RIGHT_MID, -4, 0);
    ui_make_icon_button(ui->screen_wifi, on_wifi_clicked);
}

/**********************
 *   BUILD: LEFT HALF
 **********************/
static void build_machine_panel(lv_obj_t *screen)
{
    lv_obj_t *card = create_card(screen, LEFT_X, BODY_Y, HALF_W, BODY_H, "设备");
    lv_obj_t *badge;

    /* Top-right machine index pill: "#06" */
    badge = ui_create_box(card);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, UI_SY(24));
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(badge, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_pad_right(badge, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(DASH_COLOR_ACCENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, lv_color_hex(DASH_COLOR_ACCENT_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -CARD_PAD, UI_SY(9));
    s_view.machine_badge_label = ui_create_label(badge, DASH_FONT_SMALL, DASH_COLOR_ACCENT, "#--");

    /* Large agricultural machine photo centered in the left half */
    s_view.machine_image = lv_image_create(card);
    lv_image_set_scale(s_view.machine_image, UI_IMG_SCALE);
    lv_obj_align(s_view.machine_image, LV_ALIGN_CENTER, 0, -UI_SY(12));

    /* Machine name anchored cleanly below the photo */
    s_view.machine_name = ui_create_label(card, DASH_FONT_VALUE, DASH_COLOR_TEXT, "--");
    lv_obj_align(s_view.machine_name, LV_ALIGN_BOTTOM_MID, 0, -UI_SY(26));
}

/**********************
 *   BUILD: RIGHT HALF
 **********************/
static void build_cmd_tile(lv_obj_t *card, uint32_t index)
{
    const cmd_spec_t *spec = &k_cmd_specs[index];
    cmd_tile_t *tile = &s_view.tiles[index];
    int32_t col = (int32_t)(index % CMD_COLS);
    int32_t row = (int32_t)(index / CMD_COLS);
    lv_obj_t *text_col;

    tile->base_x = CMD_PAD_X + col * (CMD_TILE_W + CMD_GAP_X);
    tile->base_y = CMD_TOP + row * (CMD_TILE_H + CMD_GAP_Y);

    tile->box = ui_create_box(card);
    lv_obj_set_pos(tile->box, tile->base_x, tile->base_y);
    lv_obj_set_size(tile->box, CMD_TILE_W, CMD_TILE_H);
    lv_obj_set_style_radius(tile->box, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile->box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(tile->box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile->box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(tile->box, UI_SU(16), LV_PART_MAIN);
    lv_obj_set_style_pad_right(tile->box, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_pad_column(tile->box, UI_SU(12), LV_PART_MAIN);
    lv_obj_add_event_cb(tile->box, on_tile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    tile->icon_bg = ui_create_box(tile->box);
    lv_obj_set_size(tile->icon_bg, CMD_ICON_SZ, CMD_ICON_SZ);
    lv_obj_set_style_radius(tile->icon_bg, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile->icon_bg, LV_OPA_COVER, LV_PART_MAIN);
    tile->icon_label = ui_create_label(tile->icon_bg, DASH_FONT_BODY, spec->fg, spec->icon);
    lv_obj_center(tile->icon_label);

    text_col = ui_create_box(tile->box);
    lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(text_col, UI_SY(2), LV_PART_MAIN);

    tile->title_label = ui_create_label(text_col, DASH_FONT_BODY, DASH_COLOR_TEXT, spec->title);
    tile->sub_label = ui_create_label(text_col, DASH_FONT_SMALL, DASH_COLOR_MUTED, spec->proto);

    tile->visual_initialized = false;
    apply_tile_visual(index, TILE_VISUAL_IDLE);
}

static void build_commands_panel(lv_obj_t *screen)
{
    uint32_t i;

    s_view.cmd_card = create_card(screen, RIGHT_X, BODY_Y, HALF_W, BODY_H, "远程控制指令");

    s_view.cmd_status = ui_create_label(s_view.cmd_card, DASH_FONT_SMALL, DASH_COLOR_MUTED, "暂未收到指令");
    lv_obj_align(s_view.cmd_status, LV_ALIGN_TOP_RIGHT, -CARD_PAD, TITLE_Y + UI_SY(2));

    for(i = 0; i < CMD_TILE_COUNT; i++) {
        build_cmd_tile(s_view.cmd_card, i);
    }
}

/**********************
 *   BUILD: HERO CARD
 **********************/
static void build_hero_overlay(lv_obj_t *screen)
{
    lv_obj_t *top_left;
    lv_obj_t *badge;
    lv_obj_t *center_row;
    lv_obj_t *text_col;
    lv_obj_t *meta_row;
    lv_obj_t *hint;

    /* Scrim covers the body below the status bar; tapping anywhere collapses the card. */
    s_view.hero_scrim = ui_create_box(screen);
    lv_obj_set_pos(s_view.hero_scrim, 0, UI_STATUS_BAR_H);
    lv_obj_set_size(s_view.hero_scrim, UI_SCREEN_W, SCRIM_H);
    lv_obj_set_style_bg_color(s_view.hero_scrim, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.hero_scrim, LV_OPA_TRANSP, LV_PART_MAIN);
    ui_set_clickable(s_view.hero_scrim, true);
    lv_obj_add_event_cb(s_view.hero_scrim, on_hero_scrim_clicked, LV_EVENT_CLICKED, NULL);

    /* Expanding hero card (stays non-clickable so taps on it pass to scrim). */
    s_view.hero_card = ui_create_box(s_view.hero_scrim);
    lv_obj_set_pos(s_view.hero_card, HERO_X, HERO_Y);
    lv_obj_set_size(s_view.hero_card, HERO_W, HERO_H);
    lv_obj_set_style_radius(s_view.hero_card, UI_SU(18), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.hero_card, lv_color_hex(DASH_COLOR_OK_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.hero_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_view.hero_card, lv_color_hex(DASH_COLOR_OK), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_view.hero_card, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_view.hero_card, UI_SU(36), LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(s_view.hero_card, UI_SU(2), LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s_view.hero_card, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_view.hero_card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_view.hero_card, LV_OPA_30, LV_PART_MAIN);

    /* Full-size inner content box centered inside hero_card for clean fade during morph. */
    s_view.hero_content = ui_create_box(s_view.hero_card);
    lv_obj_set_size(s_view.hero_content, HERO_W, HERO_H);
    lv_obj_center(s_view.hero_content);

    /* Header row: "#06  多功能作业车" on left, live reception time on right */
    top_left = ui_create_row(s_view.hero_content, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_left, UI_SU(10), LV_PART_MAIN);
    lv_obj_align(top_left, LV_ALIGN_TOP_LEFT, HERO_PAD_X, UI_SY(20));

    badge = ui_create_box(top_left);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, UI_SY(26));
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(badge, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_pad_right(badge, UI_SU(10), LV_PART_MAIN);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, lv_color_hex(DASH_COLOR_ACCENT_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    s_view.hero_machine_badge = ui_create_label(badge, DASH_FONT_SMALL, DASH_COLOR_ACCENT, "#--");

    s_view.hero_machine_name = ui_create_label(top_left, DASH_FONT_VALUE, DASH_COLOR_TEXT, "--");

    s_view.hero_time = ui_create_label(s_view.hero_content, DASH_FONT_BODY, DASH_COLOR_MUTED, "");
    lv_obj_align(s_view.hero_time, LV_ALIGN_TOP_RIGHT, -HERO_PAD_X, UI_SY(24));

    s_view.hero_divider = ui_create_divider(s_view.hero_content, HERO_PAD_X, UI_SY(64),
                                            HERO_W - 2 * HERO_PAD_X, 1);

    /* Center focal area: 132x132 icon circle + 64px title + status pill & protocol */
    center_row = ui_create_row(s_view.hero_content, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_row, UI_SU(36), LV_PART_MAIN);
    lv_obj_align(center_row, LV_ALIGN_CENTER, 0, UI_SY(10));

    s_view.hero_icon_bg = ui_create_box(center_row);
    lv_obj_set_size(s_view.hero_icon_bg, HERO_ICON_SZ, HERO_ICON_SZ);
    lv_obj_set_style_radius(s_view.hero_icon_bg, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.hero_icon_bg, lv_color_hex(DASH_COLOR_OK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.hero_icon_bg, LV_OPA_COVER, LV_PART_MAIN);
    s_view.hero_icon = ui_create_label(s_view.hero_icon_bg, DASH_FONT_CMD_HERO, 0xFFFFFF, LV_SYMBOL_PLAY);
    lv_obj_center(s_view.hero_icon);

    text_col = ui_create_box(center_row);
    lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(text_col, UI_SY(14), LV_PART_MAIN);

    s_view.hero_title = ui_create_label(text_col, DASH_FONT_CMD_HERO, DASH_COLOR_OK, "开始作业");

    meta_row = ui_create_row(text_col, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta_row, UI_SU(14), LV_PART_MAIN);

    s_view.hero_rx_pill = ui_create_box(meta_row);
    lv_obj_set_size(s_view.hero_rx_pill, LV_SIZE_CONTENT, UI_SY(32));
    lv_obj_set_flex_flow(s_view.hero_rx_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_view.hero_rx_pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(s_view.hero_rx_pill, UI_SU(14), LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_view.hero_rx_pill, UI_SU(14), LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.hero_rx_pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.hero_rx_pill, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.hero_rx_pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_view.hero_rx_pill, lv_color_hex(DASH_COLOR_OK_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_view.hero_rx_pill, 1, LV_PART_MAIN);
    s_view.hero_rx_label = ui_create_label(s_view.hero_rx_pill, DASH_FONT_BODY, DASH_COLOR_OK, "● 已接收");

    s_view.hero_proto = ui_create_label(meta_row, DASH_FONT_VALUE, DASH_COLOR_MUTED, "start");

    /* Bottom dismiss hint */
    hint = ui_create_label(s_view.hero_content, DASH_FONT_SMALL, DASH_COLOR_MUTED, "点击任意位置收起");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -UI_SY(20));

    ui_set_hidden(s_view.hero_scrim, true);
}

/**********************
 *   PUBLIC API
 **********************/
void machine_view_create(lv_ui *ui, machine_view_back_cb_t on_back, machine_view_wifi_cb_t on_wifi,
                         machine_view_mqtt_cb_t on_mqtt)
{
    lv_obj_t *screen = ui->screen;

    if(s_view.created) {
        return;
    }
    memset(&s_view, 0, sizeof(s_view));
    s_view.active_tile = -1;
    s_view.on_back = on_back;
    s_view.on_wifi = on_wifi;
    s_view.on_mqtt = on_mqtt;

    /* GUI Guider drew this screen at 800 x 480. */
    lv_obj_set_size(screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(screen, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    ui_set_scrollable(screen, false);
    remove_guider_unused_widgets(ui);

    build_status_bar(ui);
    build_machine_panel(screen);
    build_commands_panel(screen);
    build_hero_overlay(screen);

    s_view.created = true;
    machine_view_set_machine(0);
}

void machine_view_set_machine(uint32_t machine_index)
{
    const machine_info_t *m = machine_get(machine_index);
    char number[4];
    char badge[8];

    if(!s_view.created || m == NULL) {
        return;
    }
    s_view.machine_index = machine_index;
    lv_snprintf(number, sizeof(number), "%02u", (unsigned)(machine_index + 1U));
    lv_snprintf(badge, sizeof(badge), "#%02u", (unsigned)(machine_index + 1U));

    ui_set_text(s_view.title_number, number);
    ui_set_text(s_view.title_name, m->name);
    ui_set_text(s_view.machine_badge_label, badge);
    ui_set_text(s_view.machine_name, m->name);
    lv_image_set_src(s_view.machine_image, m->detail_image != NULL ? m->detail_image : m->image);
    sync_hero_machine_labels();
}

void machine_view_set_cmd(const mqtt_cmd_data_t *cmd, bool animate)
{
    int32_t next_active;
    uint32_t i;

    if(!s_view.created) {
        return;
    }

    if(cmd == NULL) {
        s_view.has_cmd = false;
        memset(&s_view.cmd, 0, sizeof(s_view.cmd));
        next_active = -1;
    }
    else {
        s_view.has_cmd = true;
        s_view.cmd = *cmd;
        next_active = action_to_tile_index(cmd->action);
    }
    s_view.active_tile = next_active;

    /* Keep the underlying 8-command grid synced and highlighted. */
    for(i = 0; i < CMD_TILE_COUNT; i++) {
        tile_visual_state_t vis;

        if((int32_t)i == next_active) {
            vis = TILE_VISUAL_ACTIVE;
        }
        else if(next_active >= 0) {
            vis = TILE_VISUAL_DIM;
        }
        else {
            vis = TILE_VISUAL_IDLE;
        }
        apply_tile_visual(i, vis);
    }

    update_cmd_status_labels();

    /* Newly received standard commands expand to the large centered hero card.
     * Manual machine opens (animate == false) keep the 8-grid view visible. */
    if(next_active >= 0 && animate) {
        expand_hero(next_active, true);
    }
    else {
        collapse_hero(animate);
    }
}

void machine_view_update(const bms_data_t *bms, const imu_data_t *imu, bool mqtt_connected)
{
    LV_UNUSED(bms);
    LV_UNUSED(imu);

    if(!s_view.created) {
        return;
    }

    ui_update_clock(s_view.clock);
    ui_update_mqtt_pill(&s_view.mqtt_pill, mqtt_connected);
    if(s_view.has_cmd) {
        update_cmd_status_labels();
    }
}
