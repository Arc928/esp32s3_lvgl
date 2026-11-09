/*
 * Wi-Fi page (800 x 480).
 *
 *  +-- ‹ 返回 ----------------- Wi-Fi ------------------- Scan --+
 *  | Connected    MyHome                          [Disconnect]  |
 *  | +--------------------------------------------------------+ |
 *  | | MyHome                         Connected         ||||  | |
 *  | | Office-5G                      Secured           |||   | |
 *  | | Guest                          Open              ||    | |
 *  | +--------------------------------------------------------+ |
 *
 * Tapping a secured network swaps the list for a password sheet with the
 * keyboard below it. An open network connects right away.
 *
 * The strings are English because the dash fonts only carry the Chinese
 * characters used elsewhere. To translate them, add the new characters to the
 * dash fonts (see tools/gen_fonts.sh in the lvgl_bms_ui simulator project)
 * and edit the STR_* below.
 */

#include <string.h>

#include "wifi_view.h"
#include "net_service.h"
#include "dashboard_theme.h"
#include "ui_common.h"

/*********************
 *      STRINGS
 *********************/
#define STR_BACK            LV_SYMBOL_LEFT " 返回"
#define STR_TITLE           "Wi-Fi"
#define STR_SCAN            "Scan"
#define STR_SCANNING        "Scanning..."
#define STR_CONNECT         "Connect"
#define STR_CANCEL          "Cancel"
#define STR_DISCONNECT      "Disconnect"
#define STR_PASSWORD        "Password"
#define STR_CONNECT_TO      "Connect to "

#define STR_CONNECTED       "Connected"
#define STR_CONNECTING      "Connecting..."
#define STR_NOT_CONNECTED   "Not connected"
#define STR_FAILED          "Connection failed"
#define STR_WRONG_PASSWORD  "Wrong password"
#define STR_NOT_FOUND       "Network not found"
#define STR_OTHER_ERROR     "Could not connect"

#define STR_OPEN            "Open"
#define STR_SECURED         "Secured"
#define STR_UNSUPPORTED     "Not supported"

#define STR_NO_NETWORKS     "No networks found"
#define STR_NOT_READY       "Wi-Fi is not ready yet, tap Scan to retry"
#define STR_BUSY            "Busy connecting, tap Scan when it is done"
#define STR_SCAN_FAILED     "Scan failed, tap Scan to retry"
#define STR_NO_MEMORY       "Not enough memory to scan, try again later"
#define STR_PASSWORD_LEN    "The password needs 8 to 63 characters"

/*********************
 *      LAYOUT
 *********************/
#define BACK_W              84
#define SCAN_W              104
#define BAR_BTN_H           26
#define STATUS_CARD_H       52
#define LIST_Y              (UI_GAP + STATUS_CARD_H + UI_GAP)
#define LIST_H              (UI_SCREEN_H - UI_STATUS_BAR_H - LIST_Y)
#define ROW_H               52
#define ROW_PAD             14

#define KB_H                232
#define SHEET_H             (UI_SCREEN_H - UI_STATUS_BAR_H - KB_H)
#define SHEET_PAD           24
#define EYE_W               48
#define FIELD_H             44

#define SIGNAL_BARS         4
#define SIGNAL_BAR_W        4
#define SIGNAL_BAR_GAP      2
#define SIGNAL_W            (SIGNAL_BARS * SIGNAL_BAR_W + (SIGNAL_BARS - 1) * SIGNAL_BAR_GAP)
#define SIGNAL_H            16
#define NAME_W              460
#define CAPTION_W           160
#define CAPTION_GAP         14

#define TICK_MS             250U

/* The Montserrat fonts have the password bullet and the keyboard symbols; the dash fonts do not. */
#if LV_FONT_MONTSERRAT_20
#define INPUT_FONT          (&lv_font_montserrat_20)
#else
#define INPUT_FONT          LV_FONT_DEFAULT
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static struct {
    wifi_view_back_cb_t on_back;
    bool created;

    /* Top bar */
    lv_obj_t *scan_label;

    /* List page */
    lv_obj_t *list_page;
    lv_obj_t *status_state;
    lv_obj_t *status_detail;
    lv_obj_t *status_button;
    lv_obj_t *status_button_label;
    lv_obj_t *list;

    /* Password sheet */
    lv_obj_t *sheet;
    lv_obj_t *sheet_title;
    lv_obj_t *textarea;
    lv_obj_t *eye_label;
    lv_obj_t *message;
    lv_obj_t *keyboard;
    char target_ssid[NET_SSID_MAX_LEN + 1];
    bool sheet_open;
    bool sheet_connecting;

    /* Data */
    lv_timer_t *timer;
    net_ap_t aps[NET_SCAN_MAX_APS];
    size_t ap_count;
    bool scanning;
    const char *hint;                           /* shown instead of the list while it is empty */
    net_status_t status;                        /* as last drawn */
    bool status_valid;
    char connected_ssid[NET_SSID_MAX_LEN + 1];  /* row marked as connected, "" if none */
} s;

/**********************
 *   HELPERS
 **********************/
static lv_obj_t *create_button(lv_obj_t *parent, int32_t w, int32_t h, bool primary, const lv_font_t *font,
                               const char *text, lv_event_cb_t on_click)
{
    lv_obj_t *button = ui_create_box(parent);
    lv_obj_t *label;
    uint32_t color = primary ? DASH_COLOR_ACCENT : DASH_COLOR_TEXT;

    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, primary ? lv_color_hex(color) : lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, primary ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, primary ? LV_OPA_70 : LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    ui_set_clickable(button, true);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, NULL);

    label = ui_create_label(button, font, primary ? 0xFFFFFF : DASH_COLOR_TEXT, text);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *button_label(lv_obj_t *button)
{
    return lv_obj_get_child(button, 0);
}

static const char *fail_text(net_fail_t fail)
{
    switch(fail) {
        case NET_FAIL_WRONG_PASSWORD: return STR_WRONG_PASSWORD;
        case NET_FAIL_NOT_FOUND:      return STR_NOT_FOUND;
        default:                      return STR_OTHER_ERROR;
    }
}

static int signal_bars_lit(int rssi)
{
    if(rssi >= -55) return 4;
    if(rssi >= -65) return 3;
    if(rssi >= -75) return 2;
    return 1;
}

static void show_message(const char *text, uint32_t color)
{
    ui_set_text(s.message, text);
    ui_set_text_color(s.message, color);
}

/**********************
 *   LIST
 **********************/
static void on_row_clicked(lv_event_t *e);

static lv_style_t s_row_style;
static lv_style_t s_row_pressed_style;
static bool s_styles_ready;

static void init_row_styles(void)
{
    lv_style_init(&s_row_style);
    lv_style_set_width(&s_row_style, LV_PCT(100));
    lv_style_set_height(&s_row_style, ROW_H);
    lv_style_set_bg_color(&s_row_style, lv_color_hex(DASH_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_row_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_row_style, lv_color_hex(DASH_COLOR_BORDER));
    lv_style_set_border_width(&s_row_style, 1);
    lv_style_set_radius(&s_row_style, UI_CARD_RADIUS);

    lv_style_init(&s_row_pressed_style);
    lv_style_set_bg_color(&s_row_pressed_style, lv_color_hex(DASH_COLOR_PRESSED));
    lv_style_set_border_color(&s_row_pressed_style, lv_color_hex(DASH_COLOR_ACCENT));
    s_styles_ready = true;
}

static bool is_connected_row(const net_ap_t *ap)
{
    return s.connected_ssid[0] != '\0' && strcmp(ap->ssid, s.connected_ssid) == 0;
}

/*
 * A row is a single object that draws its own text and signal bars. The Wi-Fi
 * driver and the LVGL heap share the small internal RAM, so a row of nine
 * widgets (about 2.5 KB each on the PC) is too expensive for 24 networks.
 */
static void on_row_draw(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *row = lv_event_get_target_obj(e);
    const net_ap_t *ap;
    bool connected;
    bool usable;
    lv_area_t box;
    lv_area_t area;
    lv_draw_label_dsc_t label;
    lv_draw_rect_dsc_t bar;
    int32_t line_h;
    int32_t base_y;
    int lit;
    int i;

    if(index >= s.ap_count) {
        return;
    }
    ap = &s.aps[index];
    connected = is_connected_row(ap);
    usable = (ap->auth != NET_AUTH_UNSUPPORTED);
    lv_obj_get_coords(row, &box);

    /* Network name */
    lv_draw_label_dsc_init(&label);
    label.font = DASH_FONT_BODY;
    label.color = lv_color_hex(usable ? DASH_COLOR_TEXT : DASH_COLOR_MUTED);
    label.text = ap->ssid;
    line_h = lv_font_get_line_height(label.font);
    area.x1 = box.x1 + ROW_PAD;
    area.x2 = area.x1 + NAME_W - 1;
    area.y1 = box.y1 + (ROW_H - line_h) / 2;
    area.y2 = area.y1 + line_h - 1;
    lv_draw_label(layer, &label, &area);

    /* Caption, right-aligned just left of the signal bars */
    label.font = DASH_FONT_SMALL;
    label.color = lv_color_hex(connected ? DASH_COLOR_OK : DASH_COLOR_MUTED);
    label.text = connected ? STR_CONNECTED :
                 ap->auth == NET_AUTH_OPEN ? STR_OPEN :
                 ap->auth == NET_AUTH_PSK ? STR_SECURED : STR_UNSUPPORTED;
    label.align = LV_TEXT_ALIGN_RIGHT;
    line_h = lv_font_get_line_height(label.font);
    area.x2 = box.x2 - ROW_PAD - SIGNAL_W - CAPTION_GAP;
    area.x1 = area.x2 - CAPTION_W + 1;
    area.y1 = box.y1 + (ROW_H - line_h) / 2;
    area.y2 = area.y1 + line_h - 1;
    lv_draw_label(layer, &label, &area);

    /* Signal bars */
    lv_draw_rect_dsc_init(&bar);
    bar.radius = 1;
    bar.bg_opa = LV_OPA_COVER;
    lit = signal_bars_lit(ap->rssi);
    base_y = box.y1 + ROW_H / 2 + SIGNAL_H / 2;
    for(i = 0; i < SIGNAL_BARS; i++) {
        bar.bg_color = lv_color_hex(i < lit ? DASH_COLOR_TEXT : DASH_COLOR_OFF);
        area.x1 = box.x2 - ROW_PAD - SIGNAL_W + i * (SIGNAL_BAR_W + SIGNAL_BAR_GAP);
        area.x2 = area.x1 + SIGNAL_BAR_W - 1;
        area.y2 = base_y - 1;
        area.y1 = base_y - (4 + i * 4);
        lv_draw_rect(layer, &bar, &area);
    }
}

static void create_row(uint32_t index)
{
    const net_ap_t *ap = &s.aps[index];
    lv_obj_t *row = ui_create_box(s.list);

    lv_obj_add_style(row, &s_row_style, LV_PART_MAIN);
    lv_obj_add_event_cb(row, on_row_draw, LV_EVENT_DRAW_MAIN_END, (void *)(uintptr_t)index);
    if(ap->auth != NET_AUTH_UNSUPPORTED && !is_connected_row(ap)) {
        ui_set_clickable(row, true);
        lv_obj_add_style(row, &s_row_pressed_style, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }
}

static void rebuild_list(bool keep_scroll)
{
    int32_t scroll_y = keep_scroll ? lv_obj_get_scroll_y(s.list) : 0;
    uint32_t i;

    lv_obj_clean(s.list);
    if(s.ap_count == 0) {
        const char *text = s.scanning ? STR_SCANNING : (s.hint != NULL ? s.hint : STR_NO_NETWORKS);
        lv_obj_t *label = ui_create_label(s.list, DASH_FONT_BODY, DASH_COLOR_MUTED, text);

        lv_obj_set_style_pad_top(label, 40, LV_PART_MAIN);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }
    for(i = 0; i < s.ap_count; i++) {
        create_row(i);
    }
    if(scroll_y > 0) {
        lv_obj_update_layout(s.list);
        lv_obj_scroll_to_y(s.list, scroll_y, LV_ANIM_OFF);
    }
}

static void update_scan_button(void)
{
    ui_set_text(s.scan_label, s.scanning ? STR_SCANNING : STR_SCAN);
}

static void start_scan(void)
{
    esp_err_t err;

    if(s.scanning) {
        return;
    }
    /* Free the rows first: the Wi-Fi driver needs the internal RAM while it scans. */
    s.ap_count = 0;
    s.scanning = true;
    s.hint = NULL;
    rebuild_list(false);

    err = net_service_scan_start();
    if(err != ESP_OK) {
        s.scanning = false;
        if(err == ESP_ERR_NO_MEM) {
            s.hint = STR_NO_MEMORY;
        }
        else if(err == ESP_ERR_INVALID_STATE) {
            net_status_t status;

            net_service_get_status(&status);
            s.hint = (status.state == NET_STATE_CONNECTING) ? STR_BUSY : STR_NOT_READY;
        }
        else {
            s.hint = STR_SCAN_FAILED;
        }
        rebuild_list(false);
    }
    update_scan_button();
}

/**********************
 *   PASSWORD SHEET
 **********************/
static void set_password_visible(bool visible)
{
    lv_textarea_set_password_mode(s.textarea, !visible);
    /* The icon shows what a tap does: reveal when hidden, hide when revealed. */
    ui_set_text(s.eye_label, visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void open_sheet(const net_ap_t *ap)
{
    char title[sizeof(STR_CONNECT_TO) + NET_SSID_MAX_LEN];

    lv_snprintf(s.target_ssid, sizeof(s.target_ssid), "%s", ap->ssid);
    lv_snprintf(title, sizeof(title), STR_CONNECT_TO "%s", ap->ssid);
    ui_set_text(s.sheet_title, title);

    lv_textarea_set_text(s.textarea, "");
    set_password_visible(false);
    show_message("", DASH_COLOR_MUTED);

    ui_set_hidden(s.list_page, true);
    ui_set_hidden(s.sheet, false);
    ui_set_hidden(s.keyboard, false);
    lv_keyboard_set_mode(s.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s.keyboard, s.textarea);
    /* A touch screen never focuses the field by itself; this makes the cursor show. */
    lv_obj_add_state(s.textarea, LV_STATE_FOCUSED);

    s.sheet_open = true;
    s.sheet_connecting = false;
}

static void close_sheet(void)
{
    /* Do not keep the password around. */
    lv_textarea_set_text(s.textarea, "");
    lv_keyboard_set_textarea(s.keyboard, NULL);
    lv_obj_remove_state(s.textarea, LV_STATE_FOCUSED);

    ui_set_hidden(s.sheet, true);
    ui_set_hidden(s.keyboard, true);
    ui_set_hidden(s.list_page, false);

    s.sheet_open = false;
    s.sheet_connecting = false;
}

static void submit_password(void)
{
    const char *password = lv_textarea_get_text(s.textarea);
    size_t len = strlen(password);

    if(s.sheet_connecting) {
        return;
    }
    if(len < NET_PASSWORD_MIN_LEN) {
        show_message(STR_PASSWORD_LEN, DASH_COLOR_DANGER);
        return;
    }
    if(net_service_connect(s.target_ssid, password) == ESP_OK) {
        s.sheet_connecting = true;
        show_message(STR_CONNECTING, DASH_COLOR_ACCENT);
    }
    else {
        show_message(STR_OTHER_ERROR, DASH_COLOR_DANGER);
    }
}

static void cancel_sheet(void)
{
    if(s.sheet_connecting) {
        net_service_disconnect();
    }
    close_sheet();
}

static void on_sheet_connect_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    submit_password();
}

static void on_sheet_cancel_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    cancel_sheet();
}

static void on_eye_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    set_password_visible(lv_textarea_get_password_mode(s.textarea));
}

static void on_keyboard_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_READY) {
        submit_password();
    }
    else if(code == LV_EVENT_CANCEL) {
        cancel_sheet();
    }
}

/**********************
 *   STATUS
 **********************/
static void draw_status(const net_status_t *st)
{
    char detail[NET_SSID_MAX_LEN + 32];
    const char *state_text = STR_NOT_CONNECTED;
    uint32_t state_color = DASH_COLOR_MUTED;
    bool show_button = false;

    detail[0] = '\0';
    switch(st->state) {
        case NET_STATE_CONNECTED:
            state_text = STR_CONNECTED;
            state_color = DASH_COLOR_OK;
            show_button = true;
            lv_snprintf(detail, sizeof(detail), "%s", st->ssid);
            break;
        case NET_STATE_CONNECTING:
            state_text = STR_CONNECTING;
            state_color = DASH_COLOR_ACCENT;
            show_button = true;
            lv_snprintf(detail, sizeof(detail), "%s", st->ssid);
            break;
        case NET_STATE_FAILED:
            state_text = STR_FAILED;
            state_color = DASH_COLOR_DANGER;
            lv_snprintf(detail, sizeof(detail), "%s - %s", st->ssid, fail_text(st->fail));
            break;
        default:
            break;
    }

    ui_set_text(s.status_state, state_text);
    ui_set_text_color(s.status_state, state_color);
    ui_set_text(s.status_detail, detail);
    ui_set_text(s.status_button_label, st->state == NET_STATE_CONNECTING ? STR_CANCEL : STR_DISCONNECT);
    ui_set_hidden(s.status_button, !show_button);
}

static void on_status_button_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    net_service_disconnect();
}

/* Everything that follows the connection state and the scan, polled while the page is shown. */
static void on_tick(lv_timer_t *timer)
{
    net_status_t st;
    const char *connected = "";
    bool changed;

    LV_UNUSED(timer);

    /* Scan result */
    if(s.scanning) {
        size_t count = 0;
        net_scan_state_t scan = net_service_scan_get(s.aps, NET_SCAN_MAX_APS, &count);

        if(scan == NET_SCAN_DONE || scan == NET_SCAN_FAILED) {
            s.scanning = false;
            s.ap_count = count;     /* 0 when the scan failed */
            s.hint = (scan == NET_SCAN_FAILED) ? STR_SCAN_FAILED : NULL;
            update_scan_button();
            rebuild_list(false);
        }
    }

    /* Connection state */
    net_service_get_status(&st);
    changed = !s.status_valid || st.state != s.status.state || st.fail != s.status.fail ||
              strcmp(st.ssid, s.status.ssid) != 0;
    if(changed) {
        s.status = st;
        s.status_valid = true;
        draw_status(&st);

        if(st.state == NET_STATE_CONNECTED) {
            connected = st.ssid;
        }
        if(strcmp(connected, s.connected_ssid) != 0) {
            lv_snprintf(s.connected_ssid, sizeof(s.connected_ssid), "%s", connected);
            if(!s.scanning) {
                rebuild_list(true);
            }
        }
    }

    /* Result of the password the user just typed */
    if(s.sheet_open && s.sheet_connecting) {
        switch(st.state) {
            case NET_STATE_CONNECTED:
                close_sheet();
                break;
            case NET_STATE_FAILED:
                s.sheet_connecting = false;
                show_message(fail_text(st.fail), DASH_COLOR_DANGER);
                break;
            case NET_STATE_DISCONNECTED:
                s.sheet_connecting = false;
                show_message("", DASH_COLOR_MUTED);
                break;
            default:
                break;
        }
    }
}

static void on_row_clicked(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    const net_ap_t *ap;

    if(index >= s.ap_count) {
        return;
    }
    ap = &s.aps[index];
    if(ap->auth == NET_AUTH_PSK) {
        open_sheet(ap);
    }
    else if(ap->auth == NET_AUTH_OPEN) {
        /* The status card follows on the next tick. */
        net_service_connect(ap->ssid, NULL);
    }
}

/**********************
 *   TOP BAR
 **********************/
static void on_back_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s.on_back != NULL) {
        s.on_back();
    }
}

static void on_scan_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    start_scan();
}

static void build_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = ui_create_box(screen);
    lv_obj_t *button;
    lv_obj_t *title;

    ui_style_status_bar(bar);

    button = create_button(bar, BACK_W, BAR_BTN_H, false, DASH_FONT_BODY, STR_BACK, on_back_clicked);
    lv_obj_align(button, LV_ALIGN_LEFT_MID, UI_GAP, 0);

    title = ui_create_label(bar, DASH_FONT_BODY, DASH_COLOR_TEXT, STR_TITLE);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    button = create_button(bar, SCAN_W, BAR_BTN_H, false, DASH_FONT_BODY, STR_SCAN, on_scan_clicked);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, -UI_GAP, 0);
    s.scan_label = button_label(button);
}

/**********************
 *   BUILD
 **********************/
static void build_list_page(lv_obj_t *screen)
{
    lv_obj_t *card;

    s.list_page = ui_create_box(screen);
    lv_obj_set_pos(s.list_page, 0, UI_STATUS_BAR_H);
    lv_obj_set_size(s.list_page, UI_SCREEN_W, UI_SCREEN_H - UI_STATUS_BAR_H);

    /* Current connection */
    card = ui_create_box(s.list_page);
    lv_obj_set_pos(card, UI_GAP, UI_GAP);
    lv_obj_set_size(card, UI_SCREEN_W - 2 * UI_GAP, STATUS_CARD_H);
    ui_style_card(card);

    s.status_state = ui_create_label(card, DASH_FONT_BODY, DASH_COLOR_MUTED, STR_NOT_CONNECTED);
    lv_obj_align(s.status_state, LV_ALIGN_LEFT_MID, ROW_PAD, 0);

    s.status_detail = ui_create_label(card, DASH_FONT_BODY, DASH_COLOR_TEXT, "");
    lv_obj_set_width(s.status_detail, 400);
    lv_label_set_long_mode(s.status_detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s.status_detail, LV_ALIGN_LEFT_MID, 170, 0);

    s.status_button = create_button(card, 112, 30, false, DASH_FONT_BODY, STR_DISCONNECT, on_status_button_clicked);
    lv_obj_align(s.status_button, LV_ALIGN_RIGHT_MID, -ROW_PAD, 0);
    s.status_button_label = button_label(s.status_button);
    ui_set_hidden(s.status_button, true);

    /* Networks. Rows are added by rebuild_list(). */
    s.list = ui_create_box(s.list_page);
    lv_obj_set_pos(s.list, 0, LIST_Y);
    lv_obj_set_size(s.list, UI_SCREEN_W, LIST_H);
    /* Clickable so a drag that starts between two rows still scrolls. */
    ui_set_clickable(s.list, true);
    ui_set_scrollable(s.list, true);
    lv_obj_set_scroll_dir(s.list, LV_DIR_VER);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(s.list, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s.list, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s.list, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s.list, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_width(s.list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s.list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s.list, lv_color_hex(DASH_COLOR_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s.list, LV_OPA_60, LV_PART_SCROLLBAR);
}

static void build_keyboard(lv_obj_t *screen)
{
    lv_obj_t *kb = lv_keyboard_create(screen);

    s.keyboard = kb;
    lv_obj_set_size(kb, UI_SCREEN_W, KB_H);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_hex(DASH_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 6, LV_PART_MAIN);

    lv_obj_set_style_text_font(kb, INPUT_FONT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(DASH_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(DASH_COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(DASH_COLOR_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    /* Shift / caps lock and the mode keys when active. */
    lv_obj_set_style_bg_color(kb, lv_color_hex(DASH_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);
    ui_set_hidden(kb, true);
}

static void build_textarea(void)
{
    lv_obj_t *ta = lv_textarea_create(s.sheet);
    int32_t pad_y = (FIELD_H - 2 - lv_font_get_line_height(INPUT_FONT)) / 2;   /* centers the line, 2 = borders */

    s.textarea = ta;

    /* Font and padding first: one-line mode sizes the field from them. */
    lv_obj_set_style_text_font(ta, INPUT_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(DASH_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(DASH_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(DASH_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(DASH_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ta, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ta, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ta, pad_y, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ta, pad_y, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(DASH_COLOR_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, lv_color_hex(DASH_COLOR_ACCENT), LV_PART_CURSOR);

    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, NET_PASSWORD_MAX_LEN);
    lv_textarea_set_placeholder_text(ta, STR_PASSWORD);
    lv_textarea_set_password_mode(ta, true);

    lv_obj_set_pos(ta, SHEET_PAD, 50);
    lv_obj_set_size(ta, UI_SCREEN_W - 2 * SHEET_PAD - EYE_W - UI_GAP, FIELD_H);
}

static void build_sheet(lv_obj_t *screen)
{
    lv_obj_t *button;

    s.sheet = ui_create_box(screen);
    lv_obj_set_pos(s.sheet, 0, UI_STATUS_BAR_H);
    lv_obj_set_size(s.sheet, UI_SCREEN_W, SHEET_H);

    s.sheet_title = ui_create_label(s.sheet, DASH_FONT_VALUE, DASH_COLOR_TEXT, "");
    lv_obj_set_width(s.sheet_title, UI_SCREEN_W - 2 * SHEET_PAD);
    lv_label_set_long_mode(s.sheet_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s.sheet_title, SHEET_PAD, 12);

    build_textarea();

    /* Show / hide the password */
    button = ui_create_box(s.sheet);
    lv_obj_set_size(button, EYE_W, FIELD_H);
    lv_obj_set_pos(button, UI_SCREEN_W - SHEET_PAD - EYE_W, 50);
    ui_style_card(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(DASH_COLOR_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    ui_set_clickable(button, true);
    lv_obj_add_event_cb(button, on_eye_clicked, LV_EVENT_CLICKED, NULL);
    s.eye_label = ui_create_label(button, INPUT_FONT, DASH_COLOR_TEXT, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(s.eye_label);

    s.message = ui_create_label(s.sheet, DASH_FONT_SMALL, DASH_COLOR_MUTED, "");
    lv_obj_set_width(s.message, UI_SCREEN_W - 2 * SHEET_PAD);
    lv_label_set_long_mode(s.message, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s.message, SHEET_PAD, 102);

    button = create_button(s.sheet, 120, 40, true, DASH_FONT_BODY, STR_CONNECT, on_sheet_connect_clicked);
    lv_obj_set_pos(button, UI_SCREEN_W - SHEET_PAD - 120, SHEET_H - 40 - 14);
    button = create_button(s.sheet, 120, 40, false, DASH_FONT_BODY, STR_CANCEL, on_sheet_cancel_clicked);
    lv_obj_set_pos(button, UI_SCREEN_W - SHEET_PAD - 120 - 12 - 120, SHEET_H - 40 - 14);

    ui_set_hidden(s.sheet, true);
}

/* The page is gone: give the rows back to the heap, they are rebuilt by the next scan. */
static void on_screen_unloaded(lv_event_t *e)
{
    LV_UNUSED(e);
    s.ap_count = 0;
    s.scanning = false;
    lv_obj_clean(s.list);
}

lv_obj_t *wifi_view_create(wifi_view_back_cb_t on_back)
{
    lv_obj_t *screen;

    if(s.created) {
        return lv_obj_get_screen(s.list);
    }
    screen = ui_create_screen();
    s.on_back = on_back;
    if(!s_styles_ready) {
        init_row_styles();
    }

    build_status_bar(screen);
    build_list_page(screen);
    build_sheet(screen);
    build_keyboard(screen);
    lv_obj_add_event_cb(screen, on_screen_unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);

    s.created = true;
    return screen;
}

/**********************
 *   ENTER / LEAVE
 **********************/
void wifi_view_enter(void)
{
    if(!s.created) {
        return;
    }
    if(s.timer == NULL) {
        s.timer = lv_timer_create(on_tick, TICK_MS, NULL);
    }
    close_sheet();

    s.ap_count = 0;
    s.scanning = false;
    s.hint = NULL;
    s.status_valid = false;
    s.connected_ssid[0] = '\0';

    start_scan();       /* also draws the empty list with its hint */
    on_tick(NULL);
}

void wifi_view_leave(void)
{
    if(!s.created) {
        return;
    }
    if(s.timer != NULL) {
        lv_timer_delete(s.timer);
        s.timer = NULL;
    }
    close_sheet();
}
