#pragma once

// Pager firmware version.
#define PAGER_VERSION "1.0.0"

// BLE advertising name. Full advertised name becomes "Claude-Pager-XXXX"
// where XXXX is the last two bytes of the device's BT MAC in hex.
#define PAGER_NAME_PREFIX "Claude-Pager-"

// Nordic UART Service UUIDs per Anthropic's Hardware Buddy protocol
// (REFERENCE.md). These are fixed; do not change.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // desktop -> device (write)
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // device  -> desktop (notify)

// Line buffer for newline-delimited JSON. Sized for the worst-case turn
// event (capped at 4 KB by the desktop) plus some slack for in-flight
// fragments. If we ever overrun this, something's wrong upstream.
#define LINE_BUFFER_CAPACITY 5120

// Ring buffer capacity for captured turn events (Recent view). Allocated
// from PSRAM; cleared on reboot per spec.
#define TURN_RING_CAPACITY 64

// Per-row inline summary length. Anything longer is ellipsised.
#define TURN_SUMMARY_LEN 96

// FreeRTOS queue depth for inbound JSON lines crossing the BLE-task ->
// main-loop boundary. Sized so a brief stall in the main loop can't drop
// snapshots; deeper than this we'd rather drop than balloon RAM.
#define INBOUND_QUEUE_DEPTH 16

// NVS namespace for persisted settings + counters.
#define NVS_NAMESPACE "pager"

// Idle dim threshold (ms). Any touch wakes the screen back to full brightness.
#define IDLE_DIM_MS 30000

// Heartbeat-stale threshold (ms). No snapshot for this long => "disconnected"
// banner in Glance. Distinct from BLE link state.
#define HEARTBEAT_STALE_MS 30000

// Deny gesture threshold (ms). Approve is a tap; Deny requires this hold.
#define DENY_HOLD_MS 400
