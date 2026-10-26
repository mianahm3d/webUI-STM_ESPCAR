#ifndef OPENINVERTER_UI_H
#define OPENINVERTER_UI_H

#include <Arduino.h>
#include <lvgl.h>

#define NUM_OI_TABS 11

// OpenInverter Dark Theme Color Palette (wjcloudy/esp32-web-interface v4.1 glassmorphism)
#define OI_COLOR_BG             lv_color_hex(0x07090e)
#define OI_COLOR_SURFACE        lv_color_hex(0x10151d)
#define OI_COLOR_SURFACE_HOVER  lv_color_hex(0x151b25)
#define OI_COLOR_CARD           lv_color_hex(0x18202d)
#define OI_COLOR_BORDER         lv_color_hex(0x263040)
#define OI_COLOR_TEXT_MAIN      lv_color_hex(0xe8ecf1)
#define OI_COLOR_TEXT_MUTED     lv_color_hex(0x9aa3b2)
#define OI_COLOR_TEXT_SUBTLE    lv_color_hex(0x5d6675)
#define OI_COLOR_CYAN           lv_color_hex(0x4cc9f0)
#define OI_COLOR_GREEN          lv_color_hex(0x54e6a4)
#define OI_COLOR_AMBER          lv_color_hex(0xffb454)
#define OI_COLOR_RED            lv_color_hex(0xff6b6b)

// Callback type for screen sleep / backlight toggle
typedef void (*OiSleepCb)(void);
typedef void (*OiBrightnessCb)(uint8_t val);

// Inverter live telemetry struct passed to the UI
struct InverterTelemetry {
    float speed_kmh;
    float motor_rpm;
    float pack_volts;     // udc
    float pack_current;   // idc
    float power_kw;
    float battery_soc;
    float motor_temp;     // tmot
    float heatsink_temp;  // ths
    float aux_12v;
    int   opmode;         // 0=Off, 1=Manual, 2=Auto, 3=Boost
    const char *status_str;
    const char *lasterr_str;
    int   din_fwd;
    int   din_rev;
    int   din_brake;
    int   active_gear;    // 0=P, 1=R, 2=N, 3=D
    bool  can_connected;
};

// UI Initialization and update functions
lv_obj_t* create_openinverter_full_ui(lv_obj_t *parent, OiSleepCb sleep_cb, OiBrightnessCb bright_cb);
void update_openinverter_telemetry(const InverterTelemetry &telem);
void switch_oi_tab_view(int tab_idx);

#endif // OPENINVERTER_UI_H
