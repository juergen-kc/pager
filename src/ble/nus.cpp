// NUS peripheral: NimBLE server advertising the Nordic UART Service, with
// LE Secure Connections bonding (DisplayOnly IO cap, 6-digit passkey).
// Incoming writes on RX are accumulated and split on '\n' into complete
// JSON lines before being handed to the app.

#include "ble/nus.h"
#include "config.h"

#include <NimBLEDevice.h>
#include <esp_random.h>
#include <algorithm>
#include <cstring>

namespace pager::ble {

namespace {

NusCallbacks         g_cbs;
NimBLEServer*        g_server           = nullptr;
NimBLECharacteristic* g_txChar          = nullptr;
NimBLECharacteristic* g_rxChar          = nullptr;
std::string          g_lineBuffer;
bool                 g_isSecure         = false;
uint16_t             g_activeConn       = BLE_HS_CONN_HANDLE_NONE;

void emitState(LinkState s) {
  if (g_cbs.onStateChanged) g_cbs.onStateChanged(s);
}

// NUS writes can fragment across MTU boundaries. Accumulate bytes, emit
// complete '\n'-delimited lines upward. Trailing '\r' (if any) is stripped.
void ingest(const std::string& chunk) {
  g_lineBuffer.append(chunk);
  size_t pos;
  while ((pos = g_lineBuffer.find('\n')) != std::string::npos) {
    std::string line = g_lineBuffer.substr(0, pos);
    g_lineBuffer.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (g_cbs.onLineReceived) g_cbs.onLineReceived(line);
  }

  // Guard against a peer that never sends a newline. 5 KB covers the
  // protocol's 4 KB turn-event cap with slack; anything larger is garbage.
  if (g_lineBuffer.size() > LINE_BUFFER_CAPACITY) g_lineBuffer.clear();
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    g_activeConn = info.getConnHandle();
    g_isSecure   = info.isEncrypted();
    emitState(g_isSecure ? LinkState::Connected : LinkState::Pairing);
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int /*reason*/) override {
    g_activeConn = BLE_HS_CONN_HANDLE_NONE;
    g_isSecure   = false;
    g_lineBuffer.clear();
    emitState(LinkState::Disconnected);
    NimBLEDevice::startAdvertising();
    emitState(LinkState::Advertising);
  }

  // NimBLE invokes this when the peer is about to display/enter our passkey.
  // We generate a fresh random 6-digit code per session and return it; the
  // stack then negotiates with the central. We also surface it via a
  // callback so the app can show it on screen.
  uint32_t onPassKeyDisplay() override {
    uint32_t pk = esp_random() % 1000000;
    if (g_cbs.onPasskeyDisplay) g_cbs.onPasskeyDisplay(pk);
    return pk;
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    g_isSecure = info.isEncrypted();
    if (g_isSecure) emitState(LinkState::Connected);
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    ingest(c->getValue());
  }
};

} // namespace

void begin(const std::string& deviceName, const NusCallbacks& cbs) {
  g_cbs = cbs;

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Bonding + MITM + LE Secure Connections, with DisplayOnly IO capability
  // so the host prompts the user to enter the 6-digit passkey we display.
  NimBLEDevice::setSecurityAuth(/*bond*/ true, /*mitm*/ true, /*sc*/ true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);

  // TX: device -> desktop, notify. READ_ENC forces the CCCD subscription
  // to occur only over an encrypted link, which implicitly requires pairing
  // before notifications can flow.
  g_txChar = svc->createCharacteristic(
      NUS_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC
  );

  // RX: desktop -> device, write. WRITE_ENC rejects unencrypted writes.
  g_rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC
  );
  g_rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setName(deviceName);
  adv->enableScanResponse(true);
  adv->start();

  emitState(LinkState::Advertising);
}

bool sendLine(const std::string& line) {
  if (!g_txChar || g_activeConn == BLE_HS_CONN_HANDLE_NONE) return false;

  std::string payload = line;
  if (payload.empty() || payload.back() != '\n') payload.push_back('\n');

  // Conservative chunk size. NimBLE negotiates MTU up from 23 but we don't
  // query the active MTU here; 180 bytes fits a default ATT_MTU of 185 with
  // room for the notify header.
  constexpr size_t kChunk = 180;
  for (size_t off = 0; off < payload.size(); off += kChunk) {
    size_t n = std::min(kChunk, payload.size() - off);
    g_txChar->setValue(reinterpret_cast<const uint8_t*>(payload.data()) + off, n);
    g_txChar->notify();
  }
  return true;
}

bool isSecure() { return g_isSecure; }

void forgetBonds() { NimBLEDevice::deleteAllBonds(); }

const char* stateName(LinkState s) {
  switch (s) {
    case LinkState::Advertising:  return "Advertising";
    case LinkState::Pairing:      return "Pairing";
    case LinkState::Connected:    return "Connected";
    case LinkState::Disconnected: return "Disconnected";
  }
  return "?";
}

} // namespace pager::ble
