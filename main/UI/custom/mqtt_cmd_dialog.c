/*
 * Modal dialog for downlink MQTT device control commands (800 x 480 base, scaled).
 *
 *  +-------------------------------------------------------------------+
 *  | ● 远程控制指令                                15:36:12 · 刚刚   × |
 *  +-------------------------------------------------------------------+
 *  | +-------------------+   03  覆膜机                                |
 *  | |    [机具照片]     |   [ ● 开始作业 ]                            |
 *  | +-------------------+   协议动作: start   设备 #3                 |
 *  | +---------------------------------------------------------------+ |
 *  | | 指令内容 (仅非空时显示)                                       | |
 *  | | 指令参数 (自动格式化 JSON，支持上下滚动)                      | |
 *  | +---------------------------------------------------------------+ |
 *  +-------------------------------------------------------------------+
 *  | [ 下一条示例 / MQTT状态 ]                    [ 关闭 ]  [ 查看设备 ]|
 *  +-------------------------------------------------------------------+
 */

#include <string.h>
#include <time.h>

#include "mqtt_cmd_dialog.h"
#include "dashboard_theme.h"
#include "machine_list.h"
#include "ui_common.h"

/*********************
 *      LAYOUT
 *********************/
#define DIALOG_W            UI_SX(560)
#define DIALOG_H            UI_SY(360)
#define DIALOG_RADIUS       UI_SU(12)
#define PAD_X               UI_SU(20)

#define HEADER_H            UI_SY(48)
#define FOOTER_H            UI_SY(60)
#define BODY_Y              (HEADER_H + 1)
#define BODY_H              (DIALOG_H - HEADER_H - FOOTER_H - 2)
#define FOOTER_Y            (DIALOG_H - FOOTER_H)

#define PHOTO_W             UI_SX(200)
#define PHOTO_H             UI_SY(114)
#define PHOTO_Y             UI_SY(12)
#define INFO_X              (PAD_X + PHOTO_W + UI_SU(18))
#define INFO_W              (DIALOG_W - PAD_X - INFO_X)

#define DETAIL_Y            (PHOTO_Y + PHOTO_H + UI_SY(12))
#define DETAIL_W            (DIALOG_W - 2 * PAD_X)
#define DETAIL_H            (BODY_H - DETAIL_Y - UI_SY(10))

#define CLOSE_BTN_SIZE      UI_SU(28)
#define BTN_PRIMARY_W       UI_SU(110)
#define BTN_SECONDARY_W     UI_SU(88)
#define BTN_MOCK_W          UI_SU(112)
#define BTN_H               UI_SY(36)
#define BTN_GAP             UI_SU(10)

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
    const char *title;
    const char *proto;
    const char *icon;       /* NULL to show a plain colored dot instead */
    uint32_t fg;
    uint32_t bg;
    uint32_t border;
} action_style_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static const action_style_t k_action_styles[] = {
    [MQTT_CMD_ACTION_START] = {
        "开始作业", "start", LV_SYMBOL_PLAY,
        DASH_COLOR_OK, DASH_COLOR_OK_BG, DASH_COLOR_OK_BORDER
    },
    [MQTT_CMD_ACTION_STOP] = {
        "停止作业", "stop", LV_SYMBOL_STOP,
        DASH_COLOR_DANGER, DASH_COLOR_DANGER_BG, DASH_COLOR_DANGER_BORDER
    },
    [MQTT_CMD_ACTION_RESET] = {
        "回停车场", "reset", LV_SYMBOL_HOME,
        DASH_COLOR_ACCENT, DASH_COLOR_ACCENT_BG, DASH_COLOR_ACCENT_BORDER
    },
    [MQTT_CMD_ACTION_MAINTENANCE] = {
        "维护模式", "maintenance", LV_SYMBOL_SETTINGS,
        DASH_COLOR_WARN, DASH_COLOR_WARN_BG, DASH_COLOR_WARN_BORDER
    },
    [MQTT_CMD_ACTION_FORWARD] = {
        "向前行驶", "forward", SYM_ARROW_UP,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    [MQTT_CMD_ACTION_BACKWARD] = {
        "向后行驶", "backward", SYM_ARROW_DOWN,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    [MQTT_CMD_ACTION_LEFT] = {
        "向左行驶", "left", SYM_ARROW_LEFT,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    [MQTT_CMD_ACTION_RIGHT] = {
        "向右行驶", "right", SYM_ARROW_RIGHT,
        DASH_COLOR_DRIVE, DASH_COLOR_DRIVE_BG, DASH_COLOR_DRIVE_BORDER
    },
    [MQTT_CMD_ACTION_OTHER] = {
        "自定义指令", "other", NULL,
        DASH_COLOR_TEXT, DASH_COLOR_NEUTRAL_BG, DASH_COLOR_NEUTRAL_BORDER
    },
};

static struct {
    bool created;
    bool open;
    bool has_cmd;
    bool on_same_machine;
    uint32_t last_seq;
    mqtt_cmd_data_t cmd;
    mqtt_cmd_open_machine_cb_t on_open_machine;

    /* Modal layers */
    lv_obj_t *overlay;
    lv_obj_t *dialog;

    /* Header */
    lv_obj_t *header_dot;
    lv_obj_t *header_time;

    /* Command body */
    lv_obj_t *cmd_body;
    lv_obj_t *machine_image;
    lv_obj_t *machine_number;
    lv_obj_t *machine_name;
    lv_obj_t *action_badge;
    lv_obj_t *action_dot;
    lv_obj_t *action_icon;
    lv_obj_t *action_label;
    lv_obj_t *meta_label;

    lv_obj_t *detail_box;
    lv_obj_t *other_section;
    lv_obj_t *other_value;
    lv_obj_t *detail_sep;
    lv_obj_t *params_value;

    /* Empty body */
    lv_obj_t *empty_body;
    lv_obj_t *empty_dot;
    lv_obj_t *empty_state;

    /* Footer */
    lv_obj_t *btn_primary;
    lv_obj_t *btn_primary_label;
    lv_obj_t *btn_secondary;
#if !DASHBOARD_DATA_MOCK
    lv_obj_t *footer_dot;
    lv_obj_t *footer_state;
#endif
} s;

/**********************
 *   HELPERS
 **********************/
static const action_style_t *get_action_style(mqtt_cmd_action_t action)
{
    if((size_t)action < (sizeof(k_action_styles) / sizeof(k_action_styles[0]))) {
        return &k_action_styles[action];
    }
    return &k_action_styles[MQTT_CMD_ACTION_OTHER];
}

static bool is_direction_action(mqtt_cmd_action_t action)
{
    return action == MQTT_CMD_ACTION_FORWARD || action == MQTT_CMD_ACTION_BACKWARD ||
           action == MQTT_CMD_ACTION_LEFT || action == MQTT_CMD_ACTION_RIGHT;
}

static lv_obj_t *create_dot(lv_obj_t *parent, int32_t size, uint32_t color)
{
    lv_obj_t *dot = ui_create_box(parent);

    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    return dot;
}

static void set_dot_color(lv_obj_t *dot, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);

    if(!lv_color_eq(lv_obj_get_style_bg_color(dot, LV_PART_MAIN), c)) {
        lv_obj_set_style_bg_color(dot, c, LV_PART_MAIN);
    }
}

static lv_obj_t *create_pill_button(lv_obj_t *parent, int32_t w, int32_t h, bool primary,
                                    const lv_font_t *font, const char *text, lv_event_cb_t on_click)
{
    lv_obj_t *btn = ui_create_box(parent);
    lv_obj_t *label;
    uint32_t border_color = primary ? DASH_COLOR_ACCENT : DASH_COLOR_BORDER;
    uint32_t bg_color = primary ? DASH_COLOR_ACCENT : DASH_COLOR_SURFACE;
    uint32_t pressed_color = primary ? DASH_COLOR_ACCENT : DASH_COLOR_PRESSED;
    uint32_t text_color = primary ? 0xFFFFFF : DASH_COLOR_TEXT;

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(border_color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_80 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    if(!primary) {
        lv_obj_set_style_border_color(btn, lv_color_hex(DASH_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    }
    ui_set_clickable(btn, true);
    lv_obj_set_ext_click_area(btn, UI_SU(6));
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, NULL);

    label = ui_create_label(btn, font, text_color, text);
    lv_obj_center(label);
    return btn;
}

static bool append_char(char *out, size_t out_size, size_t *pos, char ch)
{
    if(*pos + 1U >= out_size) {
        return false;
    }
    out[*pos] = ch;
    (*pos)++;
    out[*pos] = '\0';
    return true;
}

static bool append_str(char *out, size_t out_size, size_t *pos, const char *str)
{
    while(*str != '\0') {
        if(!append_char(out, out_size, pos, *str)) {
            return false;
        }
        str++;
    }
    return true;
}

static const char *skip_ws(const char *p)
{
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/*
 * Formats a compact JSON object like {"speed":1.5,"mode":"auto"} into readable
 * "speed: 1.5\nmode: auto" lines without dynamic allocation.
 * Returns false if parameters are empty / "{}". Falls back to raw text on parse mismatch.
 */
static bool format_json_params(const char *src, char *out, size_t out_size)
{
    const char *p;
    size_t pos = 0;
    bool first = true;

    if(out == NULL || out_size == 0U) {
        return false;
    }
    out[0] = '\0';
    if(src == NULL) {
        return false;
    }

    p = skip_ws(src);
    if(*p == '\0') {
        return false;
    }

    if(*p == '{') {
        const char *inner = skip_ws(p + 1);
        if(*inner == '}' && *skip_ws(inner + 1) == '\0') {
            return false;
        }

        p = inner;
        while(*p != '\0' && *p != '}') {
            const char *val_start;
            const char *val_end;
            int depth = 0;
            bool in_str = false;
            bool esc = false;

            if(*p != '"') {
                goto raw_fallback;
            }
            p++; /* skip opening quote of key */

            if(!first) {
                append_char(out, out_size, &pos, '\n');
            }
            first = false;

            while(*p != '\0' && *p != '"') {
                if(*p == '\\' && p[1] != '\0') {
                    p++;
                }
                append_char(out, out_size, &pos, *p);
                p++;
            }
            if(*p != '"') {
                goto raw_fallback;
            }
            p = skip_ws(p + 1);
            if(*p != ':') {
                goto raw_fallback;
            }
            p = skip_ws(p + 1);
            append_str(out, out_size, &pos, ": ");

            val_start = p;
            while(*p != '\0') {
                char c = *p;
                if(in_str) {
                    if(esc) {
                        esc = false;
                    }
                    else if(c == '\\') {
                        esc = true;
                    }
                    else if(c == '"') {
                        in_str = false;
                    }
                }
                else {
                    if(c == '"') {
                        in_str = true;
                    }
                    else if(c == '{' || c == '[') {
                        depth++;
                    }
                    else if(c == '}' || c == ']') {
                        if(depth == 0) {
                            break;
                        }
                        depth--;
                    }
                    else if(c == ',' && depth == 0) {
                        break;
                    }
                }
                p++;
            }

            val_end = p;
            while(val_end > val_start &&
                  (val_end[-1] == ' ' || val_end[-1] == '\t' || val_end[-1] == '\r' || val_end[-1] == '\n')) {
                val_end--;
            }

            if(val_end - val_start >= 2 && val_start[0] == '"' && val_end[-1] == '"') {
                const char *q = val_start + 1;
                while(q < val_end - 1) {
                    if(*q == '\\' && (q + 1) < (val_end - 1)) {
                        q++;
                    }
                    append_char(out, out_size, &pos, *q);
                    q++;
                }
            }
            else {
                const char *q = val_start;
                while(q < val_end) {
                    append_char(out, out_size, &pos, *q);
                    q++;
                }
            }

            p = skip_ws(p);
            if(*p == ',') {
                p = skip_ws(p + 1);
            }
            else if(*p != '}') {
                goto raw_fallback;
            }
        }

        if(*p == '}' && *skip_ws(p + 1) == '\0' && pos > 0U) {
            return true;
        }
    }

raw_fallback:
    lv_snprintf(out, out_size, "%s", src);
    return out[0] != '\0';
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

static void update_time_label(void)
{
    char age_buf[24];
    char buf[48];

    if(!s.has_cmd) {
        ui_set_text(s.header_time, "");
        return;
    }

    format_age(s.cmd.received_tick_ms, age_buf, sizeof(age_buf));
    if(s.cmd.timestamp_ms >= VALID_EPOCH_MS) {
        time_t sec = (time_t)(s.cmd.timestamp_ms / 1000LL);
        const struct tm *lt = localtime(&sec);
        if(lt != NULL) {
            lv_snprintf(buf, sizeof(buf), "%02d:%02d:%02d · %s",
                        lt->tm_hour, lt->tm_min, lt->tm_sec, age_buf);
            ui_set_text(s.header_time, buf);
            return;
        }
    }
    ui_set_text(s.header_time, age_buf);
}

static void update_connection_indicators(bool connected)
{
    uint32_t dot_color = connected ? DASH_COLOR_OK : DASH_COLOR_OFF;
    uint32_t text_color = connected ? DASH_COLOR_TEXT : DASH_COLOR_MUTED;
    const char *state_text = connected ? "MQTT 已连接" : "MQTT 未连接";

    set_dot_color(s.empty_dot, dot_color);
    ui_set_text(s.empty_state, state_text);
    ui_set_text_color(s.empty_state, text_color);

#if !DASHBOARD_DATA_MOCK
    set_dot_color(s.footer_dot, dot_color);
    ui_set_text(s.footer_state, state_text);
    ui_set_text_color(s.footer_state, connected ? DASH_COLOR_MUTED : DASH_COLOR_DANGER);
#endif
}

/**********************
 *   EVENTS
 **********************/
static void on_overlay_clicked(lv_event_t *e)
{
    if(lv_event_get_target_obj(e) == s.overlay) {
        mqtt_cmd_dialog_hide();
    }
}

static void on_close_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    mqtt_cmd_dialog_hide();
}

static void on_primary_clicked(lv_event_t *e)
{
    bool should_open = s.has_cmd && !s.on_same_machine && s.on_open_machine != NULL &&
                       s.cmd.device_id >= MQTT_CMD_DEVICE_MIN && s.cmd.device_id <= MQTT_CMD_DEVICE_MAX;
    uint32_t machine_idx = should_open ? (uint32_t)(s.cmd.device_id - MQTT_CMD_DEVICE_MIN) : 0U;

    LV_UNUSED(e);
    mqtt_cmd_dialog_hide();
    if(should_open) {
        s.on_open_machine(machine_idx);
    }
}

#if DASHBOARD_DATA_MOCK
static void on_mock_next_clicked(lv_event_t *e)
{
    mqtt_cmd_data_t cmd;

    LV_UNUSED(e);
    mqtt_data_mock_trigger_next();
    if(mqtt_data_get_latest_cmd(&cmd)) {
        mqtt_cmd_dialog_show(&cmd, false);
    }
}
#endif

/**********************
 *   BUILD
 **********************/
static void build_header(lv_obj_t *dialog)
{
    lv_obj_t *header = ui_create_box(dialog);
    lv_obj_t *title_row;
    lv_obj_t *close_btn;
    lv_obj_t *close_lbl;

    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, DIALOG_W, HEADER_H);

    title_row = ui_create_row(header, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, UI_SU(8), LV_PART_MAIN);
    lv_obj_align(title_row, LV_ALIGN_LEFT_MID, PAD_X, 0);
    s.header_dot = create_dot(title_row, UI_SU(9), DASH_COLOR_ACCENT);
    ui_create_label(title_row, DASH_FONT_BODY, DASH_COLOR_TEXT, "远程控制指令");

    s.header_time = ui_create_label(header, DASH_FONT_SMALL, DASH_COLOR_MUTED, "");
    lv_obj_align(s.header_time, LV_ALIGN_RIGHT_MID, -UI_SU(48), 0);

    close_btn = ui_create_box(header);
    lv_obj_set_size(close_btn, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -UI_SU(12), 0);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    ui_set_clickable(close_btn, true);
    lv_obj_set_ext_click_area(close_btn, UI_SU(8));
    lv_obj_add_event_cb(close_btn, on_close_clicked, LV_EVENT_CLICKED, NULL);

    close_lbl = ui_create_label(close_btn, DASH_FONT_SMALL, DASH_COLOR_MUTED, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);

    ui_create_divider(dialog, 0, HEADER_H, DIALOG_W, 1);
}

static void build_cmd_body(lv_obj_t *dialog)
{
    lv_obj_t *photo_box;
    lv_obj_t *name_row;
    lv_obj_t *badge;
    lv_obj_t *detail;

    s.cmd_body = ui_create_box(dialog);
    lv_obj_set_pos(s.cmd_body, 0, BODY_Y);
    lv_obj_set_size(s.cmd_body, DIALOG_W, BODY_H);

    /* Left: machine photo framed in a soft neutral card */
    photo_box = ui_create_box(s.cmd_body);
    lv_obj_set_pos(photo_box, PAD_X, PHOTO_Y);
    lv_obj_set_size(photo_box, PHOTO_W, PHOTO_H);
    lv_obj_set_style_bg_color(photo_box, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(photo_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(photo_box, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(photo_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(photo_box, UI_CARD_RADIUS, LV_PART_MAIN);

    s.machine_image = lv_image_create(photo_box);
    lv_image_set_scale(s.machine_image, UI_IMG_SCALE);
    lv_obj_center(s.machine_image);

    /* Right: machine title, semantic action badge, raw protocol info */
    name_row = ui_create_row(s.cmd_body, INFO_X, UI_SY(14), LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(name_row, UI_SU(8), LV_PART_MAIN);
    s.machine_number = ui_create_label(name_row, DASH_FONT_BODY, DASH_COLOR_ACCENT, "--");
    ui_match_baseline(s.machine_number, DASH_FONT_BODY, DASH_FONT_VALUE);
    s.machine_name = ui_create_label(name_row, DASH_FONT_VALUE, DASH_COLOR_TEXT, "--");

    badge = ui_create_box(s.cmd_body);
    lv_obj_set_pos(badge, INFO_X, UI_SY(52));
    lv_obj_set_size(badge, LV_SIZE_CONTENT, UI_SY(38));
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(badge, UI_SU(14), LV_PART_MAIN);
    lv_obj_set_style_pad_right(badge, UI_SU(14), LV_PART_MAIN);
    lv_obj_set_style_pad_column(badge, UI_SU(8), LV_PART_MAIN);
    lv_obj_set_style_radius(badge, UI_SU(8), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    s.action_badge = badge;
    s.action_dot = create_dot(badge, UI_SU(9), DASH_COLOR_OK);
    s.action_icon = ui_create_label(badge, DASH_FONT_BODY, DASH_COLOR_OK, "");
    ui_set_hidden(s.action_icon, true);
    s.action_label = ui_create_label(badge, DASH_FONT_VALUE, DASH_COLOR_OK, "--");

    s.meta_label = ui_create_label(s.cmd_body, DASH_FONT_SMALL, DASH_COLOR_MUTED, "");
    lv_obj_set_pos(s.meta_label, INFO_X, UI_SY(102));
    lv_obj_set_width(s.meta_label, INFO_W);

    /* Bottom: scrollable detail card for other_cmd_info and parameters */
    detail = ui_create_box(s.cmd_body);
    lv_obj_set_pos(detail, PAD_X, DETAIL_Y);
    lv_obj_set_size(detail, DETAIL_W, DETAIL_H);
    lv_obj_set_style_bg_color(detail, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(detail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(detail, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(detail, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(detail, UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_left(detail, UI_SU(12), LV_PART_MAIN);
    lv_obj_set_style_pad_right(detail, UI_SU(12), LV_PART_MAIN);
    lv_obj_set_style_pad_top(detail, UI_SU(8), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(detail, UI_SU(8), LV_PART_MAIN);
    lv_obj_set_style_pad_row(detail, UI_SU(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    ui_set_clickable(detail, true);
    ui_set_scrollable(detail, true);
    lv_obj_set_scroll_dir(detail, LV_DIR_VER);
    lv_obj_set_style_width(detail, UI_SU(4), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(detail, UI_SU(2), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(detail, lv_color_hex(DASH_COLOR_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(detail, LV_OPA_50, LV_PART_SCROLLBAR);
    s.detail_box = detail;

    /* Optional custom command section (hidden when other_cmd_info is empty) */
    s.other_section = ui_create_box(detail);
    lv_obj_set_width(s.other_section, LV_PCT(100));
    lv_obj_set_height(s.other_section, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.other_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.other_section, UI_SU(2), LV_PART_MAIN);
    ui_create_label(s.other_section, DASH_FONT_SMALL, DASH_COLOR_MUTED, "指令内容");
    s.other_value = ui_create_label(s.other_section, DASH_FONT_BODY, DASH_COLOR_TEXT, "");
    lv_obj_set_width(s.other_value, LV_PCT(100));
    lv_label_set_long_mode(s.other_value, LV_LABEL_LONG_MODE_WRAP);

    s.detail_sep = ui_create_box(detail);
    lv_obj_set_width(s.detail_sep, LV_PCT(100));
    lv_obj_set_height(s.detail_sep, 1);
    lv_obj_set_style_bg_color(s.detail_sep, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.detail_sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_margin_top(s.detail_sep, UI_SU(2), LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(s.detail_sep, UI_SU(2), LV_PART_MAIN);

    ui_create_label(detail, DASH_FONT_SMALL, DASH_COLOR_MUTED, "指令参数");
    s.params_value = ui_create_label(detail, DASH_FONT_SMALL, DASH_COLOR_TEXT, "无附加参数");
    lv_obj_set_width(s.params_value, LV_PCT(100));
    lv_label_set_long_mode(s.params_value, LV_LABEL_LONG_MODE_WRAP);
}

static void build_empty_body(lv_obj_t *dialog)
{
    lv_obj_t *state_row;
    lv_obj_t *hint;

    s.empty_body = ui_create_box(dialog);
    lv_obj_set_pos(s.empty_body, 0, BODY_Y);
    lv_obj_set_size(s.empty_body, DIALOG_W, BODY_H);
    ui_set_hidden(s.empty_body, true);

    state_row = ui_create_row(s.empty_body, 0, 0, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state_row, UI_SU(8), LV_PART_MAIN);
    lv_obj_align(state_row, LV_ALIGN_CENTER, 0, -UI_SY(16));
    s.empty_dot = create_dot(state_row, UI_SU(10), DASH_COLOR_OFF);
    s.empty_state = ui_create_label(state_row, DASH_FONT_VALUE, DASH_COLOR_MUTED, "MQTT 未连接");

    hint = ui_create_label(s.empty_body, DASH_FONT_BODY, DASH_COLOR_MUTED, "暂未收到远程控制指令");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, UI_SY(20));
}

static void build_footer(lv_obj_t *dialog)
{
    lv_obj_t *footer = ui_create_box(dialog);

    ui_create_divider(dialog, 0, FOOTER_Y - 1, DIALOG_W, 1);

    lv_obj_set_pos(footer, 0, FOOTER_Y);
    lv_obj_set_size(footer, DIALOG_W, FOOTER_H);

#if DASHBOARD_DATA_MOCK
    {
        lv_obj_t *mock_btn = create_pill_button(footer, BTN_MOCK_W, BTN_H, false,
                                                DASH_FONT_SMALL, "下一条示例", on_mock_next_clicked);
        lv_obj_align(mock_btn, LV_ALIGN_LEFT_MID, PAD_X, 0);
    }
#else
    {
        lv_obj_t *row = ui_create_row(footer, 0, 0, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, UI_SU(6), LV_PART_MAIN);
        lv_obj_align(row, LV_ALIGN_LEFT_MID, PAD_X, 0);
        s.footer_dot = create_dot(row, UI_SU(8), DASH_COLOR_OFF);
        s.footer_state = ui_create_label(row, DASH_FONT_SMALL, DASH_COLOR_MUTED, "MQTT 未连接");
    }
#endif

    s.btn_primary = create_pill_button(footer, BTN_PRIMARY_W, BTN_H, true,
                                       DASH_FONT_BODY, "查看设备", on_primary_clicked);
    lv_obj_align(s.btn_primary, LV_ALIGN_RIGHT_MID, -PAD_X, 0);
    s.btn_primary_label = lv_obj_get_child(s.btn_primary, 0);

    s.btn_secondary = create_pill_button(footer, BTN_SECONDARY_W, BTN_H, false,
                                         DASH_FONT_BODY, "关闭", on_close_clicked);
    lv_obj_align(s.btn_secondary, LV_ALIGN_RIGHT_MID, -(PAD_X + BTN_PRIMARY_W + BTN_GAP), 0);
}

void mqtt_cmd_dialog_init(mqtt_cmd_open_machine_cb_t on_open_machine)
{
    lv_obj_t *top;

    if(s.created) {
        s.on_open_machine = on_open_machine;
        return;
    }
    s.on_open_machine = on_open_machine;

    top = lv_layer_top();

    s.overlay = ui_create_box(top);
    lv_obj_set_pos(s.overlay, 0, 0);
    lv_obj_set_size(s.overlay, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(s.overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.overlay, LV_OPA_40, LV_PART_MAIN);
    ui_set_clickable(s.overlay, true);
    lv_obj_add_event_cb(s.overlay, on_overlay_clicked, LV_EVENT_CLICKED, NULL);

    s.dialog = ui_create_box(s.overlay);
    lv_obj_set_size(s.dialog, DIALOG_W, DIALOG_H);
    lv_obj_center(s.dialog);
    lv_obj_set_style_bg_color(s.dialog, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s.dialog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s.dialog, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s.dialog, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s.dialog, DIALOG_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s.dialog, UI_SU(28), LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s.dialog, UI_SU(6), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s.dialog, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s.dialog, LV_OPA_20, LV_PART_MAIN);
    /* Absorb taps inside the dialog card so they do not bubble to the overlay. */
    ui_set_clickable(s.dialog, true);

    build_header(s.dialog);
    build_cmd_body(s.dialog);
    build_empty_body(s.dialog);
    build_footer(s.dialog);

    ui_set_hidden(s.overlay, true);
    s.created = true;
}

/**********************
 *   SHOW / UPDATE
 **********************/
void mqtt_cmd_dialog_show(const mqtt_cmd_data_t *cmd, bool on_same_machine)
{
    const action_style_t *style;
    const machine_info_t *machine = NULL;
    char num_buf[8];
    char meta_buf[48];
    char params_buf[MQTT_CMD_PARAMS_MAX_LEN + 64U];
    bool has_other;
    bool has_params;
    bool show_jump;

    if(!s.created || cmd == NULL) {
        return;
    }

    s.cmd = *cmd;
    s.has_cmd = true;
    s.on_same_machine = on_same_machine;
    if(cmd->seq > s.last_seq) {
        s.last_seq = cmd->seq;
    }

    style = get_action_style(cmd->action);
    if(cmd->device_id >= MQTT_CMD_DEVICE_MIN && cmd->device_id <= MQTT_CMD_DEVICE_MAX) {
        machine = machine_get((uint32_t)(cmd->device_id - MQTT_CMD_DEVICE_MIN));
    }

    /* Header */
    set_dot_color(s.header_dot, style->fg);
    update_time_label();

    /* Machine identity */
    if(machine != NULL) {
        lv_snprintf(num_buf, sizeof(num_buf), "%02u", (unsigned)cmd->device_id);
        ui_set_text(s.machine_number, num_buf);
        ui_set_text(s.machine_name, machine->name);
        lv_image_set_src(s.machine_image, machine->image);
        ui_set_hidden(s.machine_image, false);
    }
    else {
        ui_set_text(s.machine_number, "--");
        ui_set_text(s.machine_name, "未知设备");
        ui_set_hidden(s.machine_image, true);
    }

    /* Semantic action badge */
    lv_obj_set_style_bg_color(s.action_badge, lv_color_hex(style->bg), LV_PART_MAIN);
    lv_obj_set_style_border_color(s.action_badge, lv_color_hex(style->border), LV_PART_MAIN);
    if(style->icon != NULL) {
        ui_set_hidden(s.action_dot, true);
        ui_set_hidden(s.action_icon, false);
        ui_set_text(s.action_icon, style->icon);
        ui_set_text_color(s.action_icon, style->fg);
    }
    else {
        ui_set_hidden(s.action_icon, true);
        ui_set_hidden(s.action_dot, false);
        set_dot_color(s.action_dot, style->fg);
    }
    ui_set_text(s.action_label, style->title);
    ui_set_text_color(s.action_label, style->fg);

    /* Protocol metadata */
    lv_snprintf(meta_buf, sizeof(meta_buf), "协议动作: %s   设备 #%u",
                style->proto, (unsigned)cmd->device_id);
    ui_set_text(s.meta_label, meta_buf);

    /* Custom command text + formatted parameters.
     * Directional commands carried via action="other" + other_cmd_info="forward"/...
     * repeat the direction word in other_cmd_info; avoid showing it twice. */
    has_other = (cmd->other_cmd_info[0] != '\0') &&
                !(is_direction_action(cmd->action) && strcmp(cmd->other_cmd_info, style->proto) == 0);
    ui_set_hidden(s.other_section, !has_other);
    ui_set_hidden(s.detail_sep, !has_other);
    if(has_other) {
        ui_set_text(s.other_value, cmd->other_cmd_info);
    }

    has_params = format_json_params(cmd->parameters, params_buf, sizeof(params_buf));
    if(has_params) {
        ui_set_text(s.params_value, params_buf);
        ui_set_text_color(s.params_value, DASH_COLOR_TEXT);
    }
    else {
        ui_set_text(s.params_value, "无附加参数");
        ui_set_text_color(s.params_value, DASH_COLOR_MUTED);
    }
    lv_obj_scroll_to_y(s.detail_box, 0, LV_ANIM_OFF);

    /* Buttons */
    show_jump = !on_same_machine && machine != NULL && s.on_open_machine != NULL;
    ui_set_hidden(s.btn_secondary, !show_jump);
    ui_set_text(s.btn_primary_label, show_jump ? "查看设备" : "知道了");

    ui_set_hidden(s.empty_body, true);
    ui_set_hidden(s.cmd_body, false);
    ui_set_hidden(s.overlay, false);
    s.open = true;
}

void mqtt_cmd_dialog_show_empty(bool mqtt_connected)
{
    if(!s.created) {
        return;
    }
    s.has_cmd = false;
    set_dot_color(s.header_dot, mqtt_connected ? DASH_COLOR_OK : DASH_COLOR_OFF);
    ui_set_text(s.header_time, "");
    update_connection_indicators(mqtt_connected);

    ui_set_hidden(s.btn_secondary, true);
    ui_set_text(s.btn_primary_label, "知道了");
    ui_set_hidden(s.cmd_body, true);
    ui_set_hidden(s.empty_body, false);
    ui_set_hidden(s.overlay, false);
    s.open = true;
}

void mqtt_cmd_dialog_hide(void)
{
    if(!s.created) {
        return;
    }
    ui_set_hidden(s.overlay, true);
    s.open = false;
}

bool mqtt_cmd_dialog_is_open(void)
{
    return s.open;
}

uint32_t mqtt_cmd_dialog_last_seq(void)
{
    return s.last_seq;
}

void mqtt_cmd_dialog_refresh(bool mqtt_connected)
{
    if(!s.created || !s.open) {
        return;
    }
    update_connection_indicators(mqtt_connected);
    if(s.has_cmd) {
        update_time_label();
    }
}
