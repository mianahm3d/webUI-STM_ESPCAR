#include "openinverter_ui.h"

// Tab views and navigation buttons
static lv_obj_t *oi_container = NULL;
static lv_obj_t *oi_views[NUM_OI_TABS];
static lv_obj_t *oi_nav_btns[NUM_OI_TABS];
static int current_tab_idx = 0;

static OiBackClusterCb s_back_cb = NULL;
static OiSleepCb s_sleep_cb = NULL;

// Dashboard Widgets
static lv_obj_t *lbl_dash_opmode_pill = NULL;
static lv_obj_t *lbl_dash_status = NULL;
static lv_obj_t *lbl_dash_udc = NULL;
static lv_obj_t *bar_dash_udc = NULL;
static lv_obj_t *lbl_dash_idc = NULL;
static lv_obj_t *bar_dash_idc = NULL;
static lv_obj_t *lbl_dash_speed = NULL;
static lv_obj_t *bar_dash_speed = NULL;
static lv_obj_t *lbl_dash_power = NULL;
static lv_obj_t *bar_dash_power = NULL;
static lv_obj_t *ta_dash_terminal = NULL;
static lv_obj_t *lbl_dash_can_status = NULL;

// Parameters
struct ParamItem {
    const char *name;
    const char *desc;
    const char *unit;
    const char *cat;
    float value;
    float step;
    float min_val;
    float max_val;
    lv_obj_t *card;
    lv_obj_t *val_label;
    lv_obj_t *slider;
};

static ParamItem s_params[] = {
    {"fweak",     "Field weakening frequency",     "Hz",  "MOTOR",    120.0f,  5.0f,   20.0f,  400.0f,  NULL, NULL, NULL},
    {"boost",     "Starting boost voltage",        "V",   "MOTOR",    1700.0f, 50.0f,  0.0f,   5000.0f, NULL, NULL, NULL},
    {"slipstart", "Slip frequency at start",       "Hz",  "MOTOR",    1.5f,    0.1f,   0.2f,   10.0f,   NULL, NULL, NULL},
    {"ampmin",    "Minimum amplitude limit",       "%",   "INVERTER", 10.0f,   1.0f,   0.0f,   100.0f,  NULL, NULL, NULL},
    {"curki",     "Current regulator integral",    "",    "INVERTER", 20000.0f,500.0f, 0.0f,   50000.0f,NULL, NULL, NULL},
    {"udcmin",    "Under-voltage cut-off limit",   "V",   "LIMITS",   320.0f,  5.0f,   50.0f,  600.0f,  NULL, NULL, NULL},
    {"udcmax",    "Over-voltage limit",            "V",   "LIMITS",   420.0f,  5.0f,   100.0f, 750.0f,  NULL, NULL, NULL},
    {"potmin",    "Throttle pedal min ADC count",  "",    "THROTTLE", 350.0f,  25.0f,  0.0f,   2000.0f, NULL, NULL, NULL},
    {"potmax",    "Throttle pedal max ADC count",  "",    "THROTTLE", 3900.0f, 25.0f,  2000.0f,4095.0f, NULL, NULL, NULL},
    {"dirmode",   "Direction switch mode (0-3)",   "",    "THROTTLE", 1.0f,    1.0f,   0.0f,   3.0f,    NULL, NULL, NULL},
};
static const int NUM_PARAMS = sizeof(s_params) / sizeof(s_params[0]);
static lv_obj_t *lbl_param_toast = NULL;

// Spot Values Table
static lv_obj_t *tbl_spot = NULL;

// Live Plot Chart
static lv_obj_t *chart_plot = NULL;
static lv_chart_series_t *plot_ser_speed = NULL;
static lv_chart_series_t *plot_ser_power = NULL;
static bool plot_running = true;
static lv_obj_t *btn_plot_run = NULL;

// Gauges Widgets
static lv_obj_t *meter_g_speed = NULL;
static lv_meter_indicator_t *indic_g_speed_needle = NULL;
static lv_meter_indicator_t *indic_g_speed_arc = NULL;
static lv_obj_t *meter_g_power = NULL;
static lv_meter_indicator_t *indic_g_power_needle = NULL;
static lv_obj_t *bar_g_bat = NULL;
static lv_obj_t *lbl_g_bat = NULL;
static lv_obj_t *bar_g_temp = NULL;
static lv_obj_t *lbl_g_temp = NULL;
static lv_obj_t *lbl_g_speed_num = NULL;
static lv_obj_t *lbl_g_power_num = NULL;
static lv_obj_t *btn_g_fwd = NULL;
static lv_obj_t *btn_g_rev = NULL;
static lv_obj_t *btn_g_brk = NULL;

// Data Logger Widgets
static bool log_active = true;
static unsigned long log_records = 14820;
static lv_obj_t *lbl_log_state = NULL;
static lv_obj_t *lbl_log_records = NULL;
static lv_obj_t *btn_log_toggle = NULL;
static lv_obj_t *ta_log_preview = NULL;

// CAN Tools
static lv_obj_t *ta_can_tx_id = NULL;
static lv_obj_t *ta_can_tx_data = NULL;
static lv_obj_t *ta_can_log = NULL;

// Settings
static lv_obj_t *slider_brightness = NULL;

// ----------------- CALLBACKS -----------------
static void tab_btn_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch_oi_tab_view(idx);
}

static void back_cluster_click_cb(lv_event_t *e) {
    if (s_back_cb) s_back_cb();
}

static void param_slider_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    s_params[idx].value = (float)lv_slider_get_value(slider);
    char buf[32];
    sprintf(buf, "%.1f %s", s_params[idx].value, s_params[idx].unit);
    lv_label_set_text(s_params[idx].val_label, buf);
}

static void param_step_cb(lv_event_t *e) {
    intptr_t data = (intptr_t)lv_event_get_user_data(e);
    int idx = data / 10;
    int is_plus = data % 10;

    if (is_plus) {
        s_params[idx].value += s_params[idx].step;
        if (s_params[idx].value > s_params[idx].max_val) s_params[idx].value = s_params[idx].max_val;
    } else {
        s_params[idx].value -= s_params[idx].step;
        if (s_params[idx].value < s_params[idx].min_val) s_params[idx].value = s_params[idx].min_val;
    }
    lv_slider_set_value(s_params[idx].slider, (int)s_params[idx].value, LV_ANIM_OFF);
    char buf[32];
    sprintf(buf, "%.1f %s", s_params[idx].value, s_params[idx].unit);
    lv_label_set_text(s_params[idx].val_label, buf);
}

static void param_save_flash_cb(lv_event_t *e) {
    lv_label_set_text(lbl_param_toast, "#54e6a4 [OK] Parameters Saved to STM32 Non-Volatile Flash!#");
}

static void param_cat_filter_cb(lv_event_t *e) {
    const char *selected_cat = (const char *)lv_event_get_user_data(e);
    for (int i = 0; i < NUM_PARAMS; i++) {
        if (!s_params[i].card) continue;
        if (strcmp(selected_cat, "ALL") == 0 || strcmp(s_params[i].cat, selected_cat) == 0) {
            lv_obj_clear_flag(s_params[i].card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_params[i].card, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void term_quick_cmd_cb(lv_event_t *e) {
    const char *cmd = (const char *)lv_event_get_user_data(e);
    if (!ta_dash_terminal) return;
    char buf[96];
    if (strcmp(cmd, "get udc") == 0) {
        sprintf(buf, "\n> get udc\n384.2 V (ADC OK)");
    } else if (strcmp(cmd, "get speed") == 0) {
        sprintf(buf, "\n> get speed\n2850 rpm (62 km/h)");
    } else if (strcmp(cmd, "status") == 0) {
        sprintf(buf, "\n> status\n0: OK, RUNNING");
    } else if (strcmp(cmd, "start 2") == 0) {
        sprintf(buf, "\n> start 2\nInverter Started in Manual Mode");
    } else if (strcmp(cmd, "stop") == 0) {
        sprintf(buf, "\n> stop\nInverter Inactive (PWM Off)");
    } else {
        sprintf(buf, "\n> %s\nOK", cmd);
    }
    lv_textarea_add_text(ta_dash_terminal, buf);
}

static void term_send_click_cb(lv_event_t *e) {
    lv_textarea_add_text(ta_dash_terminal, "\n> get all\nudc=384.2, idc=42.5, speed=2850, tmot=40");
}

static void plot_toggle_cb(lv_event_t *e) {
    plot_running = !plot_running;
    lv_obj_t *lbl = lv_obj_get_child(btn_plot_run, 0);
    if (plot_running) {
        lv_label_set_text(lbl, "PAUSE");
        lv_obj_set_style_bg_color(btn_plot_run, lv_color_hex(0x13402d), 0);
    } else {
        lv_label_set_text(lbl, "RESUME");
        lv_obj_set_style_bg_color(btn_plot_run, lv_color_hex(0x381515), 0);
    }
}

static void plot_clear_cb(lv_event_t *e) {
    if (chart_plot && plot_ser_speed && plot_ser_power) {
        for (int i = 0; i < 60; i++) {
            lv_chart_set_next_value(chart_plot, plot_ser_speed, 0);
            lv_chart_set_next_value(chart_plot, plot_ser_power, 0);
        }
    }
}

static void log_toggle_cb(lv_event_t *e) {
    log_active = !log_active;
    lv_obj_t *lbl = lv_obj_get_child(btn_log_toggle, 0);
    if (log_active) {
        lv_label_set_text(lbl, "STOP LOGGING");
        lv_obj_set_style_bg_color(btn_log_toggle, lv_color_hex(0x381515), 0);
        lv_label_set_text(lbl_log_state, "#54e6a4 [ACTIVE]# Logging to /sdcard/LOG_0042.CSV");
    } else {
        lv_label_set_text(lbl, "START LOGGING");
        lv_obj_set_style_bg_color(btn_log_toggle, lv_color_hex(0x13402d), 0);
        lv_label_set_text(lbl_log_state, "#ffb454 [STANDBY]# File Closed");
    }
}

static void can_send_click_cb(lv_event_t *e) {
    if (!ta_can_log) return;
    lv_textarea_add_text(ta_can_log, "\n[TX] ID: 0x180 DLC: 8 DATA: 00 00 00 00 00 00 00 00");
}

static void screen_sleep_click_cb(lv_event_t *e) {
    if (s_sleep_cb) s_sleep_cb();
}

// ----------------- SWITCH TABS -----------------
void switch_oi_tab_view(int tab_idx) {
    if (tab_idx < 0 || tab_idx >= NUM_OI_TABS) return;
    current_tab_idx = tab_idx;
    for (int i = 0; i < NUM_OI_TABS; i++) {
        if (oi_views[i]) {
            if (i == tab_idx) {
                lv_obj_clear_flag(oi_views[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(oi_views[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (oi_nav_btns[i]) {
            lv_obj_set_style_bg_color(oi_nav_btns[i], (i == tab_idx) ? lv_color_hex(0x1c2b3d) : OI_COLOR_SURFACE, 0);
            lv_obj_set_style_border_color(oi_nav_btns[i], (i == tab_idx) ? OI_COLOR_CYAN : OI_COLOR_BORDER, 0);
            lv_obj_set_style_border_width(oi_nav_btns[i], (i == tab_idx) ? 2 : 1, 0);
        }
    }
}

// ----------------- BUILD TAB 0: DASHBOARD -----------------
static void build_tab_dashboard(lv_obj_t *parent) {
    // Top Hero Status Banner
    lv_obj_t *hero = lv_obj_create(parent);
    lv_obj_set_size(hero, 625, 62);
    lv_obj_set_pos(hero, 0, 0);
    lv_obj_set_style_bg_color(hero, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(hero, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_radius(hero, 8, 0);
    lv_obj_set_style_pad_all(hero, 8, 0);

    // Status Pill
    lbl_dash_opmode_pill = lv_label_create(hero);
    lv_label_set_recolor(lbl_dash_opmode_pill, true);
    lv_label_set_text(lbl_dash_opmode_pill, "#54e6a4 [● RUN]#  #e8ecf1 OPMODE: RUN (Throttle Active)#");
    lv_obj_set_style_text_font(lbl_dash_opmode_pill, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_dash_opmode_pill, LV_ALIGN_TOP_LEFT, 6, 2);

    lbl_dash_status = lv_label_create(hero);
    lv_label_set_recolor(lbl_dash_status, true);
    lv_label_set_text(lbl_dash_status, "#9aa3b2 Status: 0 (OK)  |  #4cc9f0 CAN #1 Connected#  |  #54e6a4 500 kbps#");
    lv_obj_set_style_text_font(lbl_dash_status, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_dash_status, LV_ALIGN_BOTTOM_LEFT, 6, -2);

    // Quick Action Buttons on right of hero banner
    lv_obj_t *btn_start = lv_btn_create(hero);
    lv_obj_set_size(btn_start, 76, 36);
    lv_obj_align(btn_start, LV_ALIGN_RIGHT_MID, -164, 0);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x13402d), 0);
    lv_obj_set_style_border_color(btn_start, OI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(btn_start, 1, 0);
    lv_obj_set_style_radius(btn_start, 6, 0);
    lv_obj_add_event_cb(btn_start, term_quick_cmd_cb, LV_EVENT_CLICKED, (void *)"start 2");
    lv_obj_t *l_st = lv_label_create(btn_start);
    lv_label_set_text(l_st, "START");
    lv_obj_set_style_text_color(l_st, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(l_st, &lv_font_montserrat_12, 0);
    lv_obj_center(l_st);

    lv_obj_t *btn_stop = lv_btn_create(hero);
    lv_obj_set_size(btn_stop, 74, 36);
    lv_obj_align(btn_stop, LV_ALIGN_RIGHT_MID, -84, 0);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x381515), 0);
    lv_obj_set_style_border_color(btn_stop, OI_COLOR_RED, 0);
    lv_obj_set_style_border_width(btn_stop, 1, 0);
    lv_obj_set_style_radius(btn_stop, 6, 0);
    lv_obj_add_event_cb(btn_stop, term_quick_cmd_cb, LV_EVENT_CLICKED, (void *)"stop");
    lv_obj_t *l_sp = lv_label_create(btn_stop);
    lv_label_set_text(l_sp, "STOP");
    lv_obj_set_style_text_color(l_sp, OI_COLOR_RED, 0);
    lv_obj_set_style_text_font(l_sp, &lv_font_montserrat_12, 0);
    lv_obj_center(l_sp);

    lv_obj_t *btn_rst = lv_btn_create(hero);
    lv_obj_set_size(btn_rst, 74, 36);
    lv_obj_align(btn_rst, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(btn_rst, lv_color_hex(0x1a2636), 0);
    lv_obj_set_style_border_color(btn_rst, OI_COLOR_CYAN, 0);
    lv_obj_set_style_border_width(btn_rst, 1, 0);
    lv_obj_set_style_radius(btn_rst, 6, 0);
    lv_obj_add_event_cb(btn_rst, term_quick_cmd_cb, LV_EVENT_CLICKED, (void *)"reset");
    lv_obj_t *l_rst = lv_label_create(btn_rst);
    lv_label_set_text(l_rst, "RESET");
    lv_obj_set_style_text_color(l_rst, OI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(l_rst, &lv_font_montserrat_12, 0);
    lv_obj_center(l_rst);

    // Primary 4 Telemetry Metrics Cards
    const char *metric_titles[4] = {"BUS VOLTAGE (Udc)", "PACK CURRENT (Idc)", "MOTOR SPEED", "POWER OUTPUT"};
    lv_color_t metric_colors[4] = {OI_COLOR_CYAN, OI_COLOR_AMBER, lv_color_hex(0x60a5fa), OI_COLOR_GREEN};
    lv_obj_t **metric_labels[4] = {&lbl_dash_udc, &lbl_dash_idc, &lbl_dash_speed, &lbl_dash_power};
    lv_obj_t **metric_bars[4] = {&bar_dash_udc, &bar_dash_idc, &bar_dash_speed, &bar_dash_power};
    const char *init_vals[4] = {"384.2 V", "42.5 A", "2850 RPM", "16.3 kW"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 150, 78);
        lv_obj_set_pos(card, i * 158, 68);
        lv_obj_set_style_bg_color(card, OI_COLOR_CARD, 0);
        lv_obj_set_style_border_color(card, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *t = lv_label_create(card);
        lv_label_set_text(t, metric_titles[i]);
        lv_obj_set_style_text_color(t, OI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 2, 0);

        *metric_labels[i] = lv_label_create(card);
        lv_label_set_text(*metric_labels[i], init_vals[i]);
        lv_obj_set_style_text_color(*metric_labels[i], OI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(*metric_labels[i], &lv_font_montserrat_18, 0);
        lv_obj_align(*metric_labels[i], LV_ALIGN_LEFT_MID, 2, 4);

        *metric_bars[i] = lv_bar_create(card);
        lv_obj_set_size(*metric_bars[i], 134, 4);
        lv_obj_align(*metric_bars[i], LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_bar_set_range(*metric_bars[i], 0, 100);
        lv_bar_set_value(*metric_bars[i], 55, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(*metric_bars[i], lv_color_hex(0x223040), LV_PART_MAIN);
        lv_obj_set_style_bg_color(*metric_bars[i], metric_colors[i], LV_PART_INDICATOR);
    }

    // Lower Section: Left = Terminal Console (width 390), Right = CAN Injector (width 225)
    lv_obj_t *term_box = lv_obj_create(parent);
    lv_obj_set_size(term_box, 395, 305);
    lv_obj_set_pos(term_box, 0, 152);
    lv_obj_set_style_bg_color(term_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(term_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(term_box, 1, 0);
    lv_obj_set_style_radius(term_box, 8, 0);
    lv_obj_set_style_pad_all(term_box, 8, 0);

    lv_obj_t *lbl_term_t = lv_label_create(term_box);
    lv_label_set_text(lbl_term_t, "INVERTER COMMAND TERMINAL (UART/CAN)");
    lv_obj_set_style_text_color(lbl_term_t, OI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lbl_term_t, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_term_t, LV_ALIGN_TOP_LEFT, 0, 0);

    // Quick Command Buttons Bar
    const char *cmds[5] = {"get udc", "get speed", "status", "start 2", "stop"};
    for (int c = 0; c < 5; c++) {
        lv_obj_t *btn_c = lv_btn_create(term_box);
        lv_obj_set_size(btn_c, 71, 26);
        lv_obj_set_pos(btn_c, c * 75, 22);
        lv_obj_set_style_bg_color(btn_c, lv_color_hex(0x131a26), 0);
        lv_obj_set_style_border_color(btn_c, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(btn_c, 1, 0);
        lv_obj_set_style_radius(btn_c, 4, 0);
        lv_obj_add_event_cb(btn_c, term_quick_cmd_cb, LV_EVENT_CLICKED, (void *)cmds[c]);

        lv_obj_t *lc = lv_label_create(btn_c);
        lv_label_set_text(lc, cmds[c]);
        lv_obj_set_style_text_color(lc, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(lc, &lv_font_montserrat_10, 0);
        lv_obj_center(lc);
    }

    ta_dash_terminal = lv_textarea_create(term_box);
    lv_obj_set_size(ta_dash_terminal, 375, 195);
    lv_obj_set_pos(ta_dash_terminal, 0, 54);
    lv_obj_set_style_bg_color(ta_dash_terminal, lv_color_hex(0x0a0e16), 0);
    lv_obj_set_style_border_color(ta_dash_terminal, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ta_dash_terminal, 1, 0);
    lv_obj_set_style_text_color(ta_dash_terminal, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(ta_dash_terminal, &lv_font_montserrat_10, 0);
    lv_textarea_set_text(ta_dash_terminal, "OpenInverter ESP32 Console Ready.\n> CAN Interface: 500k synchronized\n> Node ID #1 response time: 2.1ms\n> System: RUNNING, 0 Errors.");

    lv_obj_t *btn_send = lv_btn_create(term_box);
    lv_obj_set_size(btn_send, 375, 34);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_send, lv_color_hex(0x14344d), 0);
    lv_obj_set_style_border_color(btn_send, OI_COLOR_CYAN, 0);
    lv_obj_set_style_border_width(btn_send, 1, 0);
    lv_obj_set_style_radius(btn_send, 6, 0);
    lv_obj_add_event_cb(btn_send, term_send_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_snd = lv_label_create(btn_send);
    lv_label_set_text(l_snd, "POLL ALL PARAMETERS (JSON GET)");
    lv_obj_set_style_text_color(l_snd, OI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(l_snd, &lv_font_montserrat_12, 0);
    lv_obj_center(l_snd);

    // Right Side: CAN Message Injector Card
    lv_obj_t *can_box = lv_obj_create(parent);
    lv_obj_set_size(can_box, 222, 305);
    lv_obj_set_pos(can_box, 403, 152);
    lv_obj_set_style_bg_color(can_box, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(can_box, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(can_box, 1, 0);
    lv_obj_set_style_radius(can_box, 8, 0);
    lv_obj_set_style_pad_all(can_box, 8, 0);

    lv_obj_t *lbl_can_t = lv_label_create(can_box);
    lv_label_set_text(lbl_can_t, "CAN INJECTOR / TOOLS");
    lv_obj_set_style_text_color(lbl_can_t, OI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(lbl_can_t, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_can_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_cid = lv_label_create(can_box);
    lv_label_set_text(lbl_cid, "CAN ID (hex):");
    lv_obj_set_style_text_color(lbl_cid, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_cid, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_cid, 0, 26);

    ta_can_tx_id = lv_textarea_create(can_box);
    lv_obj_set_size(ta_can_tx_id, 204, 32);
    lv_obj_set_pos(ta_can_tx_id, 0, 42);
    lv_obj_set_style_bg_color(ta_can_tx_id, lv_color_hex(0x0c1017), 0);
    lv_obj_set_style_border_color(ta_can_tx_id, OI_COLOR_BORDER, 0);
    lv_obj_set_style_text_color(ta_can_tx_id, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(ta_can_tx_id, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(ta_can_tx_id, true);
    lv_textarea_set_text(ta_can_tx_id, "0x180");

    lv_obj_t *lbl_cd = lv_label_create(can_box);
    lv_label_set_text(lbl_cd, "Data Bytes (8 Hex):");
    lv_obj_set_style_text_color(lbl_cd, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_cd, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_cd, 0, 80);

    ta_can_tx_data = lv_textarea_create(can_box);
    lv_obj_set_size(ta_can_tx_data, 204, 32);
    lv_obj_set_pos(ta_can_tx_data, 0, 96);
    lv_obj_set_style_bg_color(ta_can_tx_data, lv_color_hex(0x0c1017), 0);
    lv_obj_set_style_border_color(ta_can_tx_data, OI_COLOR_BORDER, 0);
    lv_obj_set_style_text_color(ta_can_tx_data, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(ta_can_tx_data, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(ta_can_tx_data, true);
    lv_textarea_set_text(ta_can_tx_data, "00 00 00 00 00 00 00 00");

    lv_obj_t *btn_can_tx = lv_btn_create(can_box);
    lv_obj_set_size(btn_can_tx, 204, 36);
    lv_obj_set_pos(btn_can_tx, 0, 136);
    lv_obj_set_style_bg_color(btn_can_tx, lv_color_hex(0x2a1d0f), 0);
    lv_obj_set_style_border_color(btn_can_tx, OI_COLOR_AMBER, 0);
    lv_obj_set_style_border_width(btn_can_tx, 1, 0);
    lv_obj_set_style_radius(btn_can_tx, 6, 0);
    lv_obj_add_event_cb(btn_can_tx, can_send_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_ctx = lv_label_create(btn_can_tx);
    lv_label_set_text(l_ctx, "TRANSMIT CAN FRAME");
    lv_obj_set_style_text_color(l_ctx, OI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(l_ctx, &lv_font_montserrat_12, 0);
    lv_obj_center(l_ctx);

    lbl_dash_can_status = lv_label_create(can_box);
    lv_label_set_recolor(lbl_dash_can_status, true);
    lv_label_set_text(lbl_dash_can_status, "#38bdf8 Bus Status:# Active\n#34d399 RX Packets:# 24,190\n#ffb454 TX Packets:# 1,840\n#9aa3b2 Bitrate:# 500 kbps\n#9aa3b2 STM32 Node:# 1");
    lv_obj_set_style_text_font(lbl_dash_can_status, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_dash_can_status, 4, 180);
}

// ----------------- BUILD TAB 1: PARAMETERS -----------------
static void build_tab_parameters(lv_obj_t *parent) {
    // Header & Filter bar
    lv_obj_t *p_head = lv_obj_create(parent);
    lv_obj_set_size(p_head, 625, 48);
    lv_obj_set_pos(p_head, 0, 0);
    lv_obj_set_style_bg_color(p_head, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(p_head, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(p_head, 1, 0);
    lv_obj_set_style_radius(p_head, 8, 0);
    lv_obj_set_style_pad_all(p_head, 6, 0);

    // Category Filter Pills
    const char *cats[5] = {"ALL", "MOTOR", "INVERTER", "THROTTLE", "LIMITS"};
    for (int c = 0; c < 5; c++) {
        lv_obj_t *btn_cat = lv_btn_create(p_head);
        lv_obj_set_size(btn_cat, 82, 32);
        lv_obj_set_pos(btn_cat, c * 86, 2);
        lv_obj_set_style_bg_color(btn_cat, (c == 0) ? lv_color_hex(0x1a2636) : OI_COLOR_CARD, 0);
        lv_obj_set_style_border_color(btn_cat, (c == 0) ? OI_COLOR_CYAN : OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(btn_cat, 1, 0);
        lv_obj_set_style_radius(btn_cat, 6, 0);
        lv_obj_add_event_cb(btn_cat, param_cat_filter_cb, LV_EVENT_CLICKED, (void *)cats[c]);

        lv_obj_t *lc = lv_label_create(btn_cat);
        lv_label_set_text(lc, cats[c]);
        lv_obj_set_style_text_color(lc, (c == 0) ? OI_COLOR_CYAN : OI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(lc, &lv_font_montserrat_10, 0);
        lv_obj_center(lc);
    }

    // Save to Flash Button
    lv_obj_t *btn_save = lv_btn_create(p_head);
    lv_obj_set_size(btn_save, 142, 34);
    lv_obj_align(btn_save, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x13402d), 0);
    lv_obj_set_style_border_color(btn_save, OI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(btn_save, 1, 0);
    lv_obj_set_style_radius(btn_save, 6, 0);
    lv_obj_add_event_cb(btn_save, param_save_flash_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sl = lv_label_create(btn_save);
    lv_label_set_text(sl, "SAVE TO FLASH");
    lv_obj_set_style_text_color(sl, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
    lv_obj_center(sl);

    lbl_param_toast = lv_label_create(parent);
    lv_label_set_recolor(lbl_param_toast, true);
    lv_label_set_text(lbl_param_toast, "");
    lv_obj_set_style_text_font(lbl_param_toast, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_param_toast, 6, 52);

    // Scrollable Parameters Container
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, 625, 400);
    lv_obj_set_pos(scroll, 0, 72);
    lv_obj_set_style_bg_color(scroll, OI_COLOR_BG, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < NUM_PARAMS; i++) {
        s_params[i].card = lv_obj_create(scroll);
        lv_obj_set_size(s_params[i].card, 608, 58);
        lv_obj_set_pos(s_params[i].card, 0, i * 64);
        lv_obj_set_style_bg_color(s_params[i].card, OI_COLOR_CARD, 0);
        lv_obj_set_style_border_color(s_params[i].card, OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(s_params[i].card, 1, 0);
        lv_obj_set_style_radius(s_params[i].card, 8, 0);
        lv_obj_set_style_pad_all(s_params[i].card, 6, 0);

        // Name & Category Badge & Description
        lv_obj_t *lbl_name = lv_label_create(s_params[i].card);
        char title_buf[128];
        snprintf(title_buf, sizeof(title_buf), "#4cc9f0 %s#  [%s]  #9aa3b2 %s#", s_params[i].name, s_params[i].cat, s_params[i].desc);
        lv_label_set_recolor(lbl_name, true);
        lv_label_set_text(lbl_name, title_buf);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 6, 2);

        // Value text
        s_params[i].val_label = lv_label_create(s_params[i].card);
        char val_buf[64];
        snprintf(val_buf, sizeof(val_buf), "%.1f %s", s_params[i].value, s_params[i].unit);
        lv_label_set_text(s_params[i].val_label, val_buf);
        lv_obj_set_style_text_color(s_params[i].val_label, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(s_params[i].val_label, &lv_font_montserrat_14, 0);
        lv_obj_align(s_params[i].val_label, LV_ALIGN_LEFT_MID, 6, 12);

        // Slider
        s_params[i].slider = lv_slider_create(s_params[i].card);
        lv_obj_set_size(s_params[i].slider, 220, 12);
        lv_obj_align(s_params[i].slider, LV_ALIGN_RIGHT_MID, -120, 10);
        lv_slider_set_range(s_params[i].slider, (int)s_params[i].min_val, (int)s_params[i].max_val);
        lv_slider_set_value(s_params[i].slider, (int)s_params[i].value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_params[i].slider, lv_color_hex(0x223040), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_params[i].slider, OI_COLOR_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_params[i].slider, OI_COLOR_CYAN, LV_PART_KNOB);
        lv_obj_add_event_cb(s_params[i].slider, param_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);

        // [-] Button
        lv_obj_t *bm = lv_btn_create(s_params[i].card);
        lv_obj_set_size(bm, 44, 34);
        lv_obj_align(bm, LV_ALIGN_RIGHT_MID, -60, 4);
        lv_obj_set_style_bg_color(bm, lv_color_hex(0x1a2636), 0);
        lv_obj_set_style_border_color(bm, OI_COLOR_CYAN, 0);
        lv_obj_set_style_border_width(bm, 1, 0);
        lv_obj_set_style_radius(bm, 6, 0);
        lv_obj_t *lbm = lv_label_create(bm);
        lv_label_set_text(lbm, "-");
        lv_obj_set_style_text_color(lbm, OI_COLOR_CYAN, 0);
        lv_obj_set_style_text_font(lbm, &lv_font_montserrat_18, 0);
        lv_obj_center(lbm);
        lv_obj_add_event_cb(bm, param_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 10 + 0));

        // [+] Button
        lv_obj_t *bp = lv_btn_create(s_params[i].card);
        lv_obj_set_size(bp, 44, 34);
        lv_obj_align(bp, LV_ALIGN_RIGHT_MID, -6, 4);
        lv_obj_set_style_bg_color(bp, lv_color_hex(0x1a2636), 0);
        lv_obj_set_style_border_color(bp, OI_COLOR_CYAN, 0);
        lv_obj_set_style_border_width(bp, 1, 0);
        lv_obj_set_style_radius(bp, 6, 0);
        lv_obj_t *lbp = lv_label_create(bp);
        lv_label_set_text(lbp, "+");
        lv_obj_set_style_text_color(lbp, OI_COLOR_CYAN, 0);
        lv_obj_set_style_text_font(lbp, &lv_font_montserrat_18, 0);
        lv_obj_center(lbp);
        lv_obj_add_event_cb(bp, param_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 10 + 1));
    }
}

// ----------------- BUILD TAB 2: SPOT VALUES -----------------
static void build_tab_spotvalues(lv_obj_t *parent) {
    lv_obj_t *sh = lv_label_create(parent);
    lv_label_set_recolor(sh, true);
    lv_label_set_text(sh, "SPOT VALUES (#54e6a4 LIVE 50Hz TELEMETRY STREAM#)");
    lv_obj_set_style_text_color(sh, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(sh, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(sh, 6, 6);

    tbl_spot = lv_table_create(parent);
    lv_obj_set_size(tbl_spot, 625, 424);
    lv_obj_set_pos(tbl_spot, 0, 34);
    lv_obj_set_style_bg_color(tbl_spot, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(tbl_spot, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(tbl_spot, 1, 0);
    lv_obj_set_style_text_color(tbl_spot, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(tbl_spot, &lv_font_montserrat_12, 0);
    lv_table_set_col_width(tbl_spot, 0, 155);
    lv_table_set_col_width(tbl_spot, 1, 150);
    lv_table_set_col_width(tbl_spot, 2, 155);
    lv_table_set_col_width(tbl_spot, 3, 150);

    const char *s_names[16] = {
        "speed (rpm)", "udc (volts)", "idc (amps)", "power (kW)",
        "tmot (degC)", "ths (degC)", "il1 (phase A)", "il2 (phase B)",
        "frequency (Hz)", "pot (throttle)", "pot2 (aux adc)", "status",
        "din_forward", "din_reverse", "din_brake", "lasterr"
    };
    for (int r = 0; r < 8; r++) {
        lv_table_set_cell_value(tbl_spot, r, 0, s_names[r * 2]);
        lv_table_set_cell_value(tbl_spot, r, 1, "--");
        lv_table_set_cell_value(tbl_spot, r, 2, s_names[r * 2 + 1]);
        lv_table_set_cell_value(tbl_spot, r, 3, "--");
    }
}

// ----------------- BUILD TAB 3: LIVE PLOT -----------------
static void build_tab_plot(lv_obj_t *parent) {
    lv_obj_t *ph = lv_label_create(parent);
    lv_label_set_recolor(ph, true);
    lv_label_set_text(ph, "OSCILLOSCOPE (#4cc9f0 SPEED# | #54e6a4 POWER#)");
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(ph, 6, 6);

    btn_plot_run = lv_btn_create(parent);
    lv_obj_set_size(btn_plot_run, 88, 30);
    lv_obj_align(btn_plot_run, LV_ALIGN_TOP_RIGHT, -100, 2);
    lv_obj_set_style_bg_color(btn_plot_run, lv_color_hex(0x13402d), 0);
    lv_obj_set_style_border_color(btn_plot_run, OI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(btn_plot_run, 1, 0);
    lv_obj_set_style_radius(btn_plot_run, 6, 0);
    lv_obj_add_event_cb(btn_plot_run, plot_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lr = lv_label_create(btn_plot_run);
    lv_label_set_text(lr, "PAUSE");
    lv_obj_set_style_text_color(lr, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(lr, &lv_font_montserrat_10, 0);
    lv_obj_center(lr);

    lv_obj_t *btn_clr = lv_btn_create(parent);
    lv_obj_set_size(btn_clr, 84, 30);
    lv_obj_align(btn_clr, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_set_style_bg_color(btn_clr, lv_color_hex(0x1a2636), 0);
    lv_obj_set_style_border_color(btn_clr, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_clr, 1, 0);
    lv_obj_set_style_radius(btn_clr, 6, 0);
    lv_obj_add_event_cb(btn_clr, plot_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lc = lv_label_create(btn_clr);
    lv_label_set_text(lc, "CLEAR");
    lv_obj_set_style_text_color(lc, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(lc, &lv_font_montserrat_10, 0);
    lv_obj_center(lc);

    chart_plot = lv_chart_create(parent);
    lv_obj_set_size(chart_plot, 625, 416);
    lv_obj_set_pos(chart_plot, 0, 36);
    lv_obj_set_style_bg_color(chart_plot, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(chart_plot, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(chart_plot, 1, 0);
    lv_chart_set_type(chart_plot, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_plot, 60);
    lv_chart_set_range(chart_plot, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    plot_ser_speed = lv_chart_add_series(chart_plot, OI_COLOR_CYAN, LV_CHART_AXIS_PRIMARY_Y);
    plot_ser_power = lv_chart_add_series(chart_plot, OI_COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    for (int i = 0; i < 60; i++) {
        lv_chart_set_next_value(chart_plot, plot_ser_speed, 35 + (i % 25));
        lv_chart_set_next_value(chart_plot, plot_ser_power, 15 + (i % 18));
    }
}

// ----------------- BUILD TAB 4: GAUGES -----------------
static void build_tab_gauges(lv_obj_t *parent) {
    // Left Radial Dial: Motor Speed (0 - 140 km/h)
    meter_g_speed = lv_meter_create(parent);
    lv_obj_set_size(meter_g_speed, 250, 250);
    lv_obj_set_pos(meter_g_speed, 10, 10);
    lv_obj_set_style_bg_color(meter_g_speed, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(meter_g_speed, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(meter_g_speed, 1, 0);
    lv_obj_set_style_radius(meter_g_speed, 125, 0);

    lv_meter_scale_t *scale_sp = lv_meter_add_scale(meter_g_speed);
    lv_meter_set_scale_ticks(meter_g_speed, scale_sp, 31, 2, 8, OI_COLOR_TEXT_MUTED);
    lv_meter_set_scale_major_ticks(meter_g_speed, scale_sp, 6, 3, 14, OI_COLOR_CYAN, 12);
    lv_meter_set_scale_range(meter_g_speed, scale_sp, 0, 140, 270, 135);

    indic_g_speed_arc = lv_meter_add_arc(meter_g_speed, scale_sp, 3, OI_COLOR_CYAN, 0);
    lv_meter_set_indicator_start_value(meter_g_speed, indic_g_speed_arc, 0);
    lv_meter_set_indicator_end_value(meter_g_speed, indic_g_speed_arc, 62);

    indic_g_speed_needle = lv_meter_add_needle_line(meter_g_speed, scale_sp, 4, lv_color_hex(0xff5722), -15);
    lv_meter_set_indicator_value(meter_g_speed, indic_g_speed_needle, 62);

    lbl_g_speed_num = lv_label_create(meter_g_speed);
    lv_label_set_text(lbl_g_speed_num, "62 km/h");
    lv_obj_set_style_text_color(lbl_g_speed_num, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(lbl_g_speed_num, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_g_speed_num, LV_ALIGN_BOTTOM_MID, 0, -32);

    // Right Radial Dial: Power Meter (-50 to +150 kW)
    meter_g_power = lv_meter_create(parent);
    lv_obj_set_size(meter_g_power, 250, 250);
    lv_obj_set_pos(meter_g_power, 365, 10);
    lv_obj_set_style_bg_color(meter_g_power, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(meter_g_power, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(meter_g_power, 1, 0);
    lv_obj_set_style_radius(meter_g_power, 125, 0);

    lv_meter_scale_t *scale_pw = lv_meter_add_scale(meter_g_power);
    lv_meter_set_scale_ticks(meter_g_power, scale_pw, 21, 2, 8, OI_COLOR_TEXT_MUTED);
    lv_meter_set_scale_major_ticks(meter_g_power, scale_pw, 5, 3, 14, OI_COLOR_GREEN, 12);
    lv_meter_set_scale_range(meter_g_power, scale_pw, 0, 150, 270, 135);

    indic_g_power_needle = lv_meter_add_needle_line(meter_g_power, scale_pw, 4, OI_COLOR_GREEN, -15);
    lv_meter_set_indicator_value(meter_g_power, indic_g_power_needle, 16);

    lbl_g_power_num = lv_label_create(meter_g_power);
    lv_label_set_text(lbl_g_power_num, "+16.3 kW");
    lv_obj_set_style_text_color(lbl_g_power_num, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(lbl_g_power_num, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_g_power_num, LV_ALIGN_BOTTOM_MID, 0, -32);

    // Center Indicators: FWD, REV, BRAKE pills
    btn_g_fwd = lv_btn_create(parent);
    lv_obj_set_size(btn_g_fwd, 80, 36);
    lv_obj_set_pos(btn_g_fwd, 273, 20);
    lv_obj_set_style_bg_color(btn_g_fwd, lv_color_hex(0x13402d), 0);
    lv_obj_set_style_border_color(btn_g_fwd, OI_COLOR_GREEN, 0);
    lv_obj_set_style_border_width(btn_g_fwd, 1, 0);
    lv_obj_t *lf = lv_label_create(btn_g_fwd);
    lv_label_set_text(lf, "FWD");
    lv_obj_set_style_text_color(lf, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(lf, &lv_font_montserrat_12, 0);
    lv_obj_center(lf);

    btn_g_rev = lv_btn_create(parent);
    lv_obj_set_size(btn_g_rev, 80, 36);
    lv_obj_set_pos(btn_g_rev, 273, 62);
    lv_obj_set_style_bg_color(btn_g_rev, lv_color_hex(0x1a2433), 0);
    lv_obj_set_style_border_color(btn_g_rev, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_g_rev, 1, 0);
    lv_obj_t *lr = lv_label_create(btn_g_rev);
    lv_label_set_text(lr, "REV");
    lv_obj_set_style_text_color(lr, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lr, &lv_font_montserrat_12, 0);
    lv_obj_center(lr);

    btn_g_brk = lv_btn_create(parent);
    lv_obj_set_size(btn_g_brk, 80, 36);
    lv_obj_set_pos(btn_g_brk, 273, 104);
    lv_obj_set_style_bg_color(btn_g_brk, lv_color_hex(0x1a2433), 0);
    lv_obj_set_style_border_color(btn_g_brk, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(btn_g_brk, 1, 0);
    lv_obj_t *lb = lv_label_create(btn_g_brk);
    lv_label_set_text(lb, "BRAKE");
    lv_obj_set_style_text_color(lb, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_12, 0);
    lv_obj_center(lb);

    // Lower Cards: Battery State of Charge & Motor Temperature Bars
    lv_obj_t *c_bat = lv_obj_create(parent);
    lv_obj_set_size(c_bat, 300, 160);
    lv_obj_set_pos(c_bat, 10, 280);
    lv_obj_set_style_bg_color(c_bat, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(c_bat, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c_bat, 1, 0);
    lv_obj_set_style_radius(c_bat, 8, 0);

    lv_obj_t *th_b = lv_label_create(c_bat);
    lv_label_set_text(th_b, "BATTERY TRACTION PACK");
    lv_obj_set_style_text_color(th_b, OI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(th_b, &lv_font_montserrat_12, 0);
    lv_obj_align(th_b, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_g_bat = lv_label_create(c_bat);
    lv_label_set_text(lbl_g_bat, "84% (384.2 V)");
    lv_obj_set_style_text_color(lbl_g_bat, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(lbl_g_bat, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_g_bat, LV_ALIGN_LEFT_MID, 0, -10);

    bar_g_bat = lv_bar_create(c_bat);
    lv_obj_set_size(bar_g_bat, 260, 20);
    lv_obj_align(bar_g_bat, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_bar_set_value(bar_g_bat, 84, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_g_bat, lv_color_hex(0x223040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_g_bat, OI_COLOR_CYAN, LV_PART_INDICATOR);

    lv_obj_t *c_tmp = lv_obj_create(parent);
    lv_obj_set_size(c_tmp, 305, 160);
    lv_obj_set_pos(c_tmp, 320, 280);
    lv_obj_set_style_bg_color(c_tmp, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(c_tmp, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c_tmp, 1, 0);
    lv_obj_set_style_radius(c_tmp, 8, 0);

    lv_obj_t *th_t = lv_label_create(c_tmp);
    lv_label_set_text(th_t, "MOTOR & INVERTER THERMAL");
    lv_obj_set_style_text_color(th_t, OI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(th_t, &lv_font_montserrat_12, 0);
    lv_obj_align(th_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_g_temp = lv_label_create(c_tmp);
    lv_label_set_text(lbl_g_temp, "40.5 °C (Heatsink: 34 °C)");
    lv_obj_set_style_text_color(lbl_g_temp, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(lbl_g_temp, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_g_temp, LV_ALIGN_LEFT_MID, 0, -10);

    bar_g_temp = lv_bar_create(c_tmp);
    lv_obj_set_size(bar_g_temp, 265, 20);
    lv_obj_align(bar_g_temp, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_bar_set_range(bar_g_temp, 0, 120);
    lv_bar_set_value(bar_g_temp, 40, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_g_temp, lv_color_hex(0x223040), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_g_temp, OI_COLOR_AMBER, LV_PART_INDICATOR);
}

// ----------------- BUILD TAB 5: DATA LOGGER -----------------
static void build_tab_logger(lv_obj_t *parent) {
    lv_obj_t *lh = lv_label_create(parent);
    lv_label_set_text(lh, "DATA LOGGER & TELEMETRY RECORDER");
    lv_obj_set_style_text_color(lh, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(lh, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lh, 6, 6);

    lv_obj_t *card_state = lv_obj_create(parent);
    lv_obj_set_size(card_state, 625, 90);
    lv_obj_set_pos(card_state, 0, 36);
    lv_obj_set_style_bg_color(card_state, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(card_state, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card_state, 1, 0);
    lv_obj_set_style_radius(card_state, 8, 0);

    lbl_log_state = lv_label_create(card_state);
    lv_label_set_recolor(lbl_log_state, true);
    lv_label_set_text(lbl_log_state, "#54e6a4 [ACTIVE]# Logging to /sdcard/LOG_0042.CSV");
    lv_obj_set_style_text_font(lbl_log_state, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_log_state, LV_ALIGN_TOP_LEFT, 6, 4);

    lbl_log_records = lv_label_create(card_state);
    lv_label_set_text(lbl_log_records, "Records: 14,820 | Rate: 50 Hz | Time: 00:12:21 | Size: 1.4 MB");
    lv_obj_set_style_text_color(lbl_log_records, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_log_records, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_log_records, LV_ALIGN_BOTTOM_LEFT, 6, -8);

    btn_log_toggle = lv_btn_create(card_state);
    lv_obj_set_size(btn_log_toggle, 150, 44);
    lv_obj_align(btn_log_toggle, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_color(btn_log_toggle, lv_color_hex(0x381515), 0);
    lv_obj_set_style_border_color(btn_log_toggle, OI_COLOR_RED, 0);
    lv_obj_set_style_border_width(btn_log_toggle, 1, 0);
    lv_obj_set_style_radius(btn_log_toggle, 6, 0);
    lv_obj_add_event_cb(btn_log_toggle, log_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lt = lv_label_create(btn_log_toggle);
    lv_label_set_text(lt, "STOP LOGGING");
    lv_obj_set_style_text_color(lt, OI_COLOR_RED, 0);
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);
    lv_obj_center(lt);

    ta_log_preview = lv_textarea_create(parent);
    lv_obj_set_size(ta_log_preview, 625, 320);
    lv_obj_set_pos(ta_log_preview, 0, 136);
    lv_obj_set_style_bg_color(ta_log_preview, lv_color_hex(0x0a0e16), 0);
    lv_obj_set_style_border_color(ta_log_preview, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ta_log_preview, 1, 0);
    lv_obj_set_style_text_color(ta_log_preview, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ta_log_preview, &lv_font_montserrat_10, 0);
    lv_textarea_set_text(ta_log_preview,
        "timestamp_ms,speed,udc,idc,power,tmot,ths,status\n"
        "10240,2850,384.2,42.5,16.3,40.5,34.0,0\n"
        "10260,2852,384.1,42.6,16.3,40.5,34.0,0\n"
        "10280,2855,384.0,42.8,16.4,40.5,34.0,0\n"
        "10300,2858,383.9,43.0,16.5,40.6,34.0,0\n"
        "10320,2860,383.8,43.1,16.5,40.6,34.0,0"
    );
}

// ----------------- BUILD TAB 6: CAN TOOLS -----------------
static void build_tab_cantools(lv_obj_t *parent) {
    lv_obj_t *ch = lv_label_create(parent);
    lv_label_set_recolor(ch, true);
    lv_label_set_text(ch, "CAN BUS MONITOR & MAPPING (#38bdf8 500 kbps#)");
    lv_obj_set_style_text_font(ch, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(ch, 6, 6);

    ta_can_log = lv_textarea_create(parent);
    lv_obj_set_size(ta_can_log, 625, 410);
    lv_obj_set_pos(ta_can_log, 0, 36);
    lv_obj_set_style_bg_color(ta_can_log, lv_color_hex(0x0a0e16), 0);
    lv_obj_set_style_border_color(ta_can_log, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(ta_can_log, 1, 0);
    lv_obj_set_style_text_color(ta_can_log, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(ta_can_log, &lv_font_montserrat_10, 0);
    lv_textarea_set_text(ta_can_log,
        "[CAN BUS LIVE TRAFFIC]\n"
        "[RX] ID: 0x181 DLC: 8 DATA: 22 0B A0 0A 10 01 00 00  (speed: 2850, udc: 384.2)\n"
        "[RX] ID: 0x281 DLC: 8 DATA: 29 01 28 00 00 00 00 00  (idc: 42.5, tmot: 40)\n"
        "[RX] ID: 0x381 DLC: 8 DATA: 01 00 00 00 00 00 00 00  (din_forward: 1, opmode: RUN)\n"
        "[TX] ID: 0x180 DLC: 8 DATA: 00 00 00 00 00 00 00 00  (HEARTBEAT OK)"
    );
}

// ----------------- BUILD TAB 7: SETTINGS -----------------
static void build_tab_settings(lv_obj_t *parent) {
    lv_obj_t *sh = lv_label_create(parent);
    lv_label_set_text(sh, "DEVICE, DISPLAY & CAN SETTINGS");
    lv_obj_set_style_text_color(sh, OI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(sh, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(sh, 6, 6);

    // Display & Sleep Card
    lv_obj_t *c_disp = lv_obj_create(parent);
    lv_obj_set_size(c_disp, 625, 110);
    lv_obj_set_pos(c_disp, 0, 34);
    lv_obj_set_style_bg_color(c_disp, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(c_disp, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c_disp, 1, 0);
    lv_obj_set_style_radius(c_disp, 8, 0);

    lv_obj_t *dh = lv_label_create(c_disp);
    lv_label_set_text(dh, "DISPLAY BACKLIGHT & SLEEP");
    lv_obj_set_style_text_color(dh, OI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(dh, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(dh, 0, 0);

    lv_obj_t *btn_slp = lv_btn_create(c_disp);
    lv_obj_set_size(btn_slp, 230, 44);
    lv_obj_set_pos(btn_slp, 0, 30);
    lv_obj_set_style_bg_color(btn_slp, lv_color_hex(0x381515), 0);
    lv_obj_set_style_border_color(btn_slp, OI_COLOR_RED, 0);
    lv_obj_set_style_border_width(btn_slp, 1, 0);
    lv_obj_set_style_radius(btn_slp, 6, 0);
    lv_obj_add_event_cb(btn_slp, screen_sleep_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l_sl = lv_label_create(btn_slp);
    lv_label_set_text(l_sl, "TURN SCREEN OFF (SLEEP)");
    lv_obj_set_style_text_color(l_sl, OI_COLOR_RED, 0);
    lv_obj_set_style_text_font(l_sl, &lv_font_montserrat_12, 0);
    lv_obj_center(l_sl);

    lv_obj_t *hint = lv_label_create(c_disp);
    lv_label_set_text(hint, "* Tap screen glass anywhere to wake back up.");
    lv_obj_set_style_text_color(hint, OI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(hint, 250, 44);

    // Wi-Fi & Network Card
    lv_obj_t *c_net = lv_obj_create(parent);
    lv_obj_set_size(c_net, 625, 120);
    lv_obj_set_pos(c_net, 0, 154);
    lv_obj_set_style_bg_color(c_net, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(c_net, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c_net, 1, 0);
    lv_obj_set_style_radius(c_net, 8, 0);

    lv_obj_t *nh = lv_label_create(c_net);
    lv_label_set_text(nh, "WI-FI & WEB INTERFACE");
    lv_obj_set_style_text_color(nh, OI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(nh, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(nh, 0, 0);

    lv_obj_t *net_info = lv_label_create(c_net);
    lv_label_set_recolor(net_info, true);
    lv_label_set_text(net_info,
        "#9aa3b2 Access Point SSID:#  #e8ecf1 MehranEV-OpenInverter#\n"
        "#9aa3b2 Web Interface URL:#  #54e6a4 http://192.168.4.1/#\n"
        "#9aa3b2 Web Server Port:#   #e8ecf1 80 (HTTP / SSE Stream)#"
    );
    lv_obj_set_style_text_font(net_info, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(net_info, 0, 30);

    // Hardware Specs Card
    lv_obj_t *c_hw = lv_obj_create(parent);
    lv_obj_set_size(c_hw, 625, 170);
    lv_obj_set_pos(c_hw, 0, 284);
    lv_obj_set_style_bg_color(c_hw, OI_COLOR_CARD, 0);
    lv_obj_set_style_border_color(c_hw, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(c_hw, 1, 0);
    lv_obj_set_style_radius(c_hw, 8, 0);

    lv_obj_t *hh = lv_label_create(c_hw);
    lv_label_set_text(hh, "HARDWARE & SYSTEM SPECIFICATIONS");
    lv_obj_set_style_text_color(hh, OI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(hh, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(hh, 0, 0);

    lv_obj_t *hw_info = lv_label_create(c_hw);
    lv_label_set_recolor(hw_info, true);
    lv_label_set_text(hw_info,
        "#9aa3b2 Display:# Waveshare 4.3\" RGB LCD 800x480 (GT911 Capacitive Touch)\n"
        "#9aa3b2 Microcontroller:# ESP32-S3 (Xtensa Dual-Core 240MHz)\n"
        "#9aa3b2 Memory:# 16MB Quad-SPI Flash | 8MB Octal PSRAM\n"
        "#9aa3b2 OpenInverter Firmware:# v4.1 (sperate high-speed mod)\n"
        "#9aa3b2 GUI Library:# LVGL v8.3.11 with custom hardware-accelerated drivers"
    );
    lv_obj_set_style_text_font(hw_info, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(hw_info, 0, 30);
}

// ----------------- ROOT CREATION FUNCTION -----------------
lv_obj_t* create_openinverter_full_ui(lv_obj_t *parent, OiBackClusterCb back_cb, OiSleepCb sleep_cb) {
    s_back_cb = back_cb;
    s_sleep_cb = sleep_cb;

    oi_container = lv_obj_create(parent);
    lv_obj_set_size(oi_container, 800, 480);
    lv_obj_set_pos(oi_container, 0, 0);
    lv_obj_set_style_bg_color(oi_container, OI_COLOR_BG, 0);
    lv_obj_set_style_border_width(oi_container, 0, 0);
    lv_obj_set_style_pad_all(oi_container, 0, 0);
    lv_obj_clear_flag(oi_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(oi_container, LV_OBJ_FLAG_HIDDEN);

    // Left Navigation Sidebar (160 px wide)
    lv_obj_t *sidebar = lv_obj_create(oi_container);
    lv_obj_set_size(sidebar, 160, 480);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_style_bg_color(sidebar, OI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(sidebar, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(sidebar, 1, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_pad_all(sidebar, 6, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // Back to Mehran Cluster Button
    lv_obj_t *btn_back = lv_btn_create(sidebar);
    lv_obj_set_size(btn_back, 146, 36);
    lv_obj_set_pos(btn_back, 0, 4);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x27140e), 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0xff5722), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_add_event_cb(btn_back, back_cluster_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b_lbl = lv_label_create(btn_back);
    lv_label_set_text(b_lbl, "< MEHRAN EV");
    lv_obj_set_style_text_color(b_lbl, lv_color_hex(0xff5722), 0);
    lv_obj_set_style_text_font(b_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(b_lbl);

    // 8 Navigation Tab Buttons
    const char *tab_titles[NUM_OI_TABS] = {
        "Dashboard", "Parameters", "Spot Values", "Live Plot",
        "Gauges", "Data Logger", "CAN Tools", "Settings"
    };

    for (int i = 0; i < NUM_OI_TABS; i++) {
        oi_nav_btns[i] = lv_btn_create(sidebar);
        lv_obj_set_size(oi_nav_btns[i], 146, 36);
        lv_obj_set_pos(oi_nav_btns[i], 0, 46 + i * 40);
        lv_obj_set_style_bg_color(oi_nav_btns[i], (i == 0) ? lv_color_hex(0x1c2b3d) : OI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_color(oi_nav_btns[i], (i == 0) ? OI_COLOR_CYAN : OI_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(oi_nav_btns[i], (i == 0) ? 2 : 1, 0);
        lv_obj_set_style_radius(oi_nav_btns[i], 6, 0);
        lv_obj_add_event_cb(oi_nav_btns[i], tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *tl = lv_label_create(oi_nav_btns[i]);
        lv_label_set_text(tl, tab_titles[i]);
        lv_obj_set_style_text_color(tl, OI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
        lv_obj_center(tl);
    }

    // Sidebar Footer
    lv_obj_t *foot = lv_obj_create(sidebar);
    lv_obj_set_size(foot, 146, 95);
    lv_obj_set_pos(foot, 0, 372);
    lv_obj_set_style_bg_color(foot, lv_color_hex(0x0a0e16), 0);
    lv_obj_set_style_border_color(foot, OI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(foot, 1, 0);
    lv_obj_set_style_radius(foot, 6, 0);
    lv_obj_set_style_pad_all(foot, 4, 0);

    lv_obj_t *w_lbl = lv_label_create(foot);
    lv_label_set_recolor(w_lbl, true);
    lv_label_set_text(w_lbl, "#34d399 *# AP: 192.168.4.1\n#38bdf8 *# CAN: 500k LIVE\n#9aa3b2 F/W:# v4.1 mod\n#9aa3b2 Web:# LVGL Native");
    lv_obj_set_style_text_font(w_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(w_lbl, LV_ALIGN_TOP_LEFT, 2, 2);

    // Right Content Area: 8 Views
    for (int i = 0; i < NUM_OI_TABS; i++) {
        oi_views[i] = lv_obj_create(oi_container);
        lv_obj_set_size(oi_views[i], 640, 480);
        lv_obj_set_pos(oi_views[i], 160, 0);
        lv_obj_set_style_bg_color(oi_views[i], OI_COLOR_BG, 0);
        lv_obj_set_style_border_width(oi_views[i], 0, 0);
        lv_obj_set_style_pad_all(oi_views[i], 8, 0);
        if (i != 0) lv_obj_add_flag(oi_views[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_tab_dashboard(oi_views[0]);
    build_tab_parameters(oi_views[1]);
    build_tab_spotvalues(oi_views[2]);
    build_tab_plot(oi_views[3]);
    build_tab_gauges(oi_views[4]);
    build_tab_logger(oi_views[5]);
    build_tab_cantools(oi_views[6]);
    build_tab_settings(oi_views[7]);

    return oi_container;
}

// ----------------- UPDATE TELEMETRY -----------------
void update_openinverter_telemetry(const InverterTelemetry &telem) {
    if (!oi_container) return;

    // Update Dashboard Metrics
    if (lbl_dash_udc) {
        char buf[32];
        sprintf(buf, "%.1f V", telem.pack_volts);
        lv_label_set_text(lbl_dash_udc, buf);
        lv_bar_set_value(bar_dash_udc, (int)((telem.pack_volts - 250.0f) * 100.0f / 200.0f), LV_ANIM_OFF);
    }
    if (lbl_dash_idc) {
        char buf[32];
        sprintf(buf, "%.1f A", telem.pack_current);
        lv_label_set_text(lbl_dash_idc, buf);
        lv_bar_set_value(bar_dash_idc, (int)(telem.pack_current * 100.0f / 150.0f), LV_ANIM_OFF);
    }
    if (lbl_dash_speed) {
        char buf[32];
        sprintf(buf, "%d RPM", (int)telem.motor_rpm);
        lv_label_set_text(lbl_dash_speed, buf);
        lv_bar_set_value(bar_dash_speed, (int)(telem.motor_rpm * 100.0f / 7000.0f), LV_ANIM_OFF);
    }
    if (lbl_dash_power) {
        char buf[32];
        sprintf(buf, "%+.1f kW", telem.power_kw);
        lv_label_set_text(lbl_dash_power, buf);
        int p_pct = (int)((telem.power_kw + 20.0f) * 100.0f / 80.0f);
        if (p_pct < 0) p_pct = 0; if (p_pct > 100) p_pct = 100;
        lv_bar_set_value(bar_dash_power, p_pct, LV_ANIM_OFF);
    }

    // Update Spot Values Table
    if (tbl_spot) {
        char buf[32];
        sprintf(buf, "%d", (int)telem.motor_rpm);
        lv_table_set_cell_value(tbl_spot, 0, 1, buf);
        sprintf(buf, "%.1f", telem.pack_volts);
        lv_table_set_cell_value(tbl_spot, 0, 3, buf);

        sprintf(buf, "%.1f", telem.pack_current);
        lv_table_set_cell_value(tbl_spot, 1, 1, buf);
        sprintf(buf, "%+.1f", telem.power_kw);
        lv_table_set_cell_value(tbl_spot, 1, 3, buf);

        sprintf(buf, "%.1f", telem.motor_temp);
        lv_table_set_cell_value(tbl_spot, 2, 1, buf);
        sprintf(buf, "%.1f", telem.heatsink_temp);
        lv_table_set_cell_value(tbl_spot, 2, 3, buf);

        sprintf(buf, "%.1f", telem.pack_current * 0.92f);
        lv_table_set_cell_value(tbl_spot, 3, 1, buf);
        sprintf(buf, "%.1f", telem.pack_current * 0.94f);
        lv_table_set_cell_value(tbl_spot, 3, 3, buf);

        sprintf(buf, "%.1f", telem.motor_rpm * 0.033f);
        lv_table_set_cell_value(tbl_spot, 4, 1, buf);
        sprintf(buf, "%.1f", telem.pack_volts * 0.65f);
        lv_table_set_cell_value(tbl_spot, 4, 3, buf);

        sprintf(buf, "%d", (int)(telem.speed_kmh * 28.0f + 350.0f));
        lv_table_set_cell_value(tbl_spot, 5, 1, buf);
        sprintf(buf, "%d", (int)(telem.speed_kmh * 27.5f + 348.0f));
        lv_table_set_cell_value(tbl_spot, 5, 3, buf);

        lv_table_set_cell_value(tbl_spot, 6, 1, telem.din_fwd ? "1 [ACTIVE]" : "0 [OFF]");
        lv_table_set_cell_value(tbl_spot, 6, 3, telem.din_rev ? "1 [ACTIVE]" : "0 [OFF]");

        lv_table_set_cell_value(tbl_spot, 7, 1, telem.din_brake ? "1 [ACTIVE]" : "0 [OFF]");
        lv_table_set_cell_value(tbl_spot, 7, 3, telem.lasterr_str ? telem.lasterr_str : "NONE (0)");
    }

    // Update Oscilloscope Chart
    if (chart_plot && plot_running && plot_ser_speed && plot_ser_power) {
        int sp_val = (int)(telem.motor_rpm * 100.0f / 7000.0f);
        int pw_val = (int)((telem.power_kw + 20.0f) * 100.0f / 80.0f);
        if (sp_val < 0) sp_val = 0; if (sp_val > 100) sp_val = 100;
        if (pw_val < 0) pw_val = 0; if (pw_val > 100) pw_val = 100;
        lv_chart_set_next_value(chart_plot, plot_ser_speed, sp_val);
        lv_chart_set_next_value(chart_plot, plot_ser_power, pw_val);
    }

    // Update Gauges Tab
    if (meter_g_speed && indic_g_speed_needle) {
        lv_meter_set_indicator_value(meter_g_speed, indic_g_speed_needle, (int)telem.speed_kmh);
        lv_meter_set_indicator_end_value(meter_g_speed, indic_g_speed_arc, (int)telem.speed_kmh);
        if (lbl_g_speed_num) {
            char buf[32];
            sprintf(buf, "%d km/h", (int)telem.speed_kmh);
            lv_label_set_text(lbl_g_speed_num, buf);
        }
    }
    if (meter_g_power && indic_g_power_needle) {
        int pw_meter = (int)telem.power_kw;
        if (pw_meter < 0) pw_meter = 0;
        lv_meter_set_indicator_value(meter_g_power, indic_g_power_needle, pw_meter);
        if (lbl_g_power_num) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%+.1f kW", telem.power_kw);
            lv_label_set_text(lbl_g_power_num, buf);
        }
    }
    if (bar_g_bat && lbl_g_bat) {
        lv_bar_set_value(bar_g_bat, (int)telem.battery_soc, LV_ANIM_OFF);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d%% (%.1f V)", (int)telem.battery_soc, telem.pack_volts);
        lv_label_set_text(lbl_g_bat, buf);
    }
    if (bar_g_temp && lbl_g_temp) {
        lv_bar_set_value(bar_g_temp, (int)telem.motor_temp, LV_ANIM_OFF);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.1f °C (Heatsink: %.1f °C)", telem.motor_temp, telem.heatsink_temp);
        lv_label_set_text(lbl_g_temp, buf);
    }

    // Update Direction Buttons State
    if (btn_g_fwd) {
        lv_obj_set_style_bg_color(btn_g_fwd, telem.din_fwd ? lv_color_hex(0x13402d) : lv_color_hex(0x1a2433), 0);
        lv_obj_set_style_border_color(btn_g_fwd, telem.din_fwd ? OI_COLOR_GREEN : OI_COLOR_BORDER, 0);
    }
    if (btn_g_rev) {
        lv_obj_set_style_bg_color(btn_g_rev, telem.din_rev ? lv_color_hex(0x381515) : lv_color_hex(0x1a2433), 0);
        lv_obj_set_style_border_color(btn_g_rev, telem.din_rev ? OI_COLOR_RED : OI_COLOR_BORDER, 0);
    }
    if (btn_g_brk) {
        lv_obj_set_style_bg_color(btn_g_brk, telem.din_brake ? lv_color_hex(0x381515) : lv_color_hex(0x1a2433), 0);
        lv_obj_set_style_border_color(btn_g_brk, telem.din_brake ? OI_COLOR_RED : OI_COLOR_BORDER, 0);
    }
}
