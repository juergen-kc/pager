// Hardware Buddy protocol dispatcher. Parses inbound JSON lines, mutates
// SessionState, builds and sends acks/responses. The shape comes from
// REFERENCE.md; the v1 subset we handle is enumerated in PAGER_SPEC.md §5.

#include "proto/protocol.h"
#include "ble/nus.h"
#include "config.h"
#include "persistence/store.h"
#include "state/session.h"
#include "system/clock.h"

#include <ArduinoJson.h>

#include <Arduino.h>
#include <esp_system.h>

namespace pager::proto {

namespace {

Outbox g_tx;
Hooks  g_hooks;

// Track the last prompt id we actually surfaced so we don't re-chime on
// every keepalive snapshot while the same prompt is still pending.
std::string g_lastPromptId;

bool sendJson(const JsonDocument& doc) {
  if (!g_tx.sendLine) return false;
  std::string out;
  serializeJson(doc, out);
  return g_tx.sendLine(out);
}

// Build {"ack":<cmd>,"ok":<ok>,"n":<n>[,"error":<err>][,"data":...]}
// `data` is left unset by callers that don't need it; status uses sendStatusResponse instead.
bool sendAck(const char* cmd, bool ok, uint32_t n, const char* err = nullptr) {
  JsonDocument doc;
  doc["ack"] = cmd;
  doc["ok"]  = ok;
  doc["n"]   = n;
  if (err) doc["error"] = err;
  return sendJson(doc);
}

void parseSnapshot(JsonObjectConst snap, state::SessionState& out) {
  // Counters: protocol uses "running" / "waiting" inside a "counters" object
  // (REFERENCE.md). Tolerate either flat or nested for forward compat.
  JsonObjectConst counters = snap["counters"].as<JsonObjectConst>();
  if (counters) {
    out.counters.running = counters["running"] | 0;
    out.counters.waiting = counters["waiting"] | 0;
  } else {
    out.counters.running = snap["running"] | 0;
    out.counters.waiting = snap["waiting"] | 0;
  }

  out.tokensToday = snap["tokens_today"] | snap["tokens"] | 0u;
  out.msg         = std::string(snap["msg"] | "");

  JsonArrayConst entries = snap["entries"].as<JsonArrayConst>();
  if (entries) {
    for (JsonVariantConst e : entries) {
      state::EntryLine line;
      line.ts   = std::string(e["ts"]   | "");
      line.text = std::string(e["text"] | "");
      out.entries.push_back(line);
      if (out.entries.size() >= 4) break;  // UI only renders top 2
    }
  }

  JsonObjectConst prompt = snap["prompt"].as<JsonObjectConst>();
  if (prompt) {
    out.prompt.active = true;
    out.prompt.id   = std::string(prompt["id"]   | "");
    out.prompt.tool = std::string(prompt["tool"] | "");
    out.prompt.hint = std::string(prompt["hint"] | "");
  }
}

void onSnapshot(JsonObjectConst snap) {
  state::SessionState merged;
  parseSnapshot(snap, merged);
  state::applySnapshot(merged);

  if (merged.prompt.active && merged.prompt.id != g_lastPromptId) {
    g_lastPromptId = merged.prompt.id;
    if (g_hooks.onPromptArrived) g_hooks.onPromptArrived();
  } else if (!merged.prompt.active) {
    g_lastPromptId.clear();
  }

  if (g_hooks.onSnapshotApplied) g_hooks.onSnapshotApplied();
}

// Extract the first line of the first text block in a turn event's content
// array. Falls back to role name if nothing usable is present.
std::string summariseTurn(JsonArrayConst content, const std::string& role) {
  for (JsonVariantConst block : content) {
    const char* type = block["type"] | "";
    if (strcmp(type, "text") == 0) {
      const char* text = block["text"] | "";
      const char* nl = strchr(text, '\n');
      size_t len = nl ? (size_t)(nl - text) : strlen(text);
      if (len > TURN_SUMMARY_LEN) len = TURN_SUMMARY_LEN;
      return std::string(text, len);
    }
  }
  return role;
}

void onTurn(JsonObjectConst evt) {
  state::TurnEvent te;
  te.tEpoch  = system_clock::nowEpoch();
  te.role    = std::string(evt["role"] | "");
  te.summary = summariseTurn(evt["content"].as<JsonArrayConst>(), te.role);
  state::recordTurn(te);
  if (g_hooks.onTurnRecorded) g_hooks.onTurnRecorded();
}

void onTimeSync(JsonArrayConst arr) {
  // REFERENCE.md: {"time": [epoch, tz_offset_seconds]}
  if (arr.size() < 1) return;
  uint32_t epoch  = arr[0] | 0u;
  int32_t  offset = arr.size() >= 2 ? (arr[1] | 0) : 0;
  system_clock::applyTime(epoch, offset);
}

void onCmdName(JsonObjectConst cmd) {
  const char* name = cmd["name"] | "";
  if (*name) {
    store::setDeviceName(std::string(name));
    sendAck("name", true, 0);
  } else {
    sendAck("name", false, 0, "missing name");
  }
}

void onCmdOwner(JsonObjectConst cmd) {
  const char* owner = cmd["name"] | cmd["owner"] | "";
  store::setOwner(std::string(owner));
  sendAck("owner", true, 0);
}

void onCmdUnpair(JsonObjectConst /*cmd*/) {
  // Ack BEFORE we tear bonds down — the host will be disconnected as soon
  // as the bond goes away, and we want the ack to land cleanly first.
  sendAck("unpair", true, 0);
  if (g_hooks.onUnpair) g_hooks.onUnpair();
}

void onCmdStatus(JsonObjectConst /*cmd*/) {
  sendStatusResponse(0);
}

void onCommand(JsonObjectConst obj) {
  const char* cmd = obj["cmd"] | "";
  if      (strcmp(cmd, "name")   == 0) onCmdName(obj);
  else if (strcmp(cmd, "owner")  == 0) onCmdOwner(obj);
  else if (strcmp(cmd, "unpair") == 0) onCmdUnpair(obj);
  else if (strcmp(cmd, "status") == 0) onCmdStatus(obj);
  // Folder push family — explicit refusal rather than silent drop, so the
  // desktop gives up cleanly instead of retrying.
  else if (strcmp(cmd, "char_begin") == 0
        || strcmp(cmd, "file")       == 0
        || strcmp(cmd, "chunk")      == 0
        || strcmp(cmd, "file_end")   == 0
        || strcmp(cmd, "char_end")   == 0) {
    sendAck(cmd, false, 0, "unsupported");
  }
  else {
    sendAck(cmd[0] ? cmd : "?", false, 0, "unknown");
  }
}

} // namespace

void begin(const Outbox& tx, const Hooks& hooks) {
  g_tx    = tx;
  g_hooks = hooks;
}

bool handleLine(const std::string& line) {
  if (line.empty()) return false;

  // 4 KB matches the desktop's turn-event cap. ArduinoJson 7's JsonDocument
  // is dynamically grown but seeding with a generous initial size avoids
  // reallocation churn on the hot path.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("[proto] parse error: %s\n", err.c_str());
    return false;
  }

  // Discriminate by which top-level key is present. The protocol is
  // tagged-union over { snapshot:, evt:"turn", time:[...], cmd:"..." }.
  if (doc["cmd"].is<const char*>())            { onCommand(doc.as<JsonObjectConst>()); return true; }
  if (doc["evt"].is<const char*>()
      && strcmp(doc["evt"] | "", "turn") == 0) { onTurn(doc.as<JsonObjectConst>());   return true; }
  if (doc["time"].is<JsonArrayConst>())        { onTimeSync(doc["time"].as<JsonArrayConst>()); return true; }
  if (doc["snapshot"].is<JsonObjectConst>())   { onSnapshot(doc["snapshot"].as<JsonObjectConst>()); return true; }
  // Some implementations dump heartbeat fields at the top level rather
  // than under "snapshot". Accept that too.
  if (doc["counters"].is<JsonObjectConst>()
      || doc["entries"].is<JsonArrayConst>()
      || doc["tokens_today"].is<uint32_t>())   { onSnapshot(doc.as<JsonObjectConst>()); return true; }

  Serial.printf("[proto] unrecognised line: %s\n", line.c_str());
  return false;
}

bool sendPermission(const std::string& id, const char* decision) {
  JsonDocument doc;
  doc["cmd"]      = "permission";
  doc["id"]       = id;
  doc["decision"] = decision;
  bool ok = sendJson(doc);
  if (ok) {
    if (strcmp(decision, "once") == 0) store::incApprovals();
    else                               store::incDenials();
  }
  return ok;
}

bool sendStatusResponse(uint32_t n) {
  const auto& s = store::settings();
  const auto& c = store::stats();

  JsonDocument doc;
  doc["ack"] = "status";
  doc["ok"]  = true;
  doc["n"]   = n;

  JsonObject data = doc["data"].to<JsonObject>();
  // Fall back to the BLE-advertised name (`Claude-Pager-XXXX`) when the user
  // hasn't set an override via NVS. Claude's deviceStatus validator rejects
  // an empty `name`, so never send "" — the desktop will sit in
  // "3 status timeouts" forever (we observed this).
  const std::string& fallback = ble::advertisedName();
  data["name"] = s.deviceName.empty() ? fallback.c_str() : s.deviceName.c_str();
  data["sec"]  = ble::isSecure();

  // CoreS3 SE has no battery — but Claude's validator wants the `bat`
  // object fully populated. Mirror the shape from the protocol example:
  // pct/mV/mA/usb. With usb:true and no cell, pct=100 / mV=5000 / mA=0
  // is the closest honest equivalent. PAGER_SPEC.md §5 says we omit `bat`
  // entirely; the validator disagrees in practice.
  JsonObject bat = data["bat"].to<JsonObject>();
  bat["pct"] = 100;
  bat["mV"]  = 5000;
  bat["mA"]  = 0;
  bat["usb"] = true;

  JsonObject sys = data["sys"].to<JsonObject>();
  sys["up"]   = (uint32_t)(millis() / 1000);
  sys["heap"] = (uint32_t)ESP.getFreeHeap();

  JsonObject stats = data["stats"].to<JsonObject>();
  stats["appr"] = c.appr;
  stats["deny"] = c.deny;
  // `vel` and `nap` are pet-state derived in the reference firmware and
  // PAGER_SPEC.md §5 calls them out as omittable. Like `bat`, the validator
  // wants them present, so we send zeros.
  stats["vel"]  = 0;
  stats["nap"]  = 0;
  stats["lvl"]  = c.lvl;

  return sendJson(doc);
}

} // namespace pager::proto
