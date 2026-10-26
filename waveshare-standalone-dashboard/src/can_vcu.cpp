#include "can_vcu.h"

uint32_t can_rx_packet_count = 0;
uint32_t can_tx_packet_count = 0;
static bool twai_installed = false;
static unsigned long last_can_rx_time = 0;

// Default OpenInverter CANopen Node ID is 1
static const uint8_t VCU_NODE_ID = 1;

bool init_vcu_can() {
    if (twai_installed) {
        twai_stop();
        twai_driver_uninstall();
        twai_installed = false;
    }

    twai_general_config_t g_config = {
        .mode = TWAI_MODE_NORMAL,
        .tx_io = VCU_CAN_TX_PIN,
        .rx_io = VCU_CAN_RX_PIN,
        .clkout_io = TWAI_IO_UNUSED,
        .bus_off_io = TWAI_IO_UNUSED,
        .tx_queue_len = 32,
        .rx_queue_len = 64,
        .alerts_enabled = TWAI_ALERT_NONE,
        .clkout_divider = 0,
        .intr_flags = 0
    };

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        Serial.printf("[CAN] TWAI install failed: 0x%x\n", err);
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] TWAI start failed: 0x%x\n", err);
        twai_driver_uninstall();
        return false;
    }

    twai_installed = true;
    Serial.println("[CAN] TWAI 500 kbps started on GPIO 20 (TX) and GPIO 19 (RX)");
    return true;
}

bool is_vcu_connected() {
    if (last_can_rx_time == 0) return false;
    return (millis() - last_can_rx_time) < VCU_CAN_TIMEOUT_MS;
}

unsigned long get_last_can_rx_ms() {
    return last_can_rx_time;
}

bool send_can_frame(uint32_t id, const uint8_t *data, uint8_t len) {
    if (!twai_installed) return false;
    if (len > 8) len = 8;

    twai_message_t msg = {};
    msg.identifier = id;
    msg.data_length_code = len;
    msg.extd = (id > 0x7FF) ? 1 : 0;
    msg.rtr = 0;
    if (data && len > 0) {
        memcpy(msg.data, data, len);
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err == ESP_OK) {
        can_tx_packet_count++;
        return true;
    }
    return false;
}

// Request SDO read for parameter from VCU
void request_vcu_telemetry_sdo() {
    if (!twai_installed) return;
    // SDO Read: NodeId 1 -> Request ID 0x601
    // Index 0x2000 (params), subIndex
    uint8_t req[8] = {0x40, 0x00, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00};
    send_can_frame(0x600 | VCU_NODE_ID, req, 8);
}

bool poll_vcu_can(InverterTelemetry &telem) {
    if (!twai_installed) return false;

    twai_message_t frame;
    bool got_message = false;

    // Drain all available messages from TWAI buffer (up to 32 per tick)
    int max_frames = 32;
    while (max_frames-- > 0 && twai_receive(&frame, 0) == ESP_OK) {
        got_message = true;
        can_rx_packet_count++;
        last_can_rx_time = millis();

        uint32_t id = frame.identifier;
        uint8_t *d = frame.data;
        uint8_t len = frame.data_length_code;

        // 1. OpenInverter TPDO1 (Default: 0x181 or 0x180 + node)
        if (id == (0x180 | VCU_NODE_ID) || id == 0x181) {
            if (len >= 2) {
                int16_t raw_rpm = (int16_t)(d[0] | (d[1] << 8));
                telem.motor_rpm = (float)raw_rpm;
                // Calculate road speed from motor RPM: Suzuki Mehran EV ~48 RPM per km/h
                float spd = abs((float)raw_rpm) / 48.0f;
                telem.speed_kmh = spd;
            }
            if (len >= 4) {
                int16_t pwr_raw = (int16_t)(d[2] | (d[3] << 8));
                telem.power_kw = (float)pwr_raw * 0.1f;
            }
            if (len >= 6) {
                uint16_t v_raw = (uint16_t)(d[4] | (d[5] << 8));
                telem.pack_volts = (v_raw > 1000) ? (v_raw * 0.1f) : (float)v_raw;
                // Estimate SOC from pack voltage if pack voltage is in range 90-130V or 300-400V
                if (telem.pack_volts > 90.0f && telem.pack_volts < 135.0f) {
                    telem.battery_soc = (telem.pack_volts - 96.0f) * 100.0f / (126.0f - 96.0f);
                } else if (telem.pack_volts >= 300.0f && telem.pack_volts <= 420.0f) {
                    telem.battery_soc = (telem.pack_volts - 320.0f) * 100.0f / (410.0f - 320.0f);
                }
                if (telem.battery_soc < 0) telem.battery_soc = 0;
                if (telem.battery_soc > 100) telem.battery_soc = 100;
            }
            if (len >= 7) {
                telem.motor_temp = (float)((int8_t)d[6]);
            }
            if (len >= 8) {
                telem.heatsink_temp = (float)((int8_t)d[7]);
            }
        }
        // 2. OpenInverter TPDO2 (Currents: 0x281 or 0x280 + node)
        else if (id == (0x280 | VCU_NODE_ID) || id == 0x281) {
            if (len >= 2) {
                int16_t cur_raw = (int16_t)(d[0] | (d[1] << 8));
                telem.pack_current = abs((float)cur_raw * 0.1f);
                if (telem.pack_volts > 10.0f && telem.power_kw == 0.0f) {
                    telem.power_kw = (telem.pack_volts * cur_raw * 0.1f) / 1000.0f;
                }
            }
        }
        // 3. OpenInverter TPDO3 (Status, Opmode, Digital Inputs: 0x381)
        else if (id == (0x380 | VCU_NODE_ID) || id == 0x381) {
            if (len >= 1) telem.opmode = d[0];
            if (len >= 2) {
                telem.din_fwd = (d[1] & 0x01) ? 1 : 0;
                telem.din_rev = (d[1] & 0x02) ? 1 : 0;
                telem.din_brake = (d[1] & 0x04) ? 1 : 0;
                if (telem.din_fwd) telem.active_gear = 3; // D
                else if (telem.din_rev) telem.active_gear = 1; // R
                else if (telem.din_brake && telem.speed_kmh < 1.0f) telem.active_gear = 0; // P
                else telem.active_gear = 2; // N
            }
            telem.status_str = (telem.opmode == 1 || telem.opmode == 2) ? "RUNNING" : "STANDBY";
        }
        // 4. ZombieVerter VCU Standard Broadcast (ID 0x100 or 0x101)
        else if (id == 0x100 || id == 0x101) {
            if (len >= 2) {
                int16_t raw_rpm = (int16_t)(d[0] | (d[1] << 8));
                telem.motor_rpm = (float)raw_rpm;
                telem.speed_kmh = abs((float)raw_rpm) / 48.0f;
            }
            if (len >= 4) {
                uint16_t v_raw = (uint16_t)(d[2] | (d[3] << 8));
                telem.pack_volts = v_raw * 0.1f;
            }
            if (len >= 6) {
                int16_t cur_raw = (int16_t)(d[4] | (d[5] << 8));
                telem.pack_current = (float)cur_raw * 0.1f;
                telem.power_kw = (telem.pack_volts * telem.pack_current) / 1000.0f;
            }
            if (len >= 7) {
                telem.motor_temp = (float)((int8_t)d[6]);
            }
            if (len >= 8) {
                telem.battery_soc = (float)d[7];
            }
        }
        // 5. Nissan Leaf Inverter broadcast (0x1DA / 0x1DB)
        else if (id == 0x1DA || id == 0x1DB) {
            if (len >= 6) {
                uint16_t v_raw = ((d[0] << 2) | (d[1] >> 6));
                telem.pack_volts = v_raw * 0.5f;
                int16_t cur_raw = (int16_t)((d[2] << 8) | d[3]);
                telem.pack_current = abs((float)cur_raw * 0.1f);
                int16_t raw_rpm = (int16_t)((d[4] << 8) | d[5]);
                telem.motor_rpm = (float)raw_rpm;
                telem.speed_kmh = abs((float)raw_rpm) / 48.0f;
                telem.power_kw = (telem.pack_volts * cur_raw * 0.1f) / 1000.0f;
            }
        }
        // 6. BMS Standard Battery Broadcast (0x355 / 0x356)
        else if (id == 0x355) {
            if (len >= 2) {
                uint16_t soc_raw = (uint16_t)(d[0] | (d[1] << 8));
                telem.battery_soc = (soc_raw > 100) ? (soc_raw * 0.1f) : (float)soc_raw;
            }
        }
        else if (id == 0x356) {
            if (len >= 4) {
                int16_t v_raw = (int16_t)(d[0] | (d[1] << 8));
                int16_t i_raw = (int16_t)(d[2] | (d[3] << 8));
                telem.pack_volts = (float)v_raw * 0.01f;
                telem.pack_current = (float)i_raw * 0.1f;
            }
        }
        // 7. CANopen SDO Response (0x580 - 0x5FF)
        else if ((id & 0xFF0) == 0x580) {
            if (len >= 8 && d[0] == 0x43) { // Expedited SDO upload response (4 bytes)
                uint16_t index = d[1] | (d[2] << 8);
                uint8_t sub = d[3];
                int32_t raw_val = 0;
                memcpy(&raw_val, &d[4], 4);
                float val = (float)raw_val / 32.0f;

                // Match parameter index
                if (index == 0x2000 || (index & 0xFF00) == 0x2100) {
                    if (sub == 1) telem.motor_rpm = val;
                    else if (sub == 2) telem.pack_volts = val;
                    else if (sub == 3) telem.pack_current = val;
                    else if (sub == 4) telem.power_kw = val;
                    else if (sub == 5) telem.motor_temp = val;
                }
            }
        }
        // 8. CANopen Heartbeat (0x700 + Node)
        else if ((id & 0x780) == 0x700) {
            telem.status_str = (d[0] == 5) ? "OPERATIONAL" : "PRE-OPERATIONAL";
        }
    }

    // Check timeout: If no CAN messages received for VCU_CAN_TIMEOUT_MS, clamp values to disconnected 0
    if (!is_vcu_connected()) {
        telem.speed_kmh = 0.0f;
        telem.motor_rpm = 0.0f;
        telem.pack_volts = 0.0f;
        telem.pack_current = 0.0f;
        telem.power_kw = 0.0f;
        telem.motor_temp = 0.0f;
        telem.heatsink_temp = 0.0f;
        telem.battery_soc = 0.0f;
        telem.status_str = "NO CAN / DISCONNECTED";
        telem.opmode = 0;
        telem.din_fwd = 0;
        telem.din_rev = 0;
        telem.din_brake = 0;
        telem.active_gear = 2; // N (Neutral)
        return false;
    }

    return got_message;
}
