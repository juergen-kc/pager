#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace pager::ble {

// BLE link lifecycle, surfaced to the app layer. Actual UI mode selection
// (Glance/Approval/etc.) is derived from this plus protocol state.
enum class LinkState {
  Advertising,
  Pairing,
  Connected,
  Disconnected
};

// Callbacks into the app layer. All fire on the BLE task context; keep them
// fast and non-blocking. Heavier work should stash data and let the main
// loop pick it up.
struct NusCallbacks {
  std::function<void(LinkState)>               onStateChanged;
  std::function<void(const std::string& line)> onLineReceived;
  std::function<void(uint32_t passkey)>        onPasskeyDisplay;
};

// Initialise NimBLE with LE Secure Connections bonding (DisplayOnly IO
// capability) and start advertising the NUS service under `deviceName`.
// Call once from setup().
void begin(const std::string& deviceName, const NusCallbacks& cbs);

// Send one newline-terminated line to the connected peer. A trailing '\n'
// is appended if the caller omitted one. Returns false if no peer is
// connected or the TX characteristic isn't ready.
bool sendLine(const std::string& line);

// True if a peer is currently connected over an encrypted (bonded) link.
bool isSecure();

// Wipe every bond in NimBLE's NVS store. Caller should reboot afterwards;
// the advertising state will not self-recover in-place.
void forgetBonds();

// Short human-readable name for logs and the hello-world screen.
const char* stateName(LinkState s);

} // namespace pager::ble
