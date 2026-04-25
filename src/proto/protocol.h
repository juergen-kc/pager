#pragma once
#include <cstdint>
#include <functional>
#include <string>

// Protocol layer. Owns JSON parse/build for the v1 subset of Hardware Buddy
// REFERENCE.md. Pure: takes inbound lines, calls back out for side effects
// (state mutations, BLE TX, audio chime, reboot).

namespace pager::proto {

struct Outbox {
  // Send a JSON line back to the desktop. Implementation in main.cpp wires
  // this to pager::ble::sendLine().
  std::function<bool(const std::string& line)> sendLine;
};

struct Hooks {
  // Fires when a snapshot arrives that activates a permission prompt and
  // the prompt id differs from the one we last surfaced. UI uses this to
  // trigger the chime / takeover; the SessionState already holds details.
  std::function<void()> onPromptArrived;

  // Fires after every snapshot apply (with or without prompt). UI uses this
  // to refresh whichever view is mounted.
  std::function<void()> onSnapshotApplied;

  // Fires after a turn event is appended to the ring.
  std::function<void()> onTurnRecorded;

  // Fires when desktop has asked us to wipe bonds and reboot. Implementation
  // is expected to forget bonds, clear identity, ack, and reboot the device.
  std::function<void()> onUnpair;
};

void begin(const Outbox& tx, const Hooks& hooks);

// Parse and dispatch one inbound JSON line. Caller is responsible for line
// framing; this function does not buffer. Returns true if the line was
// recognised (ack sent or snapshot applied), false on parse error.
bool handleLine(const std::string& line);

// User-driven outbound: send a permission decision for the currently active
// prompt id. decision must be "once" or "deny". Increments local stats.
bool sendPermission(const std::string& id, const char* decision);

// Build and send a status response. Used by the status command handler and
// can be called directly from a Settings test affordance later if useful.
bool sendStatusResponse(uint32_t n);

} // namespace pager::proto
