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
std::string          g_advertisedName;
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
  g_advertisedName = deviceName;

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

  // RX: desktop -> device, write. Need to advertise BOTH write modes the
  // standard NUS profile supports — WRITE (with response, 0x0008) AND
  // WRITE_NR (write-without-response, 0x0004). Claude's bridge uses
  // WRITE_CMD; without the NR property bit, NimBLE rejects those packets
  // and Claude logs "3 status timeouts" forever. WRITE_ENC (0x1000) is the
  // NimBLE permission flag forcing encryption on the writes.
  g_rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE
        | NIMBLE_PROPERTY::WRITE_NR
        | NIMBLE_PROPERTY::WRITE_ENC
  );
  g_rxChar->setCallbacks(new RxCallbacks());

  // NimBLE 2.x: NimBLEService::start() is a deprecated no-op; services are
  // started by NimBLEServer::start(). Without this, the GATT table is never
  // registered and the central sees an empty service even though raw
  // advertisements go out.
  g_server->start();

  // A BLE adv packet is 31 bytes. The NUS UUID alone is 18 bytes (16 + 2
  // header) and `Claude-Pager-XXXX` is 17 chars — together they overflow,
  // so NimBLE silently drops the name and Claude's name-prefix filter
  // misses us. Put the UUID in the main adv and the name in the scan
  // response, which the central fetches on the follow-up SCAN_REQ.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;
  advData.setCompleteServices(NimBLEUUID(NUS_SERVICE_UUID));
  adv->setAdvertisementData(advData);

  NimBLEAdvertisementData scanResp;
  scanResp.setName(deviceName, /*isComplete*/ true);
  adv->setScanResponseData(scanResp);

  adv->enableScanResponse(true);
  adv->start();

  emitState(LinkState::Advertising);
}

bool sendLine(const std::string& line) {
  if (!g_txChar || g_activeConn == BLE_HS_CONN_HANDLE_NONE) return false;

  std::string payload = line;
  if (payload.empty() || payload.back() != '\n') payload.push_back('\n');

  // ATT notification carries up to (MTU - 3) bytes per packet. Query the
  // negotiated MTU on the live connection — peers that don't bump up from
  // the 23-byte default would silently truncate if we hardcoded a larger
  // chunk. Falls back to 23-3=20 if MTU lookup fails.
  //
  // Use the explicit notify(value, length) overload rather than setValue +
  // notify(). The setValue path silently truncates against the local
  // characteristic value buffer's capacity (NimBLE 2.x sizes that to the
  // last advertised MTU on creation, before MTU is negotiated up), so a
  // 182-byte response was going out as two 1-byte notifications.
  uint16_t mtu = g_server ? g_server->getPeerMTU(g_activeConn) : 23;
  if (mtu < 23) mtu = 23;
  size_t chunk = (size_t)mtu - 3;

  for (size_t off = 0; off < payload.size(); off += chunk) {
    size_t n = std::min(chunk, payload.size() - off);
    g_txChar->notify(reinterpret_cast<const uint8_t*>(payload.data()) + off, n);
  }
  return true;
}

bool isSecure() { return g_isSecure; }

const std::string& advertisedName() { return g_advertisedName; }

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
