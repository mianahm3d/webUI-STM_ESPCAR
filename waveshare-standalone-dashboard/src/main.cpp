#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <lvgl.h>
#include "openinverter_ui.h"
#include "can_vcu.h"

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

static esp_lcd_panel_handle_t panel_handle = NULL;
static WebServer server(80);
static bool backlight_state = true;

// LVGL Draw Buffer (40 lines in internal DMA RAM)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf_1 = NULL;

// Inverter Telemetry state (Pure CAN - strictly updated from incoming frames)
static InverterTelemetry live_telem = {};

// ----------------- Hardware Drivers -----------------
void setBacklight(bool on) {
    backlight_state = on;
    Wire.beginTransmission(CH422G_REG_MODE);
    Wire.write(0x01);
    Wire.endTransmission();

    Wire.beginTransmission(CH422G_REG_OUT);
    // Bit 5 (0x20) is USB_SEL: HIGH routes GPIO 19 & 20 to onboard CAN Transceiver (TJA1051)
    uint8_t out = on ? 0x3F : 0x3B;
    Wire.write(out);
    Wire.endTransmission();
}

void setBacklightBrightness(uint8_t val) {
    // CH422G provides discrete backlight enable; 0 = off, >0 = on
    setBacklight(val > 10);
}

void toggleSleepMode() {
    setBacklight(!backlight_state);
}

void resetTouchHardware() {
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW); // Latch address 0x5D
    delay(20);

    Wire.beginTransmission(CH422G_REG_MODE);
    Wire.write(0x01);
    Wire.endTransmission();

    Wire.beginTransmission(CH422G_REG_OUT);
    Wire.write(0x3D); // TP_RST LOW + USB_SEL HIGH (CAN Enabled)
    Wire.endTransmission();
    delay(100);

    Wire.beginTransmission(CH422G_REG_OUT);
    Wire.write(0x3F); // TP_RST HIGH + USB_SEL HIGH (CAN Enabled)
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
        .flags = {
            .fb_in_psram = 1,
        }
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_p);
    lv_disp_flush_ready(disp_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    int touchX = 0, touchY = 0;
    if (getTouch(touchX, touchY)) {
        if (touchX >= 0 && touchX < LCD_H_RES && touchY >= 0 && touchY < LCD_V_RES) {
            data->point.x = touchX;
            data->point.y = touchY;
            data->state = LV_INDEV_STATE_PR;
            return;
        }
    }
    data->state = LV_INDEV_STATE_REL;
}

// ----------------- Wi-Fi Web Server & REST API -----------------
void setupWebServer() {
    WiFi.softAP("OpenInverter-WiFi", "");

    server.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head><title>OpenInverter Web Interface</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{background:#07090e;color:#e8ecf1;font-family:sans-serif;padding:24px;}";
        html += ".card{background:#18202d;padding:16px;border-radius:10px;margin-bottom:12px;border:1px solid #263040;}";
        html += ".val{color:#4cc9f0;font-weight:bold;font-size:1.4rem;}</style></head><body>";
        html += "<h2>OpenInverter v4.1 - Waveshare Standalone Display</h2>";
        html += "<div class='card'>Status: " + String(is_vcu_connected() ? "<span style='color:#54e6a4;'>CAN LIVE</span>" : "<span style='color:#ff6b6b;'>OFFLINE</span>") + "</div>";
        html += "<div class='card'><div>DC Bus Voltage (udc):</div><div class='val'>" + String(live_telem.pack_volts) + " V</div></div>";
        html += "<div class='card'><div>DC Bus Current (idc):</div><div class='val'>" + String(live_telem.pack_current) + " A</div></div>";
        html += "<div class='card'><div>Motor Speed (rpm):</div><div class='val'>" + String(live_telem.motor_rpm) + " RPM</div></div>";
        html += "<div class='card'><div>Power:</div><div class='val'>" + String(live_telem.power_kw) + " kW</div></div>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    });

    server.on("/can-send", HTTP_GET, []() {
        if (!server.hasArg("canId") || !server.hasArg("data")) {
            server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"missing canId or data\"}");
            return;
        }
        uint32_t id = strtoul(server.arg("canId").c_str(), NULL, 16);
        String data_str = server.arg("data");
        uint8_t payload[8] = {0};
        int bidx = 0;
        char temp[64];
        strncpy(temp, data_str.c_str(), sizeof(temp));
        char *token = strtok(temp, " ,");
        while (token && bidx < 8) {
            payload[bidx++] = (uint8_t)strtoul(token, NULL, 16);
            token = strtok(NULL, " ,");
        }
        bool ok = send_can_frame(id, payload, bidx);
        server.send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"failed\"}");
    });

    server.on("/reset-inverter", HTTP_GET, []() {
        uint8_t cmd[8] = {0x23, 0x00, 0x20, 0xFF, 0x01, 0x00, 0x00, 0x00};
        send_can_frame(0x601, cmd, 8);
        server.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"reset-inverter\"}");
    });

    server.begin();
    Serial.println("[HTTP] Web Server started at http://192.168.4.1/");
}

// ----------------- Main Setup & Loop -----------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=======================================================");
    Serial.println("  OpenInverter Web Interface v4.1 - Waveshare 4.3\"");
    Serial.println("  Hardware: ESP32-S3 + GT911 Touch + TJA1051 CAN");
    Serial.println("  CAN Bus: 500 kbps (GPIO 20 TX, GPIO 19 RX)");
    Serial.println("=======================================================\n");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    resetTouchHardware();
    setBacklight(true);
    scanI2C();
    initLcd();
    init_vcu_can();

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

    // Boot directly into OpenInverter Web Interface (Exact replica of wjcloudy/esp32-web-interface)
    lv_obj_t *scr = lv_scr_act();
    create_openinverter_full_ui(scr, toggleSleepMode, setBacklightBrightness);

    setupWebServer();
    Serial.println("[SYSTEM] Boot complete. OpenInverter UI Active.");
}

static unsigned long last_telemetry_tick = 0;

void loop() {
    server.handleClient();
    lv_timer_handler();

    unsigned long now = millis();

    // Poll incoming CAN messages from ZombieVerter VCU / OpenInverter
    poll_vcu_can(live_telem);
    live_telem.can_connected = is_vcu_connected();

    if (now - last_telemetry_tick >= 50) {
        last_telemetry_tick = now;
        // Feed live CAN telemetry into OpenInverter UI (Dashboard, Spot Values, Gauges, Plot, Logger)
        update_openinverter_telemetry(live_telem);
    }

    delay(2);
}
