// Pager — composition root.
//
// Wires:
//   BLE peripheral → FreeRTOS queue → protocol dispatcher → SessionState
//                                         ↓
//                                       UI router (LVGL)
//
// The queue is the contract between the NimBLE task and the main loop:
// callbacks fire on the BLE task and must not block, so they allocate a
// std::string and shove a pointer onto the queue. The main loop drains
// the queue every iteration, parses, applies state, and refreshes views.

#include "audio/chime.h"
#include "ble/nus.h"
#include "config.h"
#include "persistence/store.h"
#include "proto/protocol.h"
#include "state/session.h"
#include "system/clock.h"
#include "ui/lvgl_port.h"
#include "ui/router.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>

namespace {

QueueHandle_t  g_inboundQ      = nullptr;
std::string    g_deviceName;

// Build "Claude-Pager-XXXX" from the BT MAC unless the user overrode it
// via desktop `cmd:name`, in which case we honour the persisted choice.
std::string buildDeviceName() {
  const auto& s = pager::store::settings();
  if (!s.deviceName.empty()) return s.deviceName;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return std::string(PAGER_NAME_PREFIX) + suffix;
}

// ─────────────── BLE-task callbacks (must be fast / non-blocking) ───────────────

// Flag set by BLE task when the link becomes encrypted; main loop hides the
// passkey screen on the transition. (Same volatile-bool pattern as passkey.)
volatile bool g_blePairingComplete = false;

void onBleStateChanged(pager::ble::LinkState s) {
  Serial.printf("[ble] state = %s\n", pager::ble::stateName(s));
  // No UI work here — LVGL is not safe to touch off the main loop. The
  // visible link state is implied by snapshot freshness in Glance.
  if (s == pager::ble::LinkState::Connected) {
    g_blePairingComplete = true;
  }
}

void onLineReceived(const std::string& line) {
  // Heap-allocate a copy of the line and hand the pointer to the queue.
  // The line buffer in nus.cpp is reused immediately after this call.
  auto* owned = new std::string(line);
  if (xQueueSend(g_inboundQ, &owned, 0) != pdTRUE) {
    Serial.println("[ble] inbound queue full, dropping line");
    delete owned;
  }
}

// Side-channel for the passkey: BLE-task writes, main-loop reads. We keep
// a separate "pending" flag because 0 (000000) is a legal — if extremely
// rare — passkey, so we can't use 0 as the no-passkey sentinel.
volatile uint32_t g_pendingPasskey = 0;
volatile bool     g_passkeyPending = false;

void onPasskeyDisplay(uint32_t passkey) {
  Serial.printf("[ble] passkey = %06u\n", passkey);
  g_pendingPasskey = passkey;
  g_passkeyPending = true;
}

// ─────────────── Outbound protocol → BLE ───────────────

bool sendLine(const std::string& line) {
  return pager::ble::sendLine(line);
}

// ─────────────── Decision callback from UI router ───────────────

void onUiDecision(const std::string& id, const char* decision) {
  Serial.printf("[ui] decision=%s id=%s\n", decision, id.c_str());
  pager::proto::sendPermission(id, decision);
}

// ─────────────── Forget bonds (Settings or `cmd:unpair`) ───────────────

void forgetEverythingAndReboot() {
  Serial.println("[main] wiping bonds + identity, rebooting");
  pager::store::clearIdentity();
  pager::ble::forgetBonds();
  delay(200);  // give NimBLE a tick to flush NVS
  ESP.restart();
}

// ─────────────── Hooks plumbed into the protocol layer ───────────────

void onPromptArrived()    { pager::ui::router::onPromptArrived(); }
void onSnapshotApplied()  { pager::ui::router::onStateChanged();  }
void onTurnRecorded()     { pager::ui::router::onStateChanged();  }
void onUnpair()           { forgetEverythingAndReboot();          }

void drainInbound() {
  std::string* line = nullptr;
  while (xQueueReceive(g_inboundQ, &line, 0) == pdTRUE) {
    pager::proto::handleLine(*line);
    delete line;
  }
}

void drainPasskey() {
  if (!g_passkeyPending) return;
  g_passkeyPending = false;
  pager::ui::router::showPasskey(g_pendingPasskey);
}

void drainPairingComplete() {
  if (!g_blePairingComplete) return;
  g_blePairingComplete = false;
  pager::ui::router::hidePasskey();
  pager::ui::router::onStateChanged();
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("Pager v%s\n", PAGER_VERSION);

  // M5Unified must come up first — it owns the panel, touch, RTC, speaker.
  auto cfg = M5.config();
  M5.begin(cfg);

  pager::store::begin();
  pager::system_clock::begin();
  pager::audio::begin();
  pager::ui::lvgl_port::begin();

  pager::ui::router::begin({
    .sendDecision = onUiDecision,
    .forgetBonds  = forgetEverythingAndReboot,
    .chime        = []() { pager::audio::chime(); },
  });

  pager::proto::begin(
    /*outbox*/ { .sendLine = sendLine },
    /*hooks*/  { .onPromptArrived   = onPromptArrived,
                 .onSnapshotApplied = onSnapshotApplied,
                 .onTurnRecorded    = onTurnRecorded,
                 .onUnpair          = onUnpair });

  g_inboundQ = xQueueCreate(INBOUND_QUEUE_DEPTH, sizeof(std::string*));

  g_deviceName = buildDeviceName();
  Serial.printf("[ble] advertising as %s\n", g_deviceName.c_str());

  pager::ble::begin(g_deviceName, {
    .onStateChanged   = onBleStateChanged,
    .onLineReceived   = onLineReceived,
    .onPasskeyDisplay = onPasskeyDisplay,
  });
}

void loop() {
  M5.update();

  drainPasskey();
  drainPairingComplete();
  drainInbound();

  pager::ui::router::serviceButtons();
  pager::ui::router::serviceIdleDimming();
  pager::ui::router::tickPasskey();

  unsigned int wait = pager::ui::lvgl_port::tick();
  if (wait > 16) wait = 16;          // cap at ~60 fps service interval
  if (wait < 2)  wait = 2;
  vTaskDelay(pdMS_TO_TICKS(wait));
}
