# OpenInverter LVGL Web Interface & EV Instrument Cluster

An embedded, hardware-accelerated **LVGL (v8.3.11)** port of the **OpenInverter Web Interface** (based on [`sperate/esp32-web-interface-mod`](https://github.com/sperate/esp32-web-interface-mod) & [`wjcloudy/esp32-web-interface`](https://github.com/wjcloudy/esp32-web-interface)) for the **Waveshare ESP32-S3-Touch-LCD-4.3**.

Features a dual-mode interface: a **Suzuki Mehran EV Hypercar Instrument Cluster** and a complete **OpenInverter Web Interface Dashboard**, with real-time bidirectional telemetry streaming over CAN bus (500 kbps) and UART.

---

## ⚡ Hardware Platform

* **Microcontroller**: ESP32-S3 (Xtensa® Dual-Core 32-bit LX7 @ 240 MHz)
* **Memory**: 16MB Quad-SPI Flash, 8MB Octal-SPI PSRAM
* **Display**: Waveshare 4.3" 800×480 RGB 16-bit Parallel LCD (12 MHz PCLK)
* **Touch Controller**: Goodix GT911 Capacitive Multi-Touch (I2C `0x5D`)
* **IO Expander**: CH422G (LCD Backlight & Touch Reset control)
* **CAN Transceiver**: On-board TWAI controller @ 500 kbps

---

## 🚀 OpenInverter LVGL UI Features

The interface matches the obsidian dark glassmorphism design system of the OpenInverter web UI:

1. **📊 Dashboard**:
   - Inverter status hero card (`RUN`, `STOP`, `MANUAL`) with operating mode & CAN node link.
   - 4-metric real-time gauge cards with colored trend bars (`Udc`, `Idc`, `Speed`, `Power`).
   - Interactive command console terminal with preset command triggers (`get udc`, `get speed`, `status`, `start 2`, `stop`).
   - Direct CAN frame injection panel (CAN ID + 8 data bytes).
2. **⚙️ Parameters**:
   - Categorized parameter management tree (`MOTOR`, `INVERTER`, `THROTTLE`, `LIMITS`).
   - Real-time interactive sliders and `[-]` / `[+]` stepping buttons.
   - Non-volatile STM32 flash commit (`SAVE TO FLASH`) with animated toast feedback.
3. **👁️ Spot Values**:
   - 4-column × 8-row live telemetry monitoring matrix (speed, bus voltage, current, power, temperatures, digital inputs).
4. **📈 Live Plot (Oscilloscope)**:
   - High-speed multi-series real-time rolling line chart (`Speed`, `Power`).
   - Pause / Resume controls and buffer clear.
5. **🧭 Gauges**:
   - Radial analog speedometer (0–140 km/h) with orange needle and cyan glow arc.
   - Radial power gauge (-50 kW Regen to +150 kW Boost).
   - Traction pack State-of-Charge (SOC) bar & motor temperature bar.
   - Drive direction indicators (`FWD`, `REV`, `BRAKE`).
6. **📝 Data Logger**:
   - Live CSV logger state, record counter, capture rate selector (10–100 Hz), and CSV stream preview.
7. **🔄 CAN Tools**:
   - CAN bus traffic monitor & packet transmission activity log.
8. **⚙️ Settings & System Diagnostics**:
   - Backlight brightness slider & display sleep with wake-on-touch.
   - Wi-Fi AP info (`MehranEV-OpenInverter` @ `192.168.4.1`).
   - Hardware stats (CPU, PSRAM, Heap, Flash).

---

## 🏎️ Dual Mode: EV Instrument Cluster

Tap `< MEHRAN EV` or `TUNING` to toggle between the OpenInverter Web Interface and the custom Mehran EV twin-binnacle instrument cluster, featuring:
- Dial needle sweep startup animation.
- Analog Speedometer and RPM tachometer.
- 6-digit mechanical tumbler odometer.
- Digital gear selector (`P`, `R`, `N`, `D`).

---

## 🛠️ Building & Flashing

This project is built using [PlatformIO](https://platformio.org/):

```bash
# Navigate to the firmware directory
cd waveshare-standalone-dashboard

# Build the firmware
pio run

# Flash to the Waveshare ESP32-S3 over USB
pio run -t upload --upload-port COM6

# Monitor serial debug output
pio device-monitor -b 115200
```

---

## 🖥️ Desktop Browser Simulator

You can test the Waveshare 4.3" screen design in a web browser using the included simulator:

```bash
# Start the local simulation server
node sim-server.mjs

# Open waveshare-simulator.html in any browser
```
