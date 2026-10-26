#ifndef CAN_VCU_H
#define CAN_VCU_H

#include <Arduino.h>
#include <driver/twai.h>
#include "openinverter_ui.h"

#define VCU_CAN_TX_PIN GPIO_NUM_20
#define VCU_CAN_RX_PIN GPIO_NUM_19
#define VCU_CAN_TIMEOUT_MS 1500

// Initialize TWAI controller on Waveshare ESP32-S3 (GPIO 20 TX, GPIO 19 RX, 500 kbps)
bool init_vcu_can();

// Poll and parse all incoming CAN frames from the ZombieVerter VCU
// Returns true if new CAN messages were processed
bool poll_vcu_can(InverterTelemetry &telem);

// Query VCU over CAN SDO for periodic parameters
void request_vcu_telemetry_sdo();

// Check if VCU is actively communicating on the CAN bus
bool is_vcu_connected();

// Get timestamp of last received CAN message
unsigned long get_last_can_rx_ms();

// Transmit arbitrary CAN frame (used by CAN Tools tab)
bool send_can_frame(uint32_t id, const uint8_t *data, uint8_t len);

// Total packet counters
extern uint32_t can_rx_packet_count;
extern uint32_t can_tx_packet_count;

#endif // CAN_VCU_H
