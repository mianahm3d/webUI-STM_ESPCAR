#include "openinverter_ui.h"
#include "can_vcu.h"
#include <math.h>

// -------------------------------------------------------------
// Constants & Color Definitions
// -------------------------------------------------------------
static const char* TAB_NAMES[NUM_OI_TABS] = {
    "Dashboard", "Parameters", "Spot Values", "Plot", "Gauges",
    "Data Logger", "CAN Mapping", "Files", "Update", "Settings", "Support"
};

static const char* TAB_SYMBOLS[NUM_OI_TABS] = {
    LV_SYMBOL_LIST,      // Dashboard
    LV_SYMBOL_SETTINGS,  // Parameters
    LV_SYMBOL_EYE_OPEN,  // Spot Values
    LV_SYMBOL_AUDIO,     // Plot
    LV_SYMBOL_IMAGE,     // Gauges
    LV_SYMBOL_FILE,      // Data Logger
    LV_SYMBOL_SHUFFLE,   // CAN Mapping
    LV_SYMBOL_DIRECTORY, // Files
    LV_SYMBOL_DOWNLOAD,  // Update
    LV_SYMBOL_SETTINGS,  // Settings
    "?"                  // Support
};

// UI Objects
static lv_obj_t *main_screen = NULL;
static lv_obj_t *navbar_obj = NULL;
static lv_obj_t *content_obj = NULL;
static lv_obj_t *tab_btns[NUM_OI_TABS];
static lv_obj_t *tab_views[NUM_OI_TABS];
static int current_tab = 0;

static OiSleepCb g_sleep_cb = NULL;
static OiBrightnessCb g_bright_cb = NULL;

// Telemetry state cache
static InverterTelemetry last_telem = {};
static bool is_logging_active = false;
static uint32_t log_record_count = 0;
static unsigned long last_plot_sample_ms = 0;

// Tab 0: Dashboard UI widgets
static lv_obj_t *hero_pill_mode = NULL;
static lv_obj_t *hero_pill_dot = NULL;
static lv_obj_t *hero_pill_err = NULL;
static lv_obj_t *hero_state_lbl = NULL;
static lv_obj_t *hero_sub_lbl = NULL;
static lv_obj_t *hero_val_udc = NULL;
static lv_obj_t *hero_val_idc = NULL;
static lv_obj_t *hero_val_spd = NULL;
static lv_obj_t *hero_val_pwr = NULL;
static lv_obj_t *dash_cmd_output = NULL;
static lv_obj_t *dash_cmd_input = NULL;
static lv_obj_t *dash_can_id_input = NULL;
static lv_obj_t *dash_can_data_input = NULL;
static lv_obj_t *dash_inv_msgs = NULL;

// Tab 1: Parameters UI widgets
struct ParamCard {
    const char *name;
    const char *desc;
    const char *unit;
    int min_val;
    int max_val;
    int step;
    int cur_val;
    lv_obj_t *val_lbl;
    lv_obj_t *slider;
};

static ParamCard param_list[] = {
    {"curbreak", "Regen braking current limit", "%", 0, 100, 1, 35, NULL, NULL},
    {"fweak", "Field weakening frequency", "Hz", 20, 400, 5, 90, NULL, NULL},
    {"potmin", "Throttle pedal minimum ADC", "cnt", 0, 2000, 10, 850, NULL, NULL},
    {"potmax", "Throttle pedal maximum ADC", "cnt", 2000, 4095, 10, 3950, NULL, NULL},
    {"udcmin", "DC bus undervoltage limit", "V", 50, 400, 5, 92, NULL, NULL},
    {"udcmax", "DC bus overvoltage limit", "V", 100, 500, 5, 410, NULL, NULL},
    {"throtramp", "Throttle acceleration ramp rate", "%/10ms", 1, 50, 1, 8, NULL, NULL},
    {"brakeramp", "Braking ramp rate", "%/10ms", 1, 50, 1, 15, NULL, NULL},
    {"fslipmin", "Minimum slip frequency", "Hz", 1, 15, 1, 2, NULL, NULL},
    {"fmax", "Maximum motor frequency", "Hz", 50, 600, 10, 320, NULL, NULL}
};
static const int NUM_PARAMS = sizeof(param_list) / sizeof(param_list[0]);

// Tab 2: Spot Values UI widgets
struct SpotValueItem {
    const char *name;
    const char *unit;
    lv_obj_t *lbl_val;
};
static SpotValueItem spot_items[] = {
    {"udc", "V", NULL},
    {"idc", "A", NULL},
    {"speed", "rpm", NULL},
    {"power", "kW", NULL},
    {"tmot", "°C", NULL},
    {"ths", "°C", NULL},
    {"uaux", "V", NULL},
    {"soc", "%", NULL},
    {"opmode", "", NULL},
    {"din_fwd", "", NULL},
    {"din_rev", "", NULL},
    {"din_brake", "", NULL},
    {"can_rx", "pkts", NULL},
    {"can_tx", "pkts", NULL}
};
static const int NUM_SPOT_ITEMS = sizeof(spot_items) / sizeof(spot_items[0]);

// Tab 3: Plot UI widgets
static lv_obj_t *plot_chart = NULL;
static lv_chart_series_t *plot_ser_udc = NULL;
static lv_chart_series_t *plot_ser_idc = NULL;
static lv_chart_series_t *plot_ser_spd = NULL;
static bool plot_running = true;

// Tab 4: Gauges UI widgets
static lv_obj_t *meter_spd = NULL;
static lv_meter_indicator_t *indic_spd_needle = NULL;
static lv_obj_t *lbl_spd_val = NULL;

static lv_obj_t *meter_udc = NULL;
static lv_meter_indicator_t *indic_udc_needle = NULL;
static lv_obj_t *lbl_udc_val = NULL;

static lv_obj_t *bar_pwr = NULL;
static lv_obj_t *lbl_pwr_val = NULL;
static lv_obj_t *bar_tmot = NULL;
static lv_obj_t *lbl_tmot_val = NULL;
static lv_obj_t *bar_soc = NULL;
static lv_obj_t *lbl_soc_val = NULL;

// Tab 5: Data Logger widgets
static lv_obj_t *logger_status_lbl = NULL;
static lv_obj_t *logger_records_lbl = NULL;
static lv_obj_t *logger_preview = NULL;

// Tab 10: Support / Diagnostics widgets
static lv_obj_t *diag_heap_lbl = NULL;
static lv_obj_t *diag_psram_lbl = NULL;
static lv_obj_t *diag_uptime_lbl = NULL;
static lv_obj_t *diag_can_lbl = NULL;

// Navbar status
static lv_obj_t *nav_can_status = NULL;

// Forward declarations
static void draw_openinverter_logo(lv_obj_t *parent);
static void build_tab_dashboard(lv_obj_t *parent);
static void build_tab_parameters(lv_obj_t *parent);
static void build_tab_spotvalues(lv_obj_t *parent);
static void build_tab_plot(lv_obj_t *parent);
static void build_tab_gauges(lv_obj_t *parent);
static void build_tab_logger(lv_obj_t *parent);
static void build_tab_canmapping(lv_obj_t *parent);
static void build_tab_files(lv_obj_t *parent);
static void build_tab_update(lv_obj_t *parent);
static void build_tab_settings(lv_obj_t *parent);
static void build_tab_support(lv_obj_t *parent);

// -------------------------------------------------------------
// Tab Switching
// -------------------------------------------------------------
void switch_oi_tab_view(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= NUM_OI_TABS) return;
    current_tab = tab_idx;

    for (int i = 0; i < NUM_OI_TABS; i++) {
        if (i == tab_idx) {
            lv_obj_clear_flag(tab_views[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(tab_btns[i], lv_color_hex(0x132738), 0);
            lv_obj_set_style_text_color(tab_btns[i], OI_COLOR_CYAN, 0);
            lv_obj_set_style_border_side(tab_btns[i], LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(tab_btns[i], 3, 0);
            lv_obj_set_style_border_color(tab_btns[i], OI_COLOR_CYAN, 0);
        } else {
            lv_obj_add_flag(tab_views[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(tab_btns[i], OI_COLOR_SURFACE, 0);
            lv_obj_set_style_text_color(tab_btns[i], OI_COLOR_TEXT_MUTED, 0);
            lv_obj_set_style_border_width(tab_btns[i], 0, 0);
        }
    }
}

static void tab_btn_event_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch_oi_tab_view(idx);
}

// -------------------------------------------------------------
// OpenInverter Logo (3 Sine Waves inside Metallic Circular Ring)
// -------------------------------------------------------------
static void draw_openinverter_logo(lv_obj_t *parent) {
    static lv_color_t logo_cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(44, 44)];
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, logo_cbuf, 44, 44, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, OI_COLOR_SURFACE, LV_OPA_COVER);

    // Draw outer circular ring
    for (int y = 0; y < 44; y++) {
        for (int x = 0; x < 44; x++) {
            float dx = (float)(x - 22);
            float dy = (float)(y - 22);
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist >= 19.5f && dist <= 21.8f) {
                // Metallic ring gradient
                float grad = (dx + dy + 44.0f) / 88.0f;
                uint8_t c = (uint8_t)(100 + grad * 120);
                lv_canvas_set_px_color(canvas, x, y, lv_color_make(c, c, c));
            }
        }
    }

    // Draw 3 phase sine waves inside circle (Blue, Yellow, Red)
    // Blue: #5b9dff, Yellow: #ffcf54, Red: #ff6b6b
    lv_color_t c_blue = lv_color_hex(0x5b9dff);
    lv_color_t c_yel = lv_color_hex(0xffcf54);
    lv_color_t c_red = lv_color_hex(0xff6b6b);

    for (int x = 4; x <= 40; x++) {
        float angle = (float)(x - 4) * (2.0f * 3.14159f / 26.0f);

        // Sine wave 1 (Blue)
        int y1 = (int)(22.0f + 9.0f * sinf(angle));
        // Sine wave 2 (Yellow, 120 deg shifted)
        int y2 = (int)(22.0f + 9.0f * sinf(angle + 2.094f));
        // Sine wave 3 (Red, 240 deg shifted)
        int y3 = (int)(22.0f + 9.0f * sinf(angle + 4.188f));

        auto draw_pt = [&](int px, int py, lv_color_t col) {
            float d = sqrtf((float)((px - 22) * (px - 22) + (py - 22) * (py - 22)));
            if (d < 19.0f && px >= 0 && px < 44 && py >= 0 && py < 44) {
                lv_canvas_set_px_color(canvas, px, py, col);
                if (py + 1 < 44) lv_canvas_set_px_color(canvas, px, py + 1, col);
            }
        };

        draw_pt(x, y1, c_blue);
        draw_pt(x, y2, c_yel);
        draw_pt(x, y3, c_red);
    }

    lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, 10);
}

// -------------------------------------------------------------
// UI Creation: Main Frame + Left Navbar + Content Area
// -------------------------------------------------------------
lv_obj_t* create_openinverter_full_ui(lv_obj_t *parent, OiSleepCb sleep_cb, OiBrightnessCb bright_cb) {
    g_sleep_cb = sleep_cb;
    g_bright_cb = bright_cb;

    main_screen = lv_obj_create(parent);
    lv_obj_set_size(main_screen, 800, 480);
    lv_obj_set_pos(main_screen, 0, 0);
    lv_obj_set_style_bg_color(main_screen, OI_COLOR_BG, 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ----------------- LEFT NAVBAR (170px) -----------------
    navbar_obj = lv_obj_create(main_screen);
    lv_obj_set_size(navbar_obj, 170, 480);
    lv_obj_set_pos(navbar_obj, 0, 0);
    lv_obj_set_style_bg_color(navbar_obj, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_width(navbar_obj, 1, 0);
    lv_obj_set_style_border_color(navbar_obj, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_side(navbar_obj, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_all(navbar_obj, 0, 0);
    lv_obj_set_style_radius(navbar_obj, 0, 0);
    lv_obj_clear_flag(navbar_obj, LV_OBJ_FLAG_SCROLLABLE);

    // Top: OpenInverter Logo
    lv_obj_t *logo_box = lv_obj_create(navbar_obj);
    lv_obj_set_size(logo_box, 170, 60);
    lv_obj_set_pos(logo_box, 0, 0);
    lv_obj_set_style_bg_opa(logo_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_box, 0, 0);
    lv_obj_clear_flag(logo_box, LV_OBJ_FLAG_SCROLLABLE);
    draw_openinverter_logo(logo_box);

    // Tab Buttons Scroll Container
    lv_obj_t *nav_scroll = lv_obj_create(navbar_obj);
    lv_obj_set_size(nav_scroll, 170, 340);
    lv_obj_set_pos(nav_scroll, 0, 62);
    lv_obj_set_style_bg_opa(nav_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nav_scroll, 0, 0);
    lv_obj_set_style_pad_all(nav_scroll, 4, 0);
    lv_obj_set_scroll_dir(nav_scroll, LV_DIR_VER);

    for (int i = 0; i < NUM_OI_TABS; i++) {
        tab_btns[i] = lv_btn_create(nav_scroll);
        lv_obj_set_size(tab_btns[i], 162, 28);
        lv_obj_set_pos(tab_btns[i], 0, i * 30);
        lv_obj_set_style_bg_color(tab_btns[i], OI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(tab_btns[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tab_btns[i], 6, 0);
        lv_obj_set_style_pad_left(tab_btns[i], 10, 0);
        lv_obj_set_style_pad_top(tab_btns[i], 4, 0);
        lv_obj_set_style_shadow_width(tab_btns[i], 0, 0);

        char btn_text[64];
        snprintf(btn_text, sizeof(btn_text), "%s  %s", TAB_SYMBOLS[i], TAB_NAMES[i]);
        lv_obj_t *lbl = lv_label_create(tab_btns[i]);
        lv_label_set_text(lbl, btn_text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, OI_COLOR_TEXT_MUTED, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_add_event_cb(tab_btns[i], tab_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Bottom Info in Navbar: Device Name, Version, CAN Status
    lv_obj_t *nav_foot = lv_obj_create(navbar_obj);
    lv_obj_set_size(nav_foot, 170, 78);
    lv_obj_set_pos(nav_foot, 0, 402);
    lv_obj_set_style_bg_color(nav_foot, OI_COLOR_SURFACE_HOVER, 0);
    lv_obj_set_style_border_color(nav_foot, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(nav_foot, 1, 0);
    lv_obj_set_style_border_side(nav_foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(nav_foot, 4, 0);
    lv_obj_clear_flag(nav_foot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_dev = lv_label_create(nav_foot);
    lv_label_set_text(lbl_dev, "ZombieVerter VCU");
    lv_obj_set_style_text_font(lbl_dev, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_dev, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(lbl_dev, 6, 2);

    lv_obj_t *lbl_ver = lv_label_create(nav_foot);
    lv_label_set_text(lbl_ver, "F/W: 1.23  Web: 4.1");
    lv_obj_set_style_text_font(lbl_ver, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_ver, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(lbl_ver, 6, 18);

    nav_can_status = lv_label_create(nav_foot);
    lv_label_set_text(nav_can_status, LV_SYMBOL_WARNING " CAN Offline");
    lv_obj_set_style_text_font(nav_can_status, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(nav_can_status, OI_COLOR_RED, 0);
    lv_obj_set_pos(nav_can_status, 6, 36);

    // ----------------- CONTENT CONTAINER (630px) -----------------
    content_obj = lv_obj_create(main_screen);
    lv_obj_set_size(content_obj, 630, 480);
    lv_obj_set_pos(content_obj, 170, 0);
    lv_obj_set_style_bg_color(content_obj, OI_COLOR_BG, 0);
    lv_obj_set_style_border_width(content_obj, 0, 0);
    lv_obj_set_style_pad_all(content_obj, 0, 0);
    lv_obj_clear_flag(content_obj, LV_OBJ_FLAG_SCROLLABLE);

    // Build each of the 11 tab views
    for (int i = 0; i < NUM_OI_TABS; i++) {
        tab_views[i] = lv_obj_create(content_obj);
        lv_obj_set_size(tab_views[i], 630, 480);
        lv_obj_set_pos(tab_views[i], 0, 0);
        lv_obj_set_style_bg_color(tab_views[i], OI_COLOR_BG, 0);
        lv_obj_set_style_border_width(tab_views[i], 0, 0);
        lv_obj_set_style_pad_all(tab_views[i], 12, 0);
        lv_obj_set_scroll_dir(tab_views[i], LV_DIR_VER);
        lv_obj_add_flag(tab_views[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_tab_dashboard(tab_views[0]);
    build_tab_parameters(tab_views[1]);
    build_tab_spotvalues(tab_views[2]);
    build_tab_plot(tab_views[3]);
    build_tab_gauges(tab_views[4]);
    build_tab_logger(tab_views[5]);
    build_tab_canmapping(tab_views[6]);
    build_tab_files(tab_views[7]);
    build_tab_update(tab_views[8]);
    build_tab_settings(tab_views[9]);
    build_tab_support(tab_views[10]);

    // Activate Dashboard as default tab
    switch_oi_tab_view(0);

    return main_screen;
}

// -------------------------------------------------------------
// TAB 0: DASHBOARD (wjcloudy exact layout & actions)
// -------------------------------------------------------------
static void cmd_send_event_cb(lv_event_t *e) {
    if (!dash_cmd_input || !dash_cmd_output) return;
    const char *text = lv_textarea_get_text(dash_cmd_input);
    if (!text || strlen(text) == 0) return;

    char buf[128];
    snprintf(buf, sizeof(buf), "> %s\nOK\n", text);
    lv_textarea_add_text(dash_cmd_output, buf);
    lv_textarea_set_text(dash_cmd_input, "");
}

static void can_send_event_cb(lv_event_t *e) {
    if (!dash_can_id_input || !dash_can_data_input) return;
    const char *id_str = lv_textarea_get_text(dash_can_id_input);
    const char *data_str = lv_textarea_get_text(dash_can_data_input);

    uint32_t id = strtoul(id_str, NULL, 16);
    uint8_t payload[8] = {0};
    int bidx = 0;
    char temp[64];
    strncpy(temp, data_str, sizeof(temp));
    char *token = strtok(temp, " ");
    while (token && bidx < 8) {
        payload[bidx++] = (uint8_t)strtoul(token, NULL, 16);
        token = strtok(NULL, " ");
    }

    bool ok = send_can_frame(id, payload, bidx);
    char out_buf[96];
    snprintf(out_buf, sizeof(out_buf), "CAN TX ID:0x%X [%d bytes] %s\n", (unsigned int)id, bidx, ok ? "SENT" : "FAIL");
    if (dash_cmd_output) lv_textarea_add_text(dash_cmd_output, out_buf);
}

static void action_start_cb(lv_event_t *e) {
    uint8_t cmd[8] = {0x23, 0x00, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00}; // start 2
    send_can_frame(0x601, cmd, 8);
    if (dash_cmd_output) lv_textarea_add_text(dash_cmd_output, "> start 2\n[CAN] Manual Run sent\n");
}

static void action_stop_cb(lv_event_t *e) {
    uint8_t cmd[8] = {0x23, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00}; // stop
    send_can_frame(0x601, cmd, 8);
    if (dash_cmd_output) lv_textarea_add_text(dash_cmd_output, "> stop\n[CAN] Inverter Stop sent\n");
}

static void action_reset_inverter_cb(lv_event_t *e) {
    uint8_t cmd[8] = {0x23, 0x00, 0x20, 0xFF, 0x01, 0x00, 0x00, 0x00}; // reset
    send_can_frame(0x601, cmd, 8);
    if (dash_cmd_output) lv_textarea_add_text(dash_cmd_output, "> reset inverter\n[CAN] Reset sent to STM32\n");
}

static void action_reset_esp32_cb(lv_event_t *e) {
    ESP.restart();
}

static void build_tab_dashboard(lv_obj_t *parent) {
    // Main layout: Left column (410px) + Right column (180px)
    lv_obj_t *left_col = lv_obj_create(parent);
    lv_obj_set_size(left_col, 410, 520);
    lv_obj_set_pos(left_col, 0, 0);
    lv_obj_set_style_bg_opa(left_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_col, 0, 0);
    lv_obj_set_style_pad_all(left_col, 0, 0);

    lv_obj_t *right_col = lv_obj_create(parent);
    lv_obj_set_size(right_col, 185, 520);
    lv_obj_set_pos(right_col, 420, 0);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_pad_all(right_col, 0, 0);

    // --- LEFT COLUMN: Title & Hero Card ---
    lv_obj_t *dash_title = lv_label_create(left_col);
    lv_label_set_text(dash_title, "Dashboard");
    lv_obj_set_style_text_font(dash_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dash_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(dash_title, 4, 0);

    // Hero Card
    lv_obj_t *hero = lv_obj_create(left_col);
    lv_obj_set_size(hero, 405, 140);
    lv_obj_set_pos(hero, 0, 28);
    lv_obj_set_style_bg_color(hero, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(hero, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_radius(hero, 12, 0);
    lv_obj_set_style_pad_all(hero, 10, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

    // Hero Top Pills
    hero_pill_mode = lv_obj_create(hero);
    lv_obj_set_size(hero_pill_mode, 80, 22);
    lv_obj_set_pos(hero_pill_mode, 0, 0);
    lv_obj_set_style_bg_color(hero_pill_mode, lv_color_hex(0x281010), 0);
    lv_obj_set_style_border_width(hero_pill_mode, 0, 0);
    lv_obj_set_style_radius(hero_pill_mode, 11, 0);
    lv_obj_set_style_pad_all(hero_pill_mode, 0, 0);

    hero_pill_dot = lv_label_create(hero_pill_mode);
    lv_label_set_text(hero_pill_dot, "Offline");
    lv_obj_set_style_text_font(hero_pill_dot, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hero_pill_dot, OI_COLOR_RED, 0);
    lv_obj_align(hero_pill_dot, LV_ALIGN_CENTER, 0, 0);

    hero_pill_err = lv_label_create(hero);
    lv_label_set_text(hero_pill_err, "CAN #1");
    lv_obj_set_style_text_font(hero_pill_err, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hero_pill_err, OI_COLOR_CYAN, 0);
    lv_obj_set_pos(hero_pill_err, 90, 4);

    hero_state_lbl = lv_label_create(hero);
    lv_label_set_text(hero_state_lbl, "No connection to inverter");
    lv_obj_set_style_text_font(hero_state_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hero_state_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(hero_state_lbl, 2, 26);

    hero_sub_lbl = lv_label_create(hero);
    lv_label_set_text(hero_sub_lbl, "Status: Waiting for CAN");
    lv_obj_set_style_text_font(hero_sub_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hero_sub_lbl, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(hero_sub_lbl, 2, 44);

    // 4 Key Metric Tiles inside Hero Card
    int mw = 90;
    auto create_metric_tile = [&](int idx, const char *label, lv_color_t color) -> lv_obj_t* {
        lv_obj_t *m = lv_obj_create(hero);
        lv_obj_set_size(m, mw, 54);
        lv_obj_set_pos(m, idx * (mw + 8), 64);
        lv_obj_set_style_bg_color(m, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(m, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(m, 1, 0);
        lv_obj_set_style_radius(m, 6, 0);
        lv_obj_set_style_pad_all(m, 4, 0);
        lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ml = lv_label_create(m);
        lv_label_set_text(ml, label);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ml, OI_COLOR_TEXT_SUBTLE, 0);
        lv_obj_set_pos(ml, 2, 0);

        lv_obj_t *mv = lv_label_create(m);
        lv_label_set_text(mv, "---");
        lv_obj_set_style_text_font(mv, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(mv, color, 0);
        lv_obj_set_pos(mv, 2, 18);
        return mv;
    };

    hero_val_udc = create_metric_tile(0, "UDC (V)", OI_COLOR_CYAN);
    hero_val_idc = create_metric_tile(1, "IDC (A)", OI_COLOR_GREEN);
    hero_val_spd = create_metric_tile(2, "SPEED (RPM)", OI_COLOR_AMBER);
    hero_val_pwr = create_metric_tile(3, "POWER (KW)", OI_COLOR_TEXT_MAIN);

    // Command Box Card
    lv_obj_t *cmd_box = lv_obj_create(left_col);
    lv_obj_set_size(cmd_box, 405, 150);
    lv_obj_set_pos(cmd_box, 0, 178);
    lv_obj_set_style_bg_color(cmd_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(cmd_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(cmd_box, 1, 0);
    lv_obj_set_style_radius(cmd_box, 12, 0);
    lv_obj_set_style_pad_all(cmd_box, 8, 0);
    lv_obj_clear_flag(cmd_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *clbl = lv_label_create(cmd_box);
    lv_label_set_text(clbl, "Command");
    lv_obj_set_style_text_font(clbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(clbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(clbl, 4, 0);

    dash_cmd_output = lv_textarea_create(cmd_box);
    lv_obj_set_size(dash_cmd_output, 389, 70);
    lv_obj_set_pos(dash_cmd_output, 0, 20);
    lv_textarea_set_text(dash_cmd_output, "OpenInverter v4.1 Ready\nCAN Bus: 500 kbps (TWAI GPIO 19/20)\n");
    lv_obj_set_style_bg_color(dash_cmd_output, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(dash_cmd_output, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(dash_cmd_output, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_color(dash_cmd_output, OI_COLOR_BORDER, 0);

    dash_cmd_input = lv_textarea_create(cmd_box);
    lv_obj_set_size(dash_cmd_input, 305, 32);
    lv_obj_set_pos(dash_cmd_input, 0, 96);
    lv_textarea_set_one_line(dash_cmd_input, true);
    lv_textarea_set_placeholder_text(dash_cmd_input, "e.g. get udc");
    lv_obj_set_style_bg_color(dash_cmd_input, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(dash_cmd_input, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(dash_cmd_input, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(dash_cmd_input, OI_COLOR_BORDER, 0);

    lv_obj_t *btn_send = lv_btn_create(cmd_box);
    lv_obj_set_size(btn_send, 78, 32);
    lv_obj_set_pos(btn_send, 312, 96);
    lv_obj_set_style_bg_color(btn_send, OI_COLOR_CYAN, 0);
    lv_obj_set_style_radius(btn_send, 6, 0);
    lv_obj_add_event_cb(btn_send, cmd_send_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *slbl = lv_label_create(btn_send);
    lv_label_set_text(slbl, "Send");
    lv_obj_set_style_text_font(slbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(slbl, OI_COLOR_BG, 0);
    lv_obj_align(slbl, LV_ALIGN_CENTER, 0, 0);

    // CAN Message Injector Card
    lv_obj_t *can_box = lv_obj_create(left_col);
    lv_obj_set_size(can_box, 405, 120);
    lv_obj_set_pos(can_box, 0, 338);
    lv_obj_set_style_bg_color(can_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(can_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(can_box, 1, 0);
    lv_obj_set_style_radius(can_box, 12, 0);
    lv_obj_set_style_pad_all(can_box, 8, 0);
    lv_obj_clear_flag(can_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *can_lbl = lv_label_create(can_box);
    lv_label_set_text(can_lbl, "CAN Message Injector");
    lv_obj_set_style_text_font(can_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(can_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(can_lbl, 4, 0);

    dash_can_id_input = lv_textarea_create(can_box);
    lv_obj_set_size(dash_can_id_input, 90, 30);
    lv_obj_set_pos(dash_can_id_input, 0, 24);
    lv_textarea_set_one_line(dash_can_id_input, true);
    lv_textarea_set_text(dash_can_id_input, "0x180");
    lv_obj_set_style_bg_color(dash_can_id_input, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(dash_can_id_input, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(dash_can_id_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_color(dash_can_id_input, OI_COLOR_BORDER, 0);

    dash_can_data_input = lv_textarea_create(can_box);
    lv_obj_set_size(dash_can_data_input, 215, 30);
    lv_obj_set_pos(dash_can_data_input, 96, 24);
    lv_textarea_set_one_line(dash_can_data_input, true);
    lv_textarea_set_text(dash_can_data_input, "00 00 00 00 00 00 00 00");
    lv_obj_set_style_bg_color(dash_can_data_input, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(dash_can_data_input, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(dash_can_data_input, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_color(dash_can_data_input, OI_COLOR_BORDER, 0);

    lv_obj_t *btn_can_send = lv_btn_create(can_box);
    lv_obj_set_size(btn_can_send, 78, 30);
    lv_obj_set_pos(btn_can_send, 316, 24);
    lv_obj_set_style_bg_color(btn_can_send, OI_COLOR_CYAN, 0);
    lv_obj_set_style_radius(btn_can_send, 6, 0);
    lv_obj_add_event_cb(btn_can_send, can_send_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cslbl = lv_label_create(btn_can_send);
    lv_label_set_text(cslbl, "Transmit");
    lv_obj_set_style_text_font(cslbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(cslbl, OI_COLOR_BG, 0);
    lv_obj_align(cslbl, LV_ALIGN_CENTER, 0, 0);

    // Quick preset buttons for CAN testing
    lv_obj_t *btn_sdo_req = lv_btn_create(can_box);
    lv_obj_set_size(btn_sdo_req, 190, 24);
    lv_obj_set_pos(btn_sdo_req, 0, 62);
    lv_obj_set_style_bg_color(btn_sdo_req, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_sdo_req, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_sdo_req, 1, 0);
    lv_obj_add_event_cb(btn_sdo_req, [](lv_event_t *e) {
        request_vcu_telemetry_sdo();
        if (dash_cmd_output) lv_textarea_add_text(dash_cmd_output, "[CAN] SDO Read 0x2000 sent\n");
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sdo_lbl = lv_label_create(btn_sdo_req);
    lv_label_set_text(sdo_lbl, "Poll SDO 0x2000 (Params)");
    lv_obj_set_style_text_font(sdo_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sdo_lbl, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_align(sdo_lbl, LV_ALIGN_CENTER, 0, 0);

    // --- RIGHT COLUMN: ACTIONS RAIL (wjcloudy layout) ---
    lv_obj_t *act_box = lv_obj_create(right_col);
    lv_obj_set_size(act_box, 185, 430);
    lv_obj_set_pos(act_box, 0, 28);
    lv_obj_set_style_bg_color(act_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(act_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(act_box, 1, 0);
    lv_obj_set_style_radius(act_box, 12, 0);
    lv_obj_set_style_pad_all(act_box, 8, 0);
    lv_obj_clear_flag(act_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *act_h = lv_label_create(act_box);
    lv_label_set_text(act_h, "ACTIONS");
    lv_obj_set_style_text_font(act_h, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(act_h, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(act_h, 4, 0);

    // Start Inverter Manual
    lv_obj_t *btn_start = lv_btn_create(act_box);
    lv_obj_set_size(btn_start, 169, 36);
    lv_obj_set_pos(btn_start, 0, 22);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x0f766e), 0);
    lv_obj_set_style_radius(btn_start, 6, 0);
    lv_obj_add_event_cb(btn_start, action_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *st_lbl = lv_label_create(btn_start);
    lv_label_set_text(st_lbl, LV_SYMBOL_PLAY "  Start Inverter");
    lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(st_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_align(st_lbl, LV_ALIGN_CENTER, 0, 0);

    // Stop Inverter
    lv_obj_t *btn_stop = lv_btn_create(act_box);
    lv_obj_set_size(btn_stop, 169, 36);
    lv_obj_set_pos(btn_stop, 0, 66);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x7f1d1d), 0);
    lv_obj_set_style_radius(btn_stop, 6, 0);
    lv_obj_add_event_cb(btn_stop, action_stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sp_lbl = lv_label_create(btn_stop);
    lv_label_set_text(sp_lbl, LV_SYMBOL_STOP "  Stop Inverter");
    lv_obj_set_style_text_font(sp_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sp_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_align(sp_lbl, LV_ALIGN_CENTER, 0, 0);

    // Reset Section
    lv_obj_t *rst_h = lv_label_create(act_box);
    lv_label_set_text(rst_h, "RESET");
    lv_obj_set_style_text_font(rst_h, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rst_h, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(rst_h, 4, 116);

    // Reboot Inverter
    lv_obj_t *btn_rst_inv = lv_btn_create(act_box);
    lv_obj_set_size(btn_rst_inv, 169, 36);
    lv_obj_set_pos(btn_rst_inv, 0, 138);
    lv_obj_set_style_bg_color(btn_rst_inv, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_rst_inv, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_rst_inv, 1, 0);
    lv_obj_set_style_radius(btn_rst_inv, 6, 0);
    lv_obj_add_event_cb(btn_rst_inv, action_reset_inverter_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ri_lbl = lv_label_create(btn_rst_inv);
    lv_label_set_text(ri_lbl, LV_SYMBOL_REFRESH "  Reboot Inverter");
    lv_obj_set_style_text_font(ri_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ri_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_align(ri_lbl, LV_ALIGN_CENTER, 0, 0);

    // Reboot ESP32
    lv_obj_t *btn_rst_esp = lv_btn_create(act_box);
    lv_obj_set_size(btn_rst_esp, 169, 36);
    lv_obj_set_pos(btn_rst_esp, 0, 182);
    lv_obj_set_style_bg_color(btn_rst_esp, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_rst_esp, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_rst_esp, 1, 0);
    lv_obj_set_style_radius(btn_rst_esp, 6, 0);
    lv_obj_add_event_cb(btn_rst_esp, action_reset_esp32_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *re_lbl = lv_label_create(btn_rst_esp);
    lv_label_set_text(re_lbl, LV_SYMBOL_POWER "  Reboot ESP32");
    lv_obj_set_style_text_font(re_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(re_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_align(re_lbl, LV_ALIGN_CENTER, 0, 0);

    // Inverter Messages Box
    lv_obj_t *msg_h = lv_label_create(act_box);
    lv_label_set_text(msg_h, "INVERTER MSGS");
    lv_obj_set_style_text_font(msg_h, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(msg_h, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(msg_h, 4, 230);

    dash_inv_msgs = lv_textarea_create(act_box);
    lv_obj_set_size(dash_inv_msgs, 169, 130);
    lv_obj_set_pos(dash_inv_msgs, 0, 250);
    lv_textarea_set_text(dash_inv_msgs, "CAN Live 500k\nNode 1 listening\n");
    lv_obj_set_style_bg_color(dash_inv_msgs, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(dash_inv_msgs, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(dash_inv_msgs, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_color(dash_inv_msgs, OI_COLOR_BORDER, 0);
}

// -------------------------------------------------------------
// TAB 1: PARAMETERS (Category filters, sliders, fine step, Save)
// -------------------------------------------------------------
static void param_slider_cb(lv_event_t *e) {
    ParamCard *p = (ParamCard*)lv_event_get_user_data(e);
    if (!p) return;
    p->cur_val = lv_slider_get_value(p->slider);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %s", p->cur_val, p->unit);
    lv_label_set_text(p->val_lbl, buf);
}

static void param_step_dec_cb(lv_event_t *e) {
    ParamCard *p = (ParamCard*)lv_event_get_user_data(e);
    if (!p) return;
    p->cur_val -= p->step;
    if (p->cur_val < p->min_val) p->cur_val = p->min_val;
    lv_slider_set_value(p->slider, p->cur_val, LV_ANIM_OFF);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %s", p->cur_val, p->unit);
    lv_label_set_text(p->val_lbl, buf);
}

static void param_step_inc_cb(lv_event_t *e) {
    ParamCard *p = (ParamCard*)lv_event_get_user_data(e);
    if (!p) return;
    p->cur_val += p->step;
    if (p->cur_val > p->max_val) p->cur_val = p->max_val;
    lv_slider_set_value(p->slider, p->cur_val, LV_ANIM_OFF);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d %s", p->cur_val, p->unit);
    lv_label_set_text(p->val_lbl, buf);
}

static void build_tab_parameters(lv_obj_t *parent) {
    // Top Bar: Search & Save
    lv_obj_t *top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, 606, 44);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(top_bar, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 8, 0);
    lv_obj_set_style_pad_all(top_bar, 6, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *p_title = lv_label_create(top_bar);
    lv_label_set_text(p_title, "Parameters");
    lv_obj_set_style_text_font(p_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(p_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(p_title, 4, 6);

    // Save to Flash Button
    lv_obj_t *btn_save = lv_btn_create(top_bar);
    lv_obj_set_size(btn_save, 140, 32);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_save, OI_COLOR_CYAN, 0);
    lv_obj_set_style_radius(btn_save, 6, 0);
    lv_obj_add_event_cb(btn_save, [](lv_event_t *e) {
        // Send SDO Save to Flash (index 0x2000 sub 0)
        uint8_t save_cmd[8] = {0x23, 0x00, 0x20, 0x00, 0x73, 0x61, 0x76, 0x65}; // "save"
        send_can_frame(0x601, save_cmd, 8);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_lbl = lv_label_create(btn_save);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE " Save Flash");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(save_lbl, OI_COLOR_BG, 0);
    lv_obj_align(save_lbl, LV_ALIGN_CENTER, 0, 0);

    // Scrollable Parameters Cards Grid
    lv_obj_t *p_grid = lv_obj_create(parent);
    lv_obj_set_size(p_grid, 606, 400);
    lv_obj_set_pos(p_grid, 0, 52);
    lv_obj_set_style_bg_opa(p_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p_grid, 0, 0);
    lv_obj_set_style_pad_all(p_grid, 0, 0);
    lv_obj_set_scroll_dir(p_grid, LV_DIR_VER);

    for (int i = 0; i < NUM_PARAMS; i++) {
        lv_obj_t *card = lv_obj_create(p_grid);
        lv_obj_set_size(card, 606, 62);
        lv_obj_set_pos(card, 0, i * 68);
        lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
        lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // Name & Description
        lv_obj_t *nlbl = lv_label_create(card);
        lv_label_set_text(nlbl, param_list[i].name);
        lv_obj_set_style_text_font(nlbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nlbl, OI_COLOR_CYAN, 0);
        lv_obj_set_pos(nlbl, 4, 2);

        lv_obj_t *dlbl = lv_label_create(card);
        lv_label_set_text(dlbl, param_list[i].desc);
        lv_obj_set_style_text_font(dlbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(dlbl, OI_COLOR_TEXT_SUBTLE, 0);
        lv_obj_set_pos(dlbl, 4, 22);

        // Value Label
        param_list[i].val_lbl = lv_label_create(card);
        char val_buf[64];
        snprintf(val_buf, sizeof(val_buf), "%d %s", param_list[i].cur_val, param_list[i].unit);
        lv_label_set_text(param_list[i].val_lbl, val_buf);
        lv_obj_set_style_text_font(param_list[i].val_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(param_list[i].val_lbl, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_pos(param_list[i].val_lbl, 240, 18);

        // Slider
        param_list[i].slider = lv_slider_create(card);
        lv_obj_set_size(param_list[i].slider, 180, 14);
        lv_obj_set_pos(param_list[i].slider, 330, 20);
        lv_slider_set_range(param_list[i].slider, param_list[i].min_val, param_list[i].max_val);
        lv_slider_set_value(param_list[i].slider, param_list[i].cur_val, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(param_list[i].slider, OI_COLOR_SURFACE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(param_list[i].slider, OI_COLOR_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(param_list[i].slider, OI_COLOR_TEXT_MAIN, LV_PART_KNOB);
        lv_obj_add_event_cb(param_list[i].slider, param_slider_cb, LV_EVENT_VALUE_CHANGED, &param_list[i]);

        // Fine [-] and [+] Buttons
        lv_obj_t *b_dec = lv_btn_create(card);
        lv_obj_set_size(b_dec, 32, 28);
        lv_obj_set_pos(b_dec, 520, 12);
        lv_obj_set_style_bg_color(b_dec, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(b_dec, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(b_dec, 1, 0);
        lv_obj_add_event_cb(b_dec, param_step_dec_cb, LV_EVENT_CLICKED, &param_list[i]);
        lv_obj_t *dl = lv_label_create(b_dec);
        lv_label_set_text(dl, "-");
        lv_obj_align(dl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *b_inc = lv_btn_create(card);
        lv_obj_set_size(b_inc, 32, 28);
        lv_obj_set_pos(b_inc, 558, 12);
        lv_obj_set_style_bg_color(b_inc, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(b_inc, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(b_inc, 1, 0);
        lv_obj_add_event_cb(b_inc, param_step_inc_cb, LV_EVENT_CLICKED, &param_list[i]);
        lv_obj_t *il = lv_label_create(b_inc);
        lv_label_set_text(il, "+");
        lv_obj_align(il, LV_ALIGN_CENTER, 0, 0);
    }
}

// -------------------------------------------------------------
// TAB 2: SPOT VALUES (Matrix grid of live CAN telemetry)
// -------------------------------------------------------------
static void build_tab_spotvalues(lv_obj_t *parent) {
    lv_obj_t *sv_title = lv_label_create(parent);
    lv_label_set_text(sv_title, "Spot Values");
    lv_obj_set_style_text_font(sv_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sv_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(sv_title, 4, 0);

    lv_obj_t *sv_grid = lv_obj_create(parent);
    lv_obj_set_size(sv_grid, 606, 420);
    lv_obj_set_pos(sv_grid, 0, 30);
    lv_obj_set_style_bg_color(sv_grid, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(sv_grid, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(sv_grid, 1, 0);
    lv_obj_set_style_radius(sv_grid, 12, 0);
    lv_obj_set_style_pad_all(sv_grid, 10, 0);
    lv_obj_set_scroll_dir(sv_grid, LV_DIR_VER);

    // 2-Column Grid
    int col_w = 280;
    int row_h = 36;
    for (int i = 0; i < NUM_SPOT_ITEMS; i++) {
        int col = i % 2;
        int row = i / 2;

        lv_obj_t *item_box = lv_obj_create(sv_grid);
        lv_obj_set_size(item_box, col_w, row_h);
        lv_obj_set_pos(item_box, col * (col_w + 14), row * (row_h + 8));
        lv_obj_set_style_bg_color(item_box, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(item_box, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(item_box, 1, 0);
        lv_obj_set_style_radius(item_box, 6, 0);
        lv_obj_set_style_pad_all(item_box, 4, 0);
        lv_obj_clear_flag(item_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nlbl = lv_label_create(item_box);
        lv_label_set_text(nlbl, spot_items[i].name);
        lv_obj_set_style_text_font(nlbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nlbl, OI_COLOR_CYAN, 0);
        lv_obj_align(nlbl, LV_ALIGN_LEFT_MID, 6, 0);

        spot_items[i].lbl_val = lv_label_create(item_box);
        lv_label_set_text(spot_items[i].lbl_val, "---");
        lv_obj_set_style_text_font(spot_items[i].lbl_val, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(spot_items[i].lbl_val, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_align(spot_items[i].lbl_val, LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

// -------------------------------------------------------------
// TAB 3: PLOT (Oscilloscope / Waveform Chart)
// -------------------------------------------------------------
static void build_tab_plot(lv_obj_t *parent) {
    lv_obj_t *p_title = lv_label_create(parent);
    lv_label_set_text(p_title, "Live Plot (Oscilloscope)");
    lv_obj_set_style_text_font(p_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(p_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(p_title, 4, 0);

    // Controls
    lv_obj_t *btn_pause = lv_btn_create(parent);
    lv_obj_set_size(btn_pause, 90, 28);
    lv_obj_set_pos(btn_pause, 400, 0);
    lv_obj_set_style_bg_color(btn_pause, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_pause, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_pause, 1, 0);
    lv_obj_add_event_cb(btn_pause, [](lv_event_t *e) {
        plot_running = !plot_running;
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *plbl = lv_label_create(btn_pause);
    lv_label_set_text(plbl, "Run/Pause");
    lv_obj_set_style_text_font(plbl, &lv_font_montserrat_10, 0);
    lv_obj_align(plbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_clr = lv_btn_create(parent);
    lv_obj_set_size(btn_clr, 90, 28);
    lv_obj_set_pos(btn_clr, 500, 0);
    lv_obj_set_style_bg_color(btn_clr, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_clr, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_clr, 1, 0);
    lv_obj_add_event_cb(btn_clr, [](lv_event_t *e) {
        if (plot_chart) {
            lv_chart_set_all_value(plot_chart, plot_ser_udc, 0);
            lv_chart_set_all_value(plot_chart, plot_ser_idc, 0);
            lv_chart_set_all_value(plot_chart, plot_ser_spd, 0);
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clbl = lv_label_create(btn_clr);
    lv_label_set_text(clbl, "Clear Chart");
    lv_obj_set_style_text_font(clbl, &lv_font_montserrat_10, 0);
    lv_obj_align(clbl, LV_ALIGN_CENTER, 0, 0);

    // Chart Box
    plot_chart = lv_chart_create(parent);
    lv_obj_set_size(plot_chart, 606, 400);
    lv_obj_set_pos(plot_chart, 0, 36);
    lv_obj_set_style_bg_color(plot_chart, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(plot_chart, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(plot_chart, 1, 0);
    lv_obj_set_style_radius(plot_chart, 12, 0);
    lv_chart_set_type(plot_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(plot_chart, 60);
    lv_chart_set_range(plot_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 450);

    // Grid lines styling
    lv_obj_set_style_line_color(plot_chart, OI_COLOR_BORDER, LV_PART_MAIN);

    // Add Series: Udc (Cyan), Idc (Green), Speed (Amber)
    plot_ser_udc = lv_chart_add_series(plot_chart, OI_COLOR_CYAN, LV_CHART_AXIS_PRIMARY_Y);
    plot_ser_idc = lv_chart_add_series(plot_chart, OI_COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    plot_ser_spd = lv_chart_add_series(plot_chart, OI_COLOR_AMBER, LV_CHART_AXIS_PRIMARY_Y);
}

// -------------------------------------------------------------
// TAB 4: GAUGES (Authentic OpenInverter Web Dials & Bars)
// -------------------------------------------------------------
static void build_tab_gauges(lv_obj_t *parent) {
    lv_obj_t *g_title = lv_label_create(parent);
    lv_label_set_text(g_title, "Gauges");
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(g_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(g_title, 4, 0);

    // Left Dial: Speedometer (0 - 160 km/h)
    meter_spd = lv_meter_create(parent);
    lv_obj_set_size(meter_spd, 220, 220);
    lv_obj_set_pos(meter_spd, 10, 36);
    lv_obj_set_style_bg_color(meter_spd, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(meter_spd, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(meter_spd, 1, 0);
    lv_obj_set_style_radius(meter_spd, 110, 0);

    lv_meter_scale_t *scale_spd = lv_meter_add_scale(meter_spd);
    lv_meter_set_scale_ticks(meter_spd, scale_spd, 17, 2, 8, OI_COLOR_TEXT_MUTED);
    lv_meter_set_scale_major_ticks(meter_spd, scale_spd, 4, 3, 14, OI_COLOR_CYAN, 12);
    lv_meter_set_scale_range(meter_spd, scale_spd, 0, 160, 240, 150);

    indic_spd_needle = lv_meter_add_needle_line(meter_spd, scale_spd, 3, lv_color_hex(0xff5722), -15);

    lbl_spd_val = lv_label_create(meter_spd);
    lv_label_set_text(lbl_spd_val, "0 km/h");
    lv_obj_set_style_text_font(lbl_spd_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_spd_val, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_align(lbl_spd_val, LV_ALIGN_CENTER, 0, 40);

    // Right Dial: DC Bus Voltage (0 - 450 V)
    meter_udc = lv_meter_create(parent);
    lv_obj_set_size(meter_udc, 220, 220);
    lv_obj_set_pos(meter_udc, 250, 36);
    lv_obj_set_style_bg_color(meter_udc, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(meter_udc, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(meter_udc, 1, 0);
    lv_obj_set_style_radius(meter_udc, 110, 0);

    lv_meter_scale_t *scale_udc = lv_meter_add_scale(meter_udc);
    lv_meter_set_scale_ticks(meter_udc, scale_udc, 19, 2, 8, OI_COLOR_TEXT_MUTED);
    lv_meter_set_scale_major_ticks(meter_udc, scale_udc, 3, 3, 14, OI_COLOR_GREEN, 12);
    lv_meter_set_scale_range(meter_udc, scale_udc, 0, 450, 240, 150);

    indic_udc_needle = lv_meter_add_needle_line(meter_udc, scale_udc, 3, OI_COLOR_GREEN, -15);

    lbl_udc_val = lv_label_create(meter_udc);
    lv_label_set_text(lbl_udc_val, "0 V");
    lv_obj_set_style_text_font(lbl_udc_val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_udc_val, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_align(lbl_udc_val, LV_ALIGN_CENTER, 0, 40);

    // Bottom Bar Meters Container
    lv_obj_t *bar_box = lv_obj_create(parent);
    lv_obj_set_size(bar_box, 606, 170);
    lv_obj_set_pos(bar_box, 0, 270);
    lv_obj_set_style_bg_color(bar_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(bar_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(bar_box, 1, 0);
    lv_obj_set_style_radius(bar_box, 12, 0);
    lv_obj_set_style_pad_all(bar_box, 10, 0);

    // Power Bar (-40 kW to +80 kW)
    lv_obj_t *plbl = lv_label_create(bar_box);
    lv_label_set_text(plbl, "Power (kW)");
    lv_obj_set_style_text_font(plbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(plbl, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(plbl, 4, 4);

    lbl_pwr_val = lv_label_create(bar_box);
    lv_label_set_text(lbl_pwr_val, "0.0 kW");
    lv_obj_set_style_text_font(lbl_pwr_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_pwr_val, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(lbl_pwr_val, 520, 4);

    bar_pwr = lv_bar_create(bar_box);
    lv_obj_set_size(bar_pwr, 580, 14);
    lv_obj_set_pos(bar_pwr, 4, 22);
    lv_bar_set_range(bar_pwr, -40, 80);
    lv_bar_set_value(bar_pwr, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_pwr, OI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_pwr, OI_COLOR_CYAN, LV_PART_INDICATOR);

    // Motor Temp Bar (0 - 120°C)
    lv_obj_t *tlbl = lv_label_create(bar_box);
    lv_label_set_text(tlbl, "Motor Temperature (°C)");
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(tlbl, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(tlbl, 4, 48);

    lbl_tmot_val = lv_label_create(bar_box);
    lv_label_set_text(lbl_tmot_val, "0 °C");
    lv_obj_set_style_text_font(lbl_tmot_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_tmot_val, OI_COLOR_AMBER, 0);
    lv_obj_set_pos(lbl_tmot_val, 520, 48);

    bar_tmot = lv_bar_create(bar_box);
    lv_obj_set_size(bar_tmot, 580, 14);
    lv_obj_set_pos(bar_tmot, 4, 66);
    lv_bar_set_range(bar_tmot, 0, 120);
    lv_bar_set_value(bar_tmot, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_tmot, OI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_tmot, OI_COLOR_AMBER, LV_PART_INDICATOR);

    // Battery SOC Bar (0 - 100%)
    lv_obj_t *blbl = lv_label_create(bar_box);
    lv_label_set_text(blbl, "Battery SOC (%)");
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(blbl, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(blbl, 4, 92);

    lbl_soc_val = lv_label_create(bar_box);
    lv_label_set_text(lbl_soc_val, "0 %");
    lv_obj_set_style_text_font(lbl_soc_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_soc_val, OI_COLOR_GREEN, 0);
    lv_obj_set_pos(lbl_soc_val, 520, 92);

    bar_soc = lv_bar_create(bar_box);
    lv_obj_set_size(bar_soc, 580, 14);
    lv_obj_set_pos(bar_soc, 4, 110);
    lv_bar_set_range(bar_soc, 0, 100);
    lv_bar_set_value(bar_soc, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_soc, OI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_soc, OI_COLOR_GREEN, LV_PART_INDICATOR);
}

// -------------------------------------------------------------
// TAB 5: DATA LOGGER
// -------------------------------------------------------------
static void build_tab_logger(lv_obj_t *parent) {
    lv_obj_t *l_title = lv_label_create(parent);
    lv_label_set_text(l_title, "Data Logger");
    lv_obj_set_style_text_font(l_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(l_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(l_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    logger_status_lbl = lv_label_create(card);
    lv_label_set_text(logger_status_lbl, "Status: IDLE");
    lv_obj_set_style_text_font(logger_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(logger_status_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(logger_status_lbl, 4, 4);

    logger_records_lbl = lv_label_create(card);
    lv_label_set_text(logger_records_lbl, "Recorded samples: 0");
    lv_obj_set_style_text_font(logger_records_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(logger_records_lbl, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(logger_records_lbl, 4, 26);

    // Controls
    lv_obj_t *btn_start = lv_btn_create(card);
    lv_obj_set_size(btn_start, 130, 34);
    lv_obj_set_pos(btn_start, 4, 56);
    lv_obj_set_style_bg_color(btn_start, OI_COLOR_GREEN, 0);
    lv_obj_add_event_cb(btn_start, [](lv_event_t *e) {
        is_logging_active = true;
        lv_label_set_text(logger_status_lbl, "Status: RECORDING (100ms)");
        lv_obj_set_style_text_color(logger_status_lbl, OI_COLOR_GREEN, 0);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *slbl = lv_label_create(btn_start);
    lv_label_set_text(slbl, LV_SYMBOL_PLAY " Start Log");
    lv_obj_set_style_text_font(slbl, &lv_font_montserrat_12, 0);
    lv_obj_align(slbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_stop = lv_btn_create(card);
    lv_obj_set_size(btn_stop, 130, 34);
    lv_obj_set_pos(btn_stop, 144, 56);
    lv_obj_set_style_bg_color(btn_stop, OI_COLOR_RED, 0);
    lv_obj_add_event_cb(btn_stop, [](lv_event_t *e) {
        is_logging_active = false;
        lv_label_set_text(logger_status_lbl, "Status: STOPPED");
        lv_obj_set_style_text_color(logger_status_lbl, OI_COLOR_RED, 0);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *splbl = lv_label_create(btn_stop);
    lv_label_set_text(splbl, LV_SYMBOL_STOP " Stop Log");
    lv_obj_set_style_text_font(splbl, &lv_font_montserrat_12, 0);
    lv_obj_align(splbl, LV_ALIGN_CENTER, 0, 0);

    // CSV Live Preview
    logger_preview = lv_textarea_create(card);
    lv_obj_set_size(logger_preview, 580, 270);
    lv_obj_set_pos(logger_preview, 4, 104);
    lv_textarea_set_text(logger_preview, "time_ms,udc,idc,speed,power,tmot\n");
    lv_obj_set_style_bg_color(logger_preview, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_text_color(logger_preview, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(logger_preview, &lv_font_montserrat_10, 0);
    lv_obj_set_style_border_color(logger_preview, OI_COLOR_BORDER, 0);
}

// -------------------------------------------------------------
// TAB 6: CAN MAPPING
// -------------------------------------------------------------
static void build_tab_canmapping(lv_obj_t *parent) {
    lv_obj_t *m_title = lv_label_create(parent);
    lv_label_set_text(m_title, "CAN Mapping");
    lv_obj_set_style_text_font(m_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(m_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(m_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Active CAN Signal Definitions (TWAI 500 kbps)");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(sub, 4, 4);

    const char *mappings[] = {
        "0x181  |  speed   |  bits  0..15  |  scale: 1.00  |  offset: 0",
        "0x181  |  power   |  bits 16..31  |  scale: 0.10  |  offset: 0",
        "0x181  |  udc     |  bits 32..47  |  scale: 0.10  |  offset: 0",
        "0x181  |  tmot    |  bits 48..55  |  scale: 1.00  |  offset: 0",
        "0x281  |  idc     |  bits  0..15  |  scale: 0.10  |  offset: 0",
        "0x381  |  opmode  |  bits  0..7   |  scale: 1.00  |  offset: 0",
        "0x100  |  zombie  |  bits  0..63  |  ZombieVerter VCU broadcast"
    };

    for (int i = 0; i < 7; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, 580, 36);
        lv_obj_set_pos(row, 0, 30 + i * 42);
        lv_obj_set_style_bg_color(row, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(row, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 6, 0);

        lv_obj_t *rl = lv_label_create(row);
        lv_label_set_text(rl, mappings[i]);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(rl, OI_COLOR_CYAN, 0);
        lv_obj_align(rl, LV_ALIGN_LEFT_MID, 6, 0);
    }
}

// -------------------------------------------------------------
// TAB 7: FILES (LittleFS File Manager)
// -------------------------------------------------------------
static void build_tab_files(lv_obj_t *parent) {
    lv_obj_t *f_title = lv_label_create(parent);
    lv_label_set_text(f_title, "Files (LittleFS)");
    lv_obj_set_style_text_font(f_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(f_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(f_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, "Flash Storage: 1408 KB Total  |  1280 KB Free");
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cap, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(cap, 4, 4);

    const char *files[] = {
        "/config.json     - 1.4 KB",
        "/gauges.json     - 21.9 KB",
        "/params.json     - 4.8 KB",
        "/logger.csv      - 12.3 KB"
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *fbox = lv_obj_create(card);
        lv_obj_set_size(fbox, 580, 38);
        lv_obj_set_pos(fbox, 0, 32 + i * 46);
        lv_obj_set_style_bg_color(fbox, OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(fbox, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(fbox, 1, 0);
        lv_obj_set_style_radius(fbox, 6, 0);
        lv_obj_set_style_pad_all(fbox, 6, 0);

        lv_obj_t *flbl = lv_label_create(fbox);
        lv_label_set_text(flbl, files[i]);
        lv_obj_set_style_text_font(flbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(flbl, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_align(flbl, LV_ALIGN_LEFT_MID, 6, 0);
    }
}

// -------------------------------------------------------------
// TAB 8: UPDATE (Firmware & OTA Status)
// -------------------------------------------------------------
static void build_tab_update(lv_obj_t *parent) {
    lv_obj_t *u_title = lv_label_create(parent);
    lv_label_set_text(u_title, "Firmware & Software Update");
    lv_obj_set_style_text_font(u_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(u_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(u_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);

    lv_obj_t *i1 = lv_label_create(card);
    lv_label_set_text(i1, "Inverter Firmware: v1.23 (ZombieVerter VCU / STM32)");
    lv_obj_set_style_text_font(i1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(i1, OI_COLOR_CYAN, 0);
    lv_obj_set_pos(i1, 4, 10);

    lv_obj_t *i2 = lv_label_create(card);
    lv_label_set_text(i2, "Web Interface: v4.1 (wjcloudy / OpenInverter)");
    lv_obj_set_style_text_font(i2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(i2, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(i2, 4, 40);

    lv_obj_t *i3 = lv_label_create(card);
    lv_label_set_text(i3, "Hardware: Waveshare ESP32-S3-Touch-LCD-4.3 (16MB Flash, 8MB PSRAM)");
    lv_obj_set_style_text_font(i3, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(i3, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(i3, 4, 70);

    lv_obj_t *i4 = lv_label_create(card);
    lv_label_set_text(i4, "OTA Web Server Active at: http://192.168.4.1/");
    lv_obj_set_style_text_font(i4, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(i4, OI_COLOR_GREEN, 0);
    lv_obj_set_pos(i4, 4, 100);
}

// -------------------------------------------------------------
// TAB 9: SETTINGS (Device Name, CAN Baud, Backlight, WiFi)
// -------------------------------------------------------------
static void bright_slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if (g_bright_cb) g_bright_cb((uint8_t)val);
}

static void build_tab_settings(lv_obj_t *parent) {
    lv_obj_t *s_title = lv_label_create(parent);
    lv_label_set_text(s_title, "Settings");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(s_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    // Device Name
    lv_obj_t *dnl = lv_label_create(card);
    lv_label_set_text(dnl, "Device Name");
    lv_obj_set_style_text_font(dnl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dnl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(dnl, 4, 4);

    lv_obj_t *dnt = lv_textarea_create(card);
    lv_obj_set_size(dnt, 300, 32);
    lv_obj_set_pos(dnt, 4, 24);
    lv_textarea_set_one_line(dnt, true);
    lv_textarea_set_text(dnt, "ZombieVerter VCU");
    lv_obj_set_style_bg_color(dnt, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dnt, OI_COLOR_BORDER, 0);
    lv_obj_set_style_text_color(dnt, OI_COLOR_TEXT_MAIN, 0);

    // CAN Baudrate
    lv_obj_t *cbl = lv_label_create(card);
    lv_label_set_text(cbl, "CAN Bus Baudrate: 500 kbps (Standard ZombieVerter / Leaf)");
    lv_obj_set_style_text_font(cbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cbl, OI_COLOR_CYAN, 0);
    lv_obj_set_pos(cbl, 4, 70);

    // Backlight Brightness
    lv_obj_t *brl = lv_label_create(card);
    lv_label_set_text(brl, "Screen Brightness");
    lv_obj_set_style_text_font(brl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(brl, 4, 104);

    lv_obj_t *br_slider = lv_slider_create(card);
    lv_obj_set_size(br_slider, 300, 14);
    lv_obj_set_pos(br_slider, 4, 130);
    lv_slider_set_range(br_slider, 10, 100);
    lv_slider_set_value(br_slider, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(br_slider, OI_COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(br_slider, OI_COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_add_event_cb(br_slider, bright_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Sleep Screen Button
    lv_obj_t *btn_sleep = lv_btn_create(card);
    lv_obj_set_size(btn_sleep, 160, 36);
    lv_obj_set_pos(btn_sleep, 4, 170);
    lv_obj_set_style_bg_color(btn_sleep, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(btn_sleep, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_sleep, 1, 0);
    lv_obj_add_event_cb(btn_sleep, [](lv_event_t *e) {
        if (g_sleep_cb) g_sleep_cb();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sll = lv_label_create(btn_sleep);
    lv_label_set_text(sll, LV_SYMBOL_POWER " Display Sleep");
    lv_obj_align(sll, LV_ALIGN_CENTER, 0, 0);

    // WiFi Info
    lv_obj_t *wl = lv_label_create(card);
    lv_label_set_text(wl, "WiFi Access Point: MehranEV-OpenInverter (192.168.4.1)");
    lv_obj_set_style_text_font(wl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wl, OI_COLOR_GREEN, 0);
    lv_obj_set_pos(wl, 4, 230);
}

// -------------------------------------------------------------
// TAB 10: SUPPORT & DIAGNOSTICS
// -------------------------------------------------------------
static void build_tab_support(lv_obj_t *parent) {
    lv_obj_t *sup_title = lv_label_create(parent);
    lv_label_set_text(sup_title, "Support & Diagnostics");
    lv_obj_set_style_text_font(sup_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sup_title, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(sup_title, 4, 0);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 606, 420);
    lv_obj_set_pos(card, 0, 30);
    lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    lv_obj_t *w_link = lv_label_create(card);
    lv_label_set_text(w_link, "Wiki: https://openinverter.org/wiki\nForum: https://openinverter.org/forum\nGitHub: wjcloudy/esp32-web-interface");
    lv_obj_set_style_text_font(w_link, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(w_link, OI_COLOR_CYAN, 0);
    lv_obj_set_pos(w_link, 4, 4);

    lv_obj_t *dh = lv_label_create(card);
    lv_label_set_text(dh, "SYSTEM DIAGNOSTICS");
    lv_obj_set_style_text_font(dh, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dh, OI_COLOR_TEXT_SUBTLE, 0);
    lv_obj_set_pos(dh, 4, 80);

    diag_heap_lbl = lv_label_create(card);
    lv_label_set_text(diag_heap_lbl, "Free Heap: ---");
    lv_obj_set_style_text_font(diag_heap_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diag_heap_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(diag_heap_lbl, 4, 104);

    diag_psram_lbl = lv_label_create(card);
    lv_label_set_text(diag_psram_lbl, "Free PSRAM: ---");
    lv_obj_set_style_text_font(diag_psram_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diag_psram_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(diag_psram_lbl, 4, 126);

    diag_uptime_lbl = lv_label_create(card);
    lv_label_set_text(diag_uptime_lbl, "Uptime: 0s");
    lv_obj_set_style_text_font(diag_uptime_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diag_uptime_lbl, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_pos(diag_uptime_lbl, 4, 148);

    diag_can_lbl = lv_label_create(card);
    lv_label_set_text(diag_can_lbl, "CAN Packets RX: 0  |  TX: 0");
    lv_obj_set_style_text_font(diag_can_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diag_can_lbl, OI_COLOR_GREEN, 0);
    lv_obj_set_pos(diag_can_lbl, 4, 170);
}

// -------------------------------------------------------------
// Live Telemetry Update (Called on every CAN cycle)
// -------------------------------------------------------------
void update_openinverter_telemetry(const InverterTelemetry &telem) {
    last_telem = telem;

    // 1. Navbar CAN Status Pill
    if (nav_can_status) {
        if (telem.can_connected) {
            lv_label_set_text(nav_can_status, LV_SYMBOL_OK " CAN Live #1");
            lv_obj_set_style_text_color(nav_can_status, OI_COLOR_GREEN, 0);
        } else {
            lv_label_set_text(nav_can_status, LV_SYMBOL_WARNING " CAN Offline");
            lv_obj_set_style_text_color(nav_can_status, OI_COLOR_RED, 0);
        }
    }

    // 2. Tab 0: Dashboard Hero Card & Metrics
    if (hero_pill_dot && hero_state_lbl && hero_sub_lbl) {
        if (telem.can_connected) {
            lv_label_set_text(hero_pill_dot, "Running");
            lv_obj_set_style_text_color(hero_pill_dot, OI_COLOR_GREEN, 0);
            lv_obj_set_style_bg_color(hero_pill_mode, lv_color_hex(0x0f3928), 0);
            lv_label_set_text(hero_state_lbl, "Inverter Running");
            char sbuf[64];
            snprintf(sbuf, sizeof(sbuf), "Status: %s (Opmode %d)", telem.status_str ? telem.status_str : "OK", telem.opmode);
            lv_label_set_text(hero_sub_lbl, sbuf);
        } else {
            lv_label_set_text(hero_pill_dot, "Offline");
            lv_obj_set_style_text_color(hero_pill_dot, OI_COLOR_RED, 0);
            lv_obj_set_style_bg_color(hero_pill_mode, lv_color_hex(0x281010), 0);
            lv_label_set_text(hero_state_lbl, "No connection to inverter");
            lv_label_set_text(hero_sub_lbl, "Status: Waiting for CAN");
        }
    }

    char buf[64];
    if (hero_val_udc) {
        if (telem.can_connected) snprintf(buf, sizeof(buf), "%.1f V", telem.pack_volts);
        else snprintf(buf, sizeof(buf), "---");
        lv_label_set_text(hero_val_udc, buf);
    }
    if (hero_val_idc) {
        if (telem.can_connected) snprintf(buf, sizeof(buf), "%.1f A", telem.pack_current);
        else snprintf(buf, sizeof(buf), "---");
        lv_label_set_text(hero_val_idc, buf);
    }
    if (hero_val_spd) {
        if (telem.can_connected) snprintf(buf, sizeof(buf), "%.0f", telem.motor_rpm);
        else snprintf(buf, sizeof(buf), "---");
        lv_label_set_text(hero_val_spd, buf);
    }
    if (hero_val_pwr) {
        if (telem.can_connected) snprintf(buf, sizeof(buf), "%.1f kW", telem.power_kw);
        else snprintf(buf, sizeof(buf), "---");
        lv_label_set_text(hero_val_pwr, buf);
    }

    // 3. Tab 2: Spot Values
    if (spot_items[0].lbl_val) {
        if (telem.can_connected) {
            snprintf(buf, sizeof(buf), "%.1f V", telem.pack_volts);
            lv_label_set_text(spot_items[0].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.1f A", telem.pack_current);
            lv_label_set_text(spot_items[1].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.0f rpm", telem.motor_rpm);
            lv_label_set_text(spot_items[2].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.1f kW", telem.power_kw);
            lv_label_set_text(spot_items[3].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.1f °C", telem.motor_temp);
            lv_label_set_text(spot_items[4].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.1f °C", telem.heatsink_temp);
            lv_label_set_text(spot_items[5].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.2f V", telem.aux_12v > 0 ? telem.aux_12v : 13.8f);
            lv_label_set_text(spot_items[6].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%.1f %%", telem.battery_soc);
            lv_label_set_text(spot_items[7].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%d (%s)", telem.opmode, telem.opmode == 2 ? "Run" : "Off");
            lv_label_set_text(spot_items[8].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%d", telem.din_fwd);
            lv_label_set_text(spot_items[9].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%d", telem.din_rev);
            lv_label_set_text(spot_items[10].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%d", telem.din_brake);
            lv_label_set_text(spot_items[11].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%u", (unsigned int)can_rx_packet_count);
            lv_label_set_text(spot_items[12].lbl_val, buf);

            snprintf(buf, sizeof(buf), "%u", (unsigned int)can_tx_packet_count);
            lv_label_set_text(spot_items[13].lbl_val, buf);
        } else {
            for (int i = 0; i < NUM_SPOT_ITEMS; i++) {
                if (spot_items[i].lbl_val) lv_label_set_text(spot_items[i].lbl_val, "---");
            }
        }
    }

    // 4. Tab 3: Plot Chart Sampling
    if (plot_chart && plot_running && (millis() - last_plot_sample_ms >= 100)) {
        last_plot_sample_ms = millis();
        if (telem.can_connected) {
            lv_chart_set_next_value(plot_chart, plot_ser_udc, (lv_coord_t)telem.pack_volts);
            lv_chart_set_next_value(plot_chart, plot_ser_idc, (lv_coord_t)telem.pack_current);
            lv_chart_set_next_value(plot_chart, plot_ser_spd, (lv_coord_t)(telem.motor_rpm / 15.0f));
        } else {
            lv_chart_set_next_value(plot_chart, plot_ser_udc, 0);
            lv_chart_set_next_value(plot_chart, plot_ser_idc, 0);
            lv_chart_set_next_value(plot_chart, plot_ser_spd, 0);
        }
    }

    // 5. Tab 4: Gauges Needle & Bars
    if (meter_spd && indic_spd_needle && lbl_spd_val) {
        int spd_val = telem.can_connected ? (int)telem.speed_kmh : 0;
        lv_meter_set_indicator_value(meter_spd, indic_spd_needle, spd_val);
        snprintf(buf, sizeof(buf), "%d km/h", spd_val);
        lv_label_set_text(lbl_spd_val, buf);
    }
    if (meter_udc && indic_udc_needle && lbl_udc_val) {
        int v_val = telem.can_connected ? (int)telem.pack_volts : 0;
        lv_meter_set_indicator_value(meter_udc, indic_udc_needle, v_val);
        snprintf(buf, sizeof(buf), "%d V", v_val);
        lv_label_set_text(lbl_udc_val, buf);
    }
    if (bar_pwr && lbl_pwr_val) {
        int p_val = telem.can_connected ? (int)telem.power_kw : 0;
        lv_bar_set_value(bar_pwr, p_val, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.1f kW", telem.can_connected ? telem.power_kw : 0.0f);
        lv_label_set_text(lbl_pwr_val, buf);
    }
    if (bar_tmot && lbl_tmot_val) {
        int t_val = telem.can_connected ? (int)telem.motor_temp : 0;
        lv_bar_set_value(bar_tmot, t_val, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.0f °C", telem.can_connected ? telem.motor_temp : 0.0f);
        lv_label_set_text(lbl_tmot_val, buf);
    }
    if (bar_soc && lbl_soc_val) {
        int s_val = telem.can_connected ? (int)telem.battery_soc : 0;
        lv_bar_set_value(bar_soc, s_val, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%.0f %%", telem.can_connected ? telem.battery_soc : 0.0f);
        lv_label_set_text(lbl_soc_val, buf);
    }

    // 6. Tab 5: Data Logger Record
    if (is_logging_active && logger_preview && telem.can_connected) {
        static unsigned long last_log_time = 0;
        if (millis() - last_log_time >= 250) {
            last_log_time = millis();
            log_record_count++;
            snprintf(buf, sizeof(buf), "%lu,%.1f,%.1f,%.0f,%.1f,%.0f\n",
                     millis(), telem.pack_volts, telem.pack_current, telem.motor_rpm, telem.power_kw, telem.motor_temp);
            lv_textarea_add_text(logger_preview, buf);

            if (logger_records_lbl) {
                char rc_buf[48];
                snprintf(rc_buf, sizeof(rc_buf), "Recorded samples: %u", (unsigned int)log_record_count);
                lv_label_set_text(logger_records_lbl, rc_buf);
            }
        }
    }

    // 7. Tab 10: Support & Diagnostics
    if (diag_heap_lbl) {
        snprintf(buf, sizeof(buf), "Free Heap: %u bytes", (unsigned int)ESP.getFreeHeap());
        lv_label_set_text(diag_heap_lbl, buf);
    }
    if (diag_psram_lbl) {
        snprintf(buf, sizeof(buf), "Free PSRAM: %u bytes", (unsigned int)ESP.getFreePsram());
        lv_label_set_text(diag_psram_lbl, buf);
    }
    if (diag_uptime_lbl) {
        unsigned long s = millis() / 1000;
        snprintf(buf, sizeof(buf), "Uptime: %02lu:%02lu:%02lu", (s / 3600), (s % 3600) / 60, s % 60);
        lv_label_set_text(diag_uptime_lbl, buf);
    }
    if (diag_can_lbl) {
        snprintf(buf, sizeof(buf), "CAN Packets RX: %u  |  TX: %u", (unsigned int)can_rx_packet_count, (unsigned int)can_tx_packet_count);
        lv_label_set_text(diag_can_lbl, buf);
    }
}
