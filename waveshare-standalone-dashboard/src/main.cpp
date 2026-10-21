#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <lvgl.h>
#include "openinverter_ui.h"

#define LCD_H_RES              800
#define LCD_V_RES              480
#define LCD_PIXEL_CLOCK_HZ     (12 * 1000 * 1000)

// Waveshare ESP32-S3-Touch-LCD-4.3 Pinout
#define LCD_PIN_PCLK           7
#define LCD_PIN_VSYNC          3
#define LCD_PIN_HSYNC          46
#define LCD_PIN_DE             5

#define LCD_PIN_R0             1
#define LCD_PIN_R1             2
#define LCD_PIN_R2             42
#define LCD_PIN_R3             41
#define LCD_PIN_R4             40

#define LCD_PIN_G0             39
#define LCD_PIN_G1             0
#define LCD_PIN_G2             45
#define LCD_PIN_G3             48
#define LCD_PIN_G4             47
#define LCD_PIN_G5             21

#define LCD_PIN_B0             14
#define LCD_PIN_B1             38
#define LCD_PIN_B2             18
#define LCD_PIN_B3             17
#define LCD_PIN_B4             10

#define I2C_SDA_PIN            8
#define I2C_SCL_PIN            9
#define CH422G_REG_MODE        0x24
#define CH422G_REG_OUT         0x38

// GT911 Touch
#define GT911_REG_STATUS       0x814E
#define GT911_REG_COORD        0x8150
static uint8_t gt911_addr = 0x5D;

// Exact Pixel-Matched Bugatti/Mehran EV Color Palette
#define COLOR_APP_BG            lv_color_hex(0x0a0c10)  // Deepest obsidian backdrop
#define COLOR_CLUSTER_HOUSING   lv_color_hex(0x0f131a)  // Outer cluster housing
#define COLOR_CLUSTER_BORDER    lv_color_hex(0x1e2738)  // Bezel edge stroke
#define COLOR_DIAL_BG           lv_color_hex(0x0c1017)  // Dial face dark background
#define COLOR_DIAL_BORDER       lv_color_hex(0x222e42)  // Dial outer ring border
#define COLOR_DIAL_GLOW_ARC     lv_color_hex(0x38bdf8)  // Luminous cyan dial glow arc

#define COLOR_TITLE_ORANGE      lv_color_hex(0xd97706)  // "SUZUKI MEHRAN EV" amber-orange
#define COLOR_CAN_GREEN         lv_color_hex(0x10b981)  // "CAN 500k / UART LIVE"
#define COLOR_TEXT_WHITE        lv_color_hex(0xf8fafc)  // Speedometer numerals & primary text
#define COLOR_TEXT_MUTED        lv_color_hex(0x64748b)  // "SUZUKI MEHRAN", "km/h", subtitles
#define COLOR_NEEDLE_ORANGE     lv_color_hex(0xff5722)  // Illuminated orange needles

#define COLOR_GEAR_INACTIVE     lv_color_hex(0x64748b)  // P, R, N gray
#define COLOR_GEAR_ACTIVE_BG    lv_color_hex(0x38130a)  // Dark reddish-orange container
#define COLOR_GEAR_ACTIVE_FG    lv_color_hex(0xfb923c)  // Bright orange [D]
#define COLOR_GEAR_ACTIVE_BD    lv_color_hex(0xea580c)  // Orange glowing border

#define COLOR_READY_BG          lv_color_hex(0x032920)  // READY pill background
#define COLOR_READY_FG          lv_color_hex(0x34d399)  // READY pill text & border
#define COLOR_BAT_CYAN          lv_color_hex(0x38bdf8)  // Battery "84%" & Card 1 cyan bar
#define COLOR_POWER_ORANGE      lv_color_hex(0xfb923c)  // Power "+1.1" and "POWER (kW)" header
#define COLOR_EFF_GREEN         lv_color_hex(0x34d399)  // Efficiency "16 Wh/km" & header
#define COLOR_AUX_GREEN         lv_color_hex(0x34d399)  // "DC-DC NORMAL" green
#define COLOR_TEMP_RED          lv_color_hex(0xef4444)  // Temp "H" and Battery "E" red
#define COLOR_ODO_BOX_BG        lv_color_hex(0x05070a)  // Black odometer tumbler
#define COLOR_ODO_RED_BOX       lv_color_hex(0xdc2626)  // Red 10th-km tumbler
#define COLOR_SUZUKI_RED        lv_color_hex(0xe60012)  // Iconic Suzuki Racing Red

// OpenInverter Colors
#define COLOR_OI_BG             lv_color_hex(0x07090e)
#define COLOR_OI_SURFACE        lv_color_hex(0x10151d)
#define COLOR_OI_CARD           lv_color_hex(0x18202d)
#define COLOR_OI_BORDER         lv_color_hex(0x263040)
#define COLOR_OI_CYAN           lv_color_hex(0x4cc9f0)

static esp_lcd_panel_handle_t panel_handle = NULL;
static WebServer server(80);
static bool backlight_state = true;

// LVGL Draw Buffer (40 lines in internal DMA RAM)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf_1 = NULL;

// Telemetry state
static float current_speed_kmh = 58.0f;
static float battery_soc = 84.0f;
static float motor_temp = 40.0f;
static float pack_volts = 116.3f;
static float pack_current = 9.5f;
static float power_kw = 1.1f;
static float avg_efficiency = 16.0f;
static float aux_12v = 13.8f;
static double odometer_km = 81779.3;
static int active_gear = 3; // 0=P, 1=R, 2=N, 3=D
static float sim_time = 0.0f;

// Mode State: true = Mehran EV Cluster, false = OpenInverter WebUI
static bool show_mehran_cluster = true;

// Startup Animation State Machine
enum StartupState {
    BOOT_LOGO,
    BOOT_SWEEP_UP,
    BOOT_SWEEP_DOWN,
    BOOT_RUNNING
};
static StartupState boot_state = BOOT_LOGO;
static unsigned long boot_start_ms = 0;

// Parent Containers
static lv_obj_t *container_boot = NULL;
static lv_obj_t *container_mehran = NULL;
static lv_obj_t *container_oi = NULL;

// Boot Screen Objects
static lv_obj_t *bar_boot_progress = NULL;
static lv_obj_t *lbl_boot_status = NULL;

// Mehran EV Cluster Widgets
static lv_obj_t *meter_speed = NULL;
static lv_meter_indicator_t *indic_speed_needle = NULL;
static lv_meter_indicator_t *indic_rpm_needle = NULL;
static lv_meter_indicator_t *indic_rpm_arc = NULL;
static lv_obj_t *lbl_speed_digital = NULL;
static lv_obj_t *lbl_rpm_val = NULL;
static float motor_rpm = 2900.0f;
static lv_obj_t *lbl_odo_digits[6];
static lv_obj_t *meter_battery = NULL;
static lv_meter_indicator_t *indic_bat_needle = NULL;
static lv_obj_t *lbl_bat_pct = NULL;
static lv_obj_t *meter_temp = NULL;
static lv_meter_indicator_t *indic_temp_needle = NULL;
static lv_obj_t *lbl_temp_val = NULL;

// Gear Buttons
static lv_obj_t *gear_btns[4];
static const char *gear_labels[4] = {"P", "R", "N", "D"};

// Top Cockpit Indicators
static lv_obj_t *lbl_turn_l = NULL;
static lv_obj_t *lbl_light = NULL;
static lv_obj_t *lbl_turn_r = NULL;
static lv_obj_t *btn_ready = NULL;
static lv_obj_t *btn_brake = NULL;

// 2-Row Telemetry Cards
static lv_obj_t *lbl_card_volts = NULL;
static lv_obj_t *lbl_card_current = NULL;
static lv_obj_t *lbl_card_power = NULL;
static lv_obj_t *lbl_card_eff = NULL;
static lv_obj_t *lbl_card_aux = NULL;
static lv_obj_t *lbl_card_range = NULL;

static void toggle_sleep_mode();

// Forward declarations
static void toggle_dashboard_mode();

// ----------------- Hardware Drivers -----------------
void setBacklight(bool on) {
    backlight_state = on;
    Wire.beginTransmission(CH422G_REG_MODE);
    Wire.write(0x01);
    Wire.endTransmission();

    Wire.beginTransmission(CH422G_REG_OUT);
    uint8_t out = on ? 0x1F : 0x1B;
    Wire.write(out);
    Wire.endTransmission();
}

void resetTouchHardware() {
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW); // Latch address 0x5D
    delay(20);

    Wire.beginTransmission(CH422G_REG_MODE);
    Wire.write(0x01);
    Wire.endTransmission();

    Wire.beginTransmission(CH422G_REG_OUT);
    Wire.write(0x1D); // TP_RST LOW
    Wire.endTransmission();
    delay(100);

    Wire.beginTransmission(CH422G_REG_OUT);
    Wire.write(0x1F); // TP_RST HIGH
    Wire.endTransmission();
    delay(150);

    pinMode(4, INPUT);
    delay(50);
}

void scanI2C() {
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (addr == 0x5D || addr == 0x14) {
                gt911_addr = addr;
            }
        }
    }
}

bool getTouch(int &tx, int &ty) {
    Wire.beginTransmission(gt911_addr);
    Wire.write(0x81);
    Wire.write(0x4E);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom((uint8_t)gt911_addr, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t status = Wire.read();

    if ((status & 0x80) && ((status & 0x0F) > 0)) {
        Wire.beginTransmission(gt911_addr);
        Wire.write(0x81);
        Wire.write(0x50);
        Wire.endTransmission(false);

        Wire.requestFrom((uint8_t)gt911_addr, (uint8_t)6);
        if (Wire.available() >= 6) {
            uint8_t buf[6];
            Wire.readBytes(buf, 6);
            tx = buf[0] | (buf[1] << 8);
            ty = buf[2] | (buf[3] << 8);

            Wire.beginTransmission(gt911_addr);
            Wire.write(0x81);
            Wire.write(0x4E);
            Wire.write(0x00);
            Wire.endTransmission();
            return true;
        }
    }

    if (status & 0x80) {
        Wire.beginTransmission(gt911_addr);
        Wire.write(0x81);
        Wire.write(0x4E);
        Wire.write(0x00);
        Wire.endTransmission();
    }
    return false;
}

void initLcd() {
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 16,
            .vsync_front_porch = 16,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = 16,
        .sram_trans_align = 0,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .pclk_gpio_num = LCD_PIN_PCLK,
        // Blue in bits 0-4, Green in bits 5-10, Red in bits 11-15 (Correct Hardware Alignment)
        .data_gpio_nums = {
            LCD_PIN_B0, LCD_PIN_B1, LCD_PIN_B2, LCD_PIN_B3, LCD_PIN_B4,
            LCD_PIN_G0, LCD_PIN_G1, LCD_PIN_G2, LCD_PIN_G3, LCD_PIN_G4, LCD_PIN_G5,
            LCD_PIN_R0, LCD_PIN_R1, LCD_PIN_R2, LCD_PIN_R3, LCD_PIN_R4
        },
        .disp_gpio_num = -1,
        .on_frame_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {
            .fb_in_psram = 1,
        },
    };

    esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
}

// ----------------- LVGL Callbacks -----------------
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(disp_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    int tx, ty;
    if (getTouch(tx, ty)) {
        if (!backlight_state) {
            setBacklight(true);
        }
        data->state = LV_INDEV_STATE_PR;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ----------------- Event Handlers -----------------
static void gear_btn_cb(lv_event_t *e) {
    int gear_idx = (int)(intptr_t)lv_event_get_user_data(e);
    active_gear = gear_idx;
    for (int i = 0; i < 4; i++) {
        if (i == active_gear) {
            lv_obj_set_style_bg_color(gear_btns[i], COLOR_GEAR_ACTIVE_BG, 0);
            lv_obj_set_style_border_color(gear_btns[i], COLOR_GEAR_ACTIVE_BD, 0);
            lv_obj_set_style_border_width(gear_btns[i], 2, 0);
            lv_obj_t *lbl = lv_obj_get_child(gear_btns[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, COLOR_GEAR_ACTIVE_FG, 0);
        } else {
            lv_obj_set_style_bg_color(gear_btns[i], lv_color_hex(0x131922), 0);
            lv_obj_set_style_border_color(gear_btns[i], COLOR_CLUSTER_BORDER, 0);
            lv_obj_set_style_border_width(gear_btns[i], 1, 0);
            lv_obj_t *lbl = lv_obj_get_child(gear_btns[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, COLOR_GEAR_INACTIVE, 0);
        }
    }
}

static void toggle_tuning_cb(lv_event_t *e) {
    toggle_dashboard_mode();
}

static void toggle_dashboard_mode() {
    show_mehran_cluster = !show_mehran_cluster;
    if (show_mehran_cluster) {
        lv_obj_clear_flag(container_mehran, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(container_oi, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(container_mehran, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(container_oi, LV_OBJ_FLAG_HIDDEN);
    }
}

// ----------------- BUILD SUZUKI BOOT ANIMATION -----------------
static void create_boot_screen(lv_obj_t *parent) {
    container_boot = lv_obj_create(parent);
    lv_obj_set_size(container_boot, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(container_boot, 0, 0);
    lv_obj_set_style_bg_color(container_boot, lv_color_hex(0x05070a), 0);
    lv_obj_set_style_border_width(container_boot, 0, 0);
    lv_obj_set_style_pad_all(container_boot, 0, 0);
    lv_obj_clear_flag(container_boot, LV_OBJ_FLAG_SCROLLABLE);

    // Iconic Suzuki "S" Emblem Box (Center Top)
    lv_obj_t *s_box = lv_obj_create(container_boot);
    lv_obj_set_size(s_box, 130, 130);
    lv_obj_align(s_box, LV_ALIGN_CENTER, 0, -65);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_box, 0, 0);
    lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);

    // Suzuki "S" Geometry (Layered Sharp Precision Cuts in Suzuki Racing Red)
    // 1. Top Blade
    lv_obj_t *bar_top = lv_obj_create(s_box);
    lv_obj_set_size(bar_top, 94, 24);
    lv_obj_set_pos(bar_top, 18, 10);
    lv_obj_set_style_bg_color(bar_top, COLOR_SUZUKI_RED, 0);
    lv_obj_set_style_border_color(bar_top, lv_color_hex(0xff8a80), 0);
    lv_obj_set_style_border_width(bar_top, 1, 0);
    lv_obj_set_style_radius(bar_top, 3, 0);

    // 2. Middle Diagonal Slash
    lv_obj_t *bar_mid = lv_obj_create(s_box);
    lv_obj_set_size(bar_mid, 82, 24);
    lv_obj_align(bar_mid, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bar_mid, COLOR_SUZUKI_RED, 0);
    lv_obj_set_style_border_color(bar_mid, lv_color_hex(0xff8a80), 0);
    lv_obj_set_style_border_width(bar_mid, 1, 0);
    lv_obj_set_style_radius(bar_mid, 3, 0);

    // 3. Bottom Blade
    lv_obj_t *bar_bot = lv_obj_create(s_box);
    lv_obj_set_size(bar_bot, 94, 24);
    lv_obj_set_pos(bar_bot, 18, 96);
    lv_obj_set_style_bg_color(bar_bot, COLOR_SUZUKI_RED, 0);
    lv_obj_set_style_border_color(bar_bot, lv_color_hex(0xff8a80), 0);
    lv_obj_set_style_border_width(bar_bot, 1, 0);
    lv_obj_set_style_radius(bar_bot, 3, 0);

    // 4. Left and Right Vertical Connectors for the "S" silhouette
    lv_obj_t *col_l = lv_obj_create(s_box);
    lv_obj_set_size(col_l, 24, 52);
    lv_obj_set_pos(col_l, 18, 14);
    lv_obj_set_style_bg_color(col_l, COLOR_SUZUKI_RED, 0);
    lv_obj_set_style_border_width(col_l, 0, 0);

    lv_obj_t *col_r = lv_obj_create(s_box);
    lv_obj_set_size(col_r, 24, 52);
    lv_obj_set_pos(col_r, 88, 64);
    lv_obj_set_style_bg_color(col_r, COLOR_SUZUKI_RED, 0);
    lv_obj_set_style_border_width(col_r, 0, 0);

    // SUZUKI Brand Typography
    lv_obj_t *lbl_suzu = lv_label_create(container_boot);
    lv_label_set_text(lbl_suzu, "S  U  Z  U  K  I");
    lv_obj_set_style_text_color(lbl_suzu, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_suzu, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_suzu, LV_ALIGN_CENTER, 0, 42);

    // MEHRAN ELECTRIC
    lv_obj_t *lbl_m_ev = lv_label_create(container_boot);
    lv_label_set_text(lbl_m_ev, "M E H R A N   E L E C T R I C   E V");
    lv_obj_set_style_text_color(lbl_m_ev, COLOR_NEEDLE_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_m_ev, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_m_ev, LV_ALIGN_CENTER, 0, 78);

    // Boot Progress Bar
    bar_boot_progress = lv_bar_create(container_boot);
    lv_obj_set_size(bar_boot_progress, 340, 4);
    lv_obj_align(bar_boot_progress, LV_ALIGN_CENTER, 0, 120);
    lv_bar_set_range(bar_boot_progress, 0, 100);
    lv_bar_set_value(bar_boot_progress, 15, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_boot_progress, lv_color_hex(0x1e2738), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_boot_progress, COLOR_BAT_CYAN, LV_PART_INDICATOR);

    // Boot Status Text
    lbl_boot_status = lv_label_create(container_boot);
    lv_label_set_text(lbl_boot_status, "INITIALIZING POWERTRAIN CONTROLLER...");
    lv_obj_set_style_text_color(lbl_boot_status, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_boot_status, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_boot_status, LV_ALIGN_CENTER, 0, 142);
}

// ----------------- BUILD SUZUKI MEHRAN EV LUXURY CLUSTER -----------------
static void create_mehran_cluster(lv_obj_t *parent) {
    container_mehran = lv_obj_create(parent);
    lv_obj_set_size(container_mehran, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(container_mehran, 0, 0);
    lv_obj_set_style_bg_color(container_mehran, COLOR_APP_BG, 0);
    lv_obj_set_style_border_width(container_mehran, 0, 0);
    lv_obj_set_style_pad_all(container_mehran, 0, 0);
    lv_obj_clear_flag(container_mehran, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_mehran, LV_OBJ_FLAG_HIDDEN); // Hidden during boot animation

    // ================= LEFT: GRAND SPEEDOMETER (TOP TO BOTTOM) =================
    // Covers 456x456 px on the left side (from Y=12 to Y=468 on 480px screen)
    meter_speed = lv_meter_create(container_mehran);
    lv_obj_set_size(meter_speed, 456, 456);
    lv_obj_set_pos(meter_speed, 10, 12);
    lv_obj_set_style_bg_color(meter_speed, COLOR_DIAL_BG, 0);
    lv_obj_set_style_border_color(meter_speed, COLOR_DIAL_BORDER, 0);
    lv_obj_set_style_border_width(meter_speed, 3, 0);
    lv_obj_set_style_radius(meter_speed, LV_RADIUS_CIRCLE, 0);

    // Outer Speed scale (0 to 140 km/h)
    lv_meter_scale_t *scale_sp = lv_meter_add_scale(meter_speed);
    lv_meter_set_scale_ticks(meter_speed, scale_sp, 57, 2, 10, lv_color_hex(0x334155));
    lv_meter_set_scale_major_ticks(meter_speed, scale_sp, 8, 4, 18, COLOR_TEXT_WHITE, 16);
    lv_meter_set_scale_range(meter_speed, scale_sp, 0, 140, 240, 150);

    // Glowing outer speed arc ring
    lv_meter_indicator_t *arc_sp = lv_meter_add_arc(meter_speed, scale_sp, 3, COLOR_DIAL_GLOW_ARC, 2);
    lv_meter_set_indicator_start_value(meter_speed, arc_sp, 0);
    lv_meter_set_indicator_end_value(meter_speed, arc_sp, 140);

    // Speed needle (Long Orange, single clean dial needle)
    indic_speed_needle = lv_meter_add_needle_line(meter_speed, scale_sp, 5, COLOR_NEEDLE_ORANGE, -26);
    lv_meter_set_indicator_value(meter_speed, indic_speed_needle, 0);

    // Brand header inside Grand Dial
    lv_obj_t *lbl_brand = lv_label_create(meter_speed);
    lv_label_set_text(lbl_brand, "SUZUKI MEHRAN");
    lv_obj_set_style_text_color(lbl_brand, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(lbl_brand, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_brand, LV_ALIGN_TOP_MID, 0, 68);

    lv_obj_t *lbl_subbrand = lv_label_create(meter_speed);
    lv_label_set_text(lbl_subbrand, "- EV HYPERDRIVE -");
    lv_obj_set_style_text_color(lbl_subbrand, COLOR_POWER_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_subbrand, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_subbrand, LV_ALIGN_TOP_MID, 0, 86);

    // Mechanical Rolling Odometer (Above center)
    lv_obj_t *odo_box = lv_obj_create(meter_speed);
    lv_obj_set_size(odo_box, 114, 24);
    lv_obj_align(odo_box, LV_ALIGN_CENTER, 0, -42);
    lv_obj_set_style_bg_color(odo_box, lv_color_hex(0x05070a), 0);
    lv_obj_set_style_border_color(odo_box, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(odo_box, 1, 0);
    lv_obj_set_style_radius(odo_box, 3, 0);
    lv_obj_set_style_pad_all(odo_box, 1, 0);
    lv_obj_clear_flag(odo_box, LV_OBJ_FLAG_SCROLLABLE);

    const char *odo_init = "817807";
    for (int d = 0; d < 6; d++) {
        lv_obj_t *cell = lv_obj_create(odo_box);
        lv_obj_set_size(cell, 18, 20);
        lv_obj_set_pos(cell, d * 18, 0);
        lv_obj_set_style_bg_color(cell, (d == 5) ? COLOR_ODO_RED_BOX : COLOR_ODO_BOX_BG, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x1e293b), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_radius(cell, 1, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lbl_odo_digits[d] = lv_label_create(cell);
        char ch[2] = {odo_init[d], '\0'};
        lv_label_set_text(lbl_odo_digits[d], ch);
        lv_obj_set_style_text_color(lbl_odo_digits[d], COLOR_TEXT_WHITE, 0);
        lv_obj_set_style_text_font(lbl_odo_digits[d], &lv_font_montserrat_12, 0);
        lv_obj_center(lbl_odo_digits[d]);
    }

    lv_obj_t *lbl_odo_sub = lv_label_create(meter_speed);
    lv_label_set_text(lbl_odo_sub, "TOTAL ODOMETER");
    lv_obj_set_style_text_color(lbl_odo_sub, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(lbl_odo_sub, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_odo_sub, LV_ALIGN_CENTER, 0, -22);

    // Center pivot cap
    lv_obj_t *center_cap = lv_obj_create(meter_speed);
    lv_obj_set_size(center_cap, 24, 24);
    lv_obj_center(center_cap);
    lv_obj_set_style_bg_color(center_cap, lv_color_hex(0x18202d), 0);
    lv_obj_set_style_border_color(center_cap, lv_color_hex(0x64748b), 0);
    lv_obj_set_style_border_width(center_cap, 2, 0);
    lv_obj_set_style_radius(center_cap, LV_RADIUS_CIRCLE, 0);

    // Large Bold Digital Speed Readout (BELOW THE ODOMETER)
    lbl_speed_digital = lv_label_create(meter_speed);
    lv_label_set_text(lbl_speed_digital, "0");
    lv_obj_set_style_text_color(lbl_speed_digital, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_speed_digital, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_speed_digital, LV_ALIGN_CENTER, 0, 42);

    // km / h unit
    lv_obj_t *lbl_unit = lv_label_create(meter_speed);
    lv_label_set_text(lbl_unit, "km / h");
    lv_obj_set_style_text_color(lbl_unit, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_CENTER, 0, 76);

    // Digital Motor RPM Readout (BELOW DIGITAL SPEED)
    lbl_rpm_val = lv_label_create(meter_speed);
    lv_label_set_text(lbl_rpm_val, "0 RPM");
    lv_obj_set_style_text_color(lbl_rpm_val, lv_color_hex(0x67e8f9), 0);
    lv_obj_set_style_text_font(lbl_rpm_val, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_rpm_val, LV_ALIGN_CENTER, 0, 96);

    // ================= RIGHT: TOP COCKPIT BAR =================
    // Width: X = 472 to 792 (320px)
    lbl_turn_l = lv_label_create(container_mehran);
    lv_label_set_text(lbl_turn_l, "<");
    lv_obj_set_style_text_color(lbl_turn_l, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_turn_l, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_turn_l, 474, 16);

    // Interactive Gear Selector (P R N [D])
    for (int i = 0; i < 4; i++) {
        gear_btns[i] = lv_btn_create(container_mehran);
        lv_obj_set_size(gear_btns[i], 30, 28);
        lv_obj_set_pos(gear_btns[i], 498 + i * 34, 10);
        lv_obj_set_style_bg_color(gear_btns[i], (i == active_gear) ? COLOR_GEAR_ACTIVE_BG : lv_color_hex(0x131922), 0);
        lv_obj_set_style_border_color(gear_btns[i], (i == active_gear) ? COLOR_GEAR_ACTIVE_BD : COLOR_CLUSTER_BORDER, 0);
        lv_obj_set_style_border_width(gear_btns[i], (i == active_gear) ? 2 : 1, 0);
        lv_obj_set_style_radius(gear_btns[i], 6, 0);
        lv_obj_add_event_cb(gear_btns[i], gear_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *gl = lv_label_create(gear_btns[i]);
        lv_label_set_text(gl, gear_labels[i]);
        lv_obj_set_style_text_color(gl, (i == active_gear) ? COLOR_GEAR_ACTIVE_FG : COLOR_GEAR_INACTIVE, 0);
        lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, 0);
        lv_obj_center(gl);
    }

    // READY badge
    btn_ready = lv_obj_create(container_mehran);
    lv_obj_set_size(btn_ready, 64, 28);
    lv_obj_set_pos(btn_ready, 640, 10);
    lv_obj_set_style_bg_color(btn_ready, COLOR_READY_BG, 0);
    lv_obj_set_style_border_color(btn_ready, COLOR_READY_FG, 0);
    lv_obj_set_style_border_width(btn_ready, 1, 0);
    lv_obj_set_style_radius(btn_ready, 6, 0);
    lv_obj_set_style_pad_all(btn_ready, 2, 0);

    lv_obj_t *rl = lv_label_create(btn_ready);
    lv_label_set_text(rl, "READY");
    lv_obj_set_style_text_color(rl, COLOR_READY_FG, 0);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_10, 0);
    lv_obj_center(rl);

    // TUNING toggle button
    lv_obj_t *btn_tune = lv_btn_create(container_mehran);
    lv_obj_set_size(btn_tune, 62, 28);
    lv_obj_set_pos(btn_tune, 710, 10);
    lv_obj_set_style_bg_color(btn_tune, lv_color_hex(0x182436), 0);
    lv_obj_set_style_border_color(btn_tune, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_border_width(btn_tune, 1, 0);
    lv_obj_set_style_radius(btn_tune, 6, 0);
    lv_obj_add_event_cb(btn_tune, toggle_tuning_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tune_lbl = lv_label_create(btn_tune);
    lv_label_set_text(tune_lbl, "TUNING");
    lv_obj_set_style_text_color(tune_lbl, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(tune_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(tune_lbl);

    lbl_turn_r = lv_label_create(container_mehran);
    lv_label_set_text(lbl_turn_r, ">");
    lv_obj_set_style_text_color(lbl_turn_r, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_turn_r, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_turn_r, 778, 16);

    // ================= RIGHT: COMPACT SECOND DIAL (TWIN ARCS) =================
    // Compact housing for Battery & Motor Temp
    lv_obj_t *binnacle_right = lv_obj_create(container_mehran);
    lv_obj_set_size(binnacle_right, 320, 175);
    lv_obj_set_pos(binnacle_right, 472, 46);
    lv_obj_set_style_bg_color(binnacle_right, COLOR_DIAL_BG, 0);
    lv_obj_set_style_border_color(binnacle_right, COLOR_DIAL_BORDER, 0);
    lv_obj_set_style_border_width(binnacle_right, 2, 0);
    lv_obj_set_style_radius(binnacle_right, 14, 0);
    lv_obj_clear_flag(binnacle_right, LV_OBJ_FLAG_SCROLLABLE);

    // Left Arc: Battery (SOC) - Decreased by 2px
    meter_battery = lv_meter_create(binnacle_right);
    lv_obj_set_size(meter_battery, 136, 151);
    lv_obj_set_pos(meter_battery, 2, 14);
    lv_obj_set_style_bg_opa(meter_battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter_battery, 0, 0);

    lv_meter_scale_t *scale_bat = lv_meter_add_scale(meter_battery);
    lv_meter_set_scale_ticks(meter_battery, scale_bat, 15, 2, 5, lv_color_hex(0x475569));
    lv_meter_set_scale_range(meter_battery, scale_bat, 0, 100, 110, 145);

    indic_bat_needle = lv_meter_add_needle_line(meter_battery, scale_bat, 3, COLOR_NEEDLE_ORANGE, -12);
    lv_meter_set_indicator_value(meter_battery, indic_bat_needle, 0);

    lv_obj_t *lbl_bat_title = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_bat_title, "BATTERY (SOC)");
    lv_obj_set_style_text_color(lbl_bat_title, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_bat_title, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_bat_title, 14, 8);

    lv_obj_t *lbl_f = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_f, "F");
    lv_obj_set_style_text_color(lbl_f, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(lbl_f, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_f, 18, 30);

    lv_obj_t *lbl_e = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_e, "E");
    lv_obj_set_style_text_color(lbl_e, COLOR_TEMP_RED, 0);
    lv_obj_set_style_text_font(lbl_e, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_e, 18, 120);

    // Number placed cleanly on the SIDE of the SOC gauge (+5px font to 32pt)
    lbl_bat_pct = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_bat_pct, "75%");
    lv_obj_set_style_text_color(lbl_bat_pct, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(lbl_bat_pct, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(lbl_bat_pct, 74, 62);

    lv_obj_t *lbl_bat_sub = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_bat_sub, "LEVEL");
    lv_obj_set_style_text_color(lbl_bat_sub, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(lbl_bat_sub, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_bat_sub, 76, 100);

    // Vertical Divider Line in the middle of compact binnacle
    lv_obj_t *div_binnacle = lv_obj_create(binnacle_right);
    lv_obj_set_size(div_binnacle, 1, 128);
    lv_obj_set_pos(div_binnacle, 158, 22);
    lv_obj_set_style_bg_color(div_binnacle, lv_color_hex(0x1e293b), 0);
    lv_obj_set_style_border_width(div_binnacle, 0, 0);

    // Right Arc: Motor Temp - Decreased by 2px
    meter_temp = lv_meter_create(binnacle_right);
    lv_obj_set_size(meter_temp, 136, 151);
    lv_obj_set_pos(meter_temp, 178, 14);
    lv_obj_set_style_bg_opa(meter_temp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter_temp, 0, 0);

    lv_meter_scale_t *scale_tmp = lv_meter_add_scale(meter_temp);
    lv_meter_set_scale_ticks(meter_temp, scale_tmp, 15, 2, 5, lv_color_hex(0x475569));
    lv_meter_set_scale_range(meter_temp, scale_tmp, 0, 120, 110, 285);

    indic_temp_needle = lv_meter_add_needle_line(meter_temp, scale_tmp, 3, COLOR_NEEDLE_ORANGE, -12);
    lv_meter_set_indicator_value(meter_temp, indic_temp_needle, 0);

    lv_obj_t *lbl_tmp_title = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_tmp_title, "MOTOR TEMP");
    lv_obj_set_style_text_color(lbl_tmp_title, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_tmp_title, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_tmp_title, 214, 8);

    lv_obj_t *lbl_h = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_h, "H");
    lv_obj_set_style_text_color(lbl_h, COLOR_TEMP_RED, 0);
    lv_obj_set_style_text_font(lbl_h, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_h, 286, 30);

    lv_obj_t *lbl_c = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_c, "C");
    lv_obj_set_style_text_color(lbl_c, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_c, 286, 120);

    // Number placed cleanly on the SIDE of the Motor Temp gauge (+5px font to 32pt)
    lbl_temp_val = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_temp_val, "42°C");
    lv_obj_set_style_text_color(lbl_temp_val, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_temp_val, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(lbl_temp_val, 174, 62);

    lv_obj_t *lbl_tmp_sub = lv_label_create(binnacle_right);
    lv_label_set_text(lbl_tmp_sub, "TEMP");
    lv_obj_set_style_text_color(lbl_tmp_sub, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(lbl_tmp_sub, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_tmp_sub, 176, 100);

    // ================= RIGHT: 2 ROWS OF TELEMETRY READINGS =================
    // Width available: 320px (from X=472 to 792). Height available: Y=230 to 468.
    int card_w = 98;
    int card_h = 112;
    int row1_y = 230;
    int row2_y = 352;
    int col_x[3] = {472, 583, 694};

    // ----- ROW 1, CARD 1: POWER (kW) -----
    lv_obj_t *c_pwr = lv_obj_create(container_mehran);
    lv_obj_set_size(c_pwr, card_w, card_h);
    lv_obj_set_pos(c_pwr, col_x[0], row1_y);
    lv_obj_set_style_bg_color(c_pwr, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_pwr, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_pwr, 1, 0);
    lv_obj_set_style_radius(c_pwr, 8, 0);
    lv_obj_set_style_pad_all(c_pwr, 4, 0);
    lv_obj_clear_flag(c_pwr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lp = lv_label_create(c_pwr);
    lv_label_set_text(lp, "POWER");
    lv_obj_set_style_text_color(lp, COLOR_POWER_ORANGE, 0);
    lv_obj_set_style_text_font(lp, &lv_font_montserrat_10, 0);
    lv_obj_align(lp, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_power = lv_label_create(c_pwr);
    lv_label_set_text(lbl_card_power, "+18.6");
    lv_obj_set_style_text_color(lbl_card_power, COLOR_POWER_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_card_power, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_power, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_p = lv_label_create(c_pwr);
    lv_label_set_text(sub_p, "kW");
    lv_obj_set_style_text_color(sub_p, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sub_p, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_p, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_p = lv_obj_create(c_pwr);
    lv_obj_set_size(bar_p, 74, 3);
    lv_obj_align(bar_p, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_p, COLOR_POWER_ORANGE, 0);
    lv_obj_set_style_border_width(bar_p, 0, 0);

    // ----- ROW 1, CARD 2: CURRENT (A) -----
    lv_obj_t *c_cur = lv_obj_create(container_mehran);
    lv_obj_set_size(c_cur, card_w, card_h);
    lv_obj_set_pos(c_cur, col_x[1], row1_y);
    lv_obj_set_style_bg_color(c_cur, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_cur, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_cur, 1, 0);
    lv_obj_set_style_radius(c_cur, 8, 0);
    lv_obj_set_style_pad_all(c_cur, 4, 0);
    lv_obj_clear_flag(c_cur, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lc = lv_label_create(c_cur);
    lv_label_set_text(lc, "CURRENT");
    lv_obj_set_style_text_color(lc, lv_color_hex(0x67e8f9), 0);
    lv_obj_set_style_text_font(lc, &lv_font_montserrat_10, 0);
    lv_obj_align(lc, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_current = lv_label_create(c_cur);
    lv_label_set_text(lbl_card_current, "159.4");
    lv_obj_set_style_text_color(lbl_card_current, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_card_current, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_current, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_c = lv_label_create(c_cur);
    lv_label_set_text(sub_c, "A");
    lv_obj_set_style_text_color(sub_c, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sub_c, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_c, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_c = lv_obj_create(c_cur);
    lv_obj_set_size(bar_c, 74, 3);
    lv_obj_align(bar_c, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_c, lv_color_hex(0x67e8f9), 0);
    lv_obj_set_style_border_width(bar_c, 0, 0);

    // ----- ROW 1, CARD 3: PACK VOLTS (V) -----
    lv_obj_t *c_vlt = lv_obj_create(container_mehran);
    lv_obj_set_size(c_vlt, card_w, card_h);
    lv_obj_set_pos(c_vlt, col_x[2], row1_y);
    lv_obj_set_style_bg_color(c_vlt, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_vlt, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_vlt, 1, 0);
    lv_obj_set_style_radius(c_vlt, 8, 0);
    lv_obj_set_style_pad_all(c_vlt, 4, 0);
    lv_obj_clear_flag(c_vlt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lvlt = lv_label_create(c_vlt);
    lv_label_set_text(lvlt, "VOLTS");
    lv_obj_set_style_text_color(lvlt, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_text_font(lvlt, &lv_font_montserrat_10, 0);
    lv_obj_align(lvlt, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_volts = lv_label_create(c_vlt);
    lv_label_set_text(lbl_card_volts, "116.5");
    lv_obj_set_style_text_color(lbl_card_volts, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_card_volts, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_volts, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_v = lv_label_create(c_vlt);
    lv_label_set_text(sub_v, "V");
    lv_obj_set_style_text_color(sub_v, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sub_v, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_v, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_v = lv_obj_create(c_vlt);
    lv_obj_set_size(bar_v, 74, 3);
    lv_obj_align(bar_v, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_v, COLOR_BAT_CYAN, 0);
    lv_obj_set_style_border_width(bar_v, 0, 0);

    // ----- ROW 2, CARD 1: AVG EFFICIENCY -----
    lv_obj_t *c_eff = lv_obj_create(container_mehran);
    lv_obj_set_size(c_eff, card_w, card_h);
    lv_obj_set_pos(c_eff, col_x[0], row2_y);
    lv_obj_set_style_bg_color(c_eff, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_eff, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_eff, 1, 0);
    lv_obj_set_style_radius(c_eff, 8, 0);
    lv_obj_set_style_pad_all(c_eff, 4, 0);
    lv_obj_clear_flag(c_eff, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *le = lv_label_create(c_eff);
    lv_label_set_text(le, "EFFICIENCY");
    lv_obj_set_style_text_color(le, COLOR_EFF_GREEN, 0);
    lv_obj_set_style_text_font(le, &lv_font_montserrat_10, 0);
    lv_obj_align(le, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_eff = lv_label_create(c_eff);
    lv_label_set_text(lbl_card_eff, "16");
    lv_obj_set_style_text_color(lbl_card_eff, COLOR_EFF_GREEN, 0);
    lv_obj_set_style_text_font(lbl_card_eff, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_eff, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_e = lv_label_create(c_eff);
    lv_label_set_text(sub_e, "Wh / km");
    lv_obj_set_style_text_color(sub_e, COLOR_EFF_GREEN, 0);
    lv_obj_set_style_text_font(sub_e, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_e, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_e = lv_obj_create(c_eff);
    lv_obj_set_size(bar_e, 74, 3);
    lv_obj_align(bar_e, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_e, COLOR_EFF_GREEN, 0);
    lv_obj_set_style_border_width(bar_e, 0, 0);

    // ----- ROW 2, CARD 2: 12V AUX -----
    lv_obj_t *c_aux = lv_obj_create(container_mehran);
    lv_obj_set_size(c_aux, card_w, card_h);
    lv_obj_set_pos(c_aux, col_x[1], row2_y);
    lv_obj_set_style_bg_color(c_aux, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_aux, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_aux, 1, 0);
    lv_obj_set_style_radius(c_aux, 8, 0);
    lv_obj_set_style_pad_all(c_aux, 4, 0);
    lv_obj_clear_flag(c_aux, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *la = lv_label_create(c_aux);
    lv_label_set_text(la, "12V AUX");
    lv_obj_set_style_text_color(la, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(la, &lv_font_montserrat_10, 0);
    lv_obj_align(la, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_aux = lv_label_create(c_aux);
    lv_label_set_text(lbl_card_aux, "13.8");
    lv_obj_set_style_text_color(lbl_card_aux, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(lbl_card_aux, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_aux, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_a = lv_label_create(c_aux);
    lv_label_set_text(sub_a, "V (DC-DC)");
    lv_obj_set_style_text_color(sub_a, COLOR_AUX_GREEN, 0);
    lv_obj_set_style_text_font(sub_a, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_a, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_a = lv_obj_create(c_aux);
    lv_obj_set_size(bar_a, 74, 3);
    lv_obj_align(bar_a, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_a, COLOR_AUX_GREEN, 0);
    lv_obj_set_style_border_width(bar_a, 0, 0);

    // ----- ROW 2, CARD 3: EST RANGE -----
    lv_obj_t *c_rng = lv_obj_create(container_mehran);
    lv_obj_set_size(c_rng, card_w, card_h);
    lv_obj_set_pos(c_rng, col_x[2], row2_y);
    lv_obj_set_style_bg_color(c_rng, lv_color_hex(0x0e141f), 0);
    lv_obj_set_style_border_color(c_rng, COLOR_CLUSTER_BORDER, 0);
    lv_obj_set_style_border_width(c_rng, 1, 0);
    lv_obj_set_style_radius(c_rng, 8, 0);
    lv_obj_set_style_pad_all(c_rng, 4, 0);
    lv_obj_clear_flag(c_rng, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lr = lv_label_create(c_rng);
    lv_label_set_text(lr, "EST RANGE");
    lv_obj_set_style_text_color(lr, lv_color_hex(0xa78bfa), 0);
    lv_obj_set_style_text_font(lr, &lv_font_montserrat_10, 0);
    lv_obj_align(lr, LV_ALIGN_TOP_MID, 0, 4);

    lbl_card_range = lv_label_create(c_rng);
    lv_label_set_text(lbl_card_range, "128");
    lv_obj_set_style_text_color(lbl_card_range, lv_color_hex(0xa78bfa), 0);
    lv_obj_set_style_text_font(lbl_card_range, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_card_range, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *sub_r = lv_label_create(c_rng);
    lv_label_set_text(sub_r, "km");
    lv_obj_set_style_text_color(sub_r, COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sub_r, &lv_font_montserrat_10, 0);
    lv_obj_align(sub_r, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t *bar_r = lv_obj_create(c_rng);
    lv_obj_set_size(bar_r, 74, 3);
    lv_obj_align(bar_r, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(bar_r, lv_color_hex(0xa78bfa), 0);
    lv_obj_set_style_border_width(bar_r, 0, 0);
}

// ----------------- BUILD OPENINVERTER FULL VIEW -----------------
static void toggle_sleep_mode() {
    setBacklight(false);
}

static void create_openinverter_ui(lv_obj_t *parent) {
    container_oi = create_openinverter_full_ui(parent, toggle_dashboard_mode, toggle_sleep_mode);
}

// ----------------- Wi-Fi Web Server -----------------
void setupWebServer() {
    WiFi.softAP("MehranEV-OpenInverter", "");

    server.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head><title>Suzuki Mehran EV</title></head>";
        html += "<body style='background:#0a0c10;color:#f8fafc;font-family:sans-serif;padding:24px;'>";
        html += "<h2 style='color:#d97706;'>Suzuki Mehran EV Dashboard Online</h2>";
        html += "<p>Hardware: Waveshare 4.3\" S3 Touch Screen (Twin Binnacle Dials)</p>";
        html += "<h3>Live Telemetry:</h3><ul>";
        html += "<li>Speed: " + String(current_speed_kmh) + " km/h</li>";
        html += "<li>Battery: " + String(battery_soc) + " % (" + String(pack_volts) + " V)</li>";
        html += "<li>Motor Temp: " + String(motor_temp) + " &deg;C</li>";
        html += "<li>Power: " + String(power_kw) + " kW (" + String(pack_current) + " A)</li>";
        html += "</ul></body></html>";
        server.send(200, "text/html", html);
    });

    server.begin();
}

// ----------------- Main Setup & Loop -----------------
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n--- Starting Suzuki Mehran EV Luxury Cluster with Boot Sequence ---");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    resetTouchHardware();
    setBacklight(true);
    scanI2C();
    initLcd();

    lv_init();

    buf_1 = (lv_color_t *)heap_caps_malloc(LCD_H_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, LCD_H_RES * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_APP_BG, 0);

    // Build the Screens
    create_boot_screen(scr);
    create_mehran_cluster(scr);
    create_openinverter_ui(scr);

    boot_state = BOOT_LOGO;
    boot_start_ms = millis();

    setupWebServer();

    Serial.println("Suzuki Mehran EV Boot Animation Started!");
}

static unsigned long last_telemetry_tick = 0;

void loop() {
    server.handleClient();
    lv_timer_handler();

    unsigned long now = millis();
    unsigned long boot_elapsed = now - boot_start_ms;

    // ================= STARTUP ANIMATION STATE MACHINE =================
    if (boot_state == BOOT_LOGO) {
        int progress = (int)((boot_elapsed * 100) / 2400);
        if (progress > 100) progress = 100;
        lv_bar_set_value(bar_boot_progress, progress, LV_ANIM_OFF);

        if (boot_elapsed > 1200) {
            lv_label_set_text(lbl_boot_status, "CAN BUS 500k SYNCHRONIZED - INVERTER READY");
        }

        if (boot_elapsed >= 2500) {
            // Hide boot screen, show Mehran cluster, start hypercar needle sweep!
            lv_obj_add_flag(container_boot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(container_mehran, LV_OBJ_FLAG_HIDDEN);
            boot_state = BOOT_SWEEP_UP;
            boot_start_ms = now;
        }
        delay(2);
        return;
    }
    else if (boot_state == BOOT_SWEEP_UP) {
        float t = (float)boot_elapsed / 700.0f; // 700ms sweep up
        if (t >= 1.0f) {
            t = 1.0f;
            boot_state = BOOT_SWEEP_DOWN;
            boot_start_ms = now;
        }
        // Sweep to maximum
        int sp_val = (int)(140.0f * t);
        int rpm_sweep = (int)(7000.0f * t);
        lv_meter_set_indicator_value(meter_speed, indic_speed_needle, sp_val);
        lv_meter_set_indicator_value(meter_battery, indic_bat_needle, (int)(100.0f * t));
        lv_meter_set_indicator_value(meter_temp, indic_temp_needle, (int)(120.0f * t));
        if (lbl_speed_digital) {
            char sbuf[16];
            sprintf(sbuf, "%d", sp_val);
            lv_label_set_text(lbl_speed_digital, sbuf);
        }
        if (lbl_rpm_val) {
            char rbuf[16];
            sprintf(rbuf, "%d RPM", rpm_sweep);
            lv_label_set_text(lbl_rpm_val, rbuf);
        }
        delay(2);
        return;
    }
    else if (boot_state == BOOT_SWEEP_DOWN) {
        float t = (float)boot_elapsed / 700.0f; // 700ms sweep down
        if (t >= 1.0f) {
            t = 1.0f;
            boot_state = BOOT_RUNNING;
        }
        // Settle from max down to live vehicle levels
        int sp_val = (int)(140.0f - (140.0f - current_speed_kmh) * t);
        int rpm_sweep = (int)(7000.0f - (7000.0f - motor_rpm) * t);
        lv_meter_set_indicator_value(meter_speed, indic_speed_needle, sp_val);
        lv_meter_set_indicator_value(meter_battery, indic_bat_needle, (int)(100.0f - (100.0f - battery_soc) * t));
        lv_meter_set_indicator_value(meter_temp, indic_temp_needle, (int)(120.0f - (120.0f - motor_temp) * t));
        if (lbl_speed_digital) {
            char sbuf[16];
            sprintf(sbuf, "%d", sp_val);
            lv_label_set_text(lbl_speed_digital, sbuf);
        }
        if (lbl_rpm_val) {
            char rbuf[16];
            sprintf(rbuf, "%d RPM", rpm_sweep);
            lv_label_set_text(lbl_rpm_val, rbuf);
        }
        delay(2);
        return;
    }

    // ================= NORMAL DRIVING TELEMETRY (15 FPS) =================
    if (now - last_telemetry_tick >= 65) {
        last_telemetry_tick = now;
        sim_time += 0.05f;

        if (active_gear == 3) { // D (Drive)
            current_speed_kmh = 42.0f + 36.0f * sin(sim_time);
            if (current_speed_kmh < 0) current_speed_kmh = 0;
            power_kw = 1.1f + (current_speed_kmh * 0.22f) + 4.0f * sin(sim_time * 2.0f);
            pack_current = abs(power_kw * 1000.0f / pack_volts);
            odometer_km += (current_speed_kmh / 3600.0) * 0.065;
            motor_rpm = current_speed_kmh * 48.0f + 150.0f;
        } else if (active_gear == 1) { // R (Reverse)
            current_speed_kmh = 12.0f + 6.0f * sin(sim_time);
            power_kw = 2.5f;
            pack_current = 21.0f;
            odometer_km += (current_speed_kmh / 3600.0) * 0.065;
            motor_rpm = current_speed_kmh * 48.0f + 100.0f;
        } else { // P or N
            current_speed_kmh = 0.0f;
            power_kw = 0.2f;
            pack_current = 1.7f;
            motor_rpm = 0.0f;
        }

        if (motor_rpm > 7000.0f) motor_rpm = 7000.0f;
        battery_soc = 84.0f - (float)((int)(sim_time * 0.1f) % 15);
        pack_volts = 112.0f + (battery_soc * 0.06f);
        motor_temp = 40.0f + 3.0f * sin(sim_time * 0.15f);

        if (show_mehran_cluster) {
            lv_meter_set_indicator_value(meter_speed, indic_speed_needle, (int)current_speed_kmh);

            if (lbl_speed_digital) {
                char sbuf[16];
                sprintf(sbuf, "%d", (int)current_speed_kmh);
                lv_label_set_text(lbl_speed_digital, sbuf);
            }

            if (lbl_rpm_val) {
                char rbuf[16];
                sprintf(rbuf, "%d RPM", (int)motor_rpm);
                lv_label_set_text(lbl_rpm_val, rbuf);
            }

            long odo_int = (long)(odometer_km * 10.0);
            char odo_str[16];
            sprintf(odo_str, "%06ld", odo_int % 1000000L);
            for (int d = 0; d < 6; d++) {
                char ch[2] = {odo_str[d], '\0'};
                lv_label_set_text(lbl_odo_digits[d], ch);
            }

            lv_meter_set_indicator_value(meter_battery, indic_bat_needle, (int)battery_soc);
            char b_buf[16];
            sprintf(b_buf, "%d%%", (int)battery_soc);
            lv_label_set_text(lbl_bat_pct, b_buf);

            lv_meter_set_indicator_value(meter_temp, indic_temp_needle, (int)motor_temp);
            char t_buf[16];
            sprintf(t_buf, "%d°C", (int)motor_temp);
            lv_label_set_text(lbl_temp_val, t_buf);

            char v_buf[16];
            sprintf(v_buf, "%.1f", pack_volts);
            lv_label_set_text(lbl_card_volts, v_buf);

            char i_buf[16];
            sprintf(i_buf, "%.1f", pack_current);
            lv_label_set_text(lbl_card_current, i_buf);

            char p_buf[16];
            sprintf(p_buf, "%+.1f", power_kw);
            lv_label_set_text(lbl_card_power, p_buf);

            char e_buf[16];
            sprintf(e_buf, "%d", (int)avg_efficiency);
            lv_label_set_text(lbl_card_eff, e_buf);

            char a_buf[16];
            sprintf(a_buf, "%.1f", aux_12v);
            lv_label_set_text(lbl_card_aux, a_buf);

            if (lbl_card_range) {
                char r_buf[16];
                sprintf(r_buf, "%d", (int)(battery_soc * 1.55f));
                lv_label_set_text(lbl_card_range, r_buf);
            }
        }

        // Keep OpenInverter Web Interface synchronized in real-time
        InverterTelemetry telem;
        telem.speed_kmh = current_speed_kmh;
        telem.motor_rpm = motor_rpm;
        telem.pack_volts = pack_volts;
        telem.pack_current = pack_current;
        telem.power_kw = power_kw;
        telem.battery_soc = battery_soc;
        telem.motor_temp = motor_temp;
        telem.heatsink_temp = motor_temp - 6.5f;
        telem.aux_12v = aux_12v;
        telem.opmode = (active_gear == 3) ? 1 : 0;
        telem.status_str = "RUNNING";
        telem.lasterr_str = "NONE (0)";
        telem.din_fwd = (active_gear == 3);
        telem.din_rev = (active_gear == 1);
        telem.din_brake = (active_gear == 0);
        telem.active_gear = active_gear;
        update_openinverter_telemetry(telem);
    }

    delay(2);
}
