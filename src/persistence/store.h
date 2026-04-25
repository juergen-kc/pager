#pragma once
#include <cstdint>
#include <string>

// NVS-backed settings + counters. Single namespace, key strings kept short
// so we don't bloat the NVS index. Reads are in-memory after begin(); writes
// go through immediately so a crash mid-session doesn't lose stats.

namespace pager::store {

struct Stats {
  uint32_t appr = 0;  // total approvals
  uint32_t deny = 0;  // total denials
  uint32_t lvl  = 0;  // protocol-mandated, kept at 0 (pet-state derived)
};

struct Settings {
  std::string deviceName;  // overrides MAC suffix when non-empty
  std::string owner;       // set by desktop via cmd:owner
  bool        chimeOn = false;
};

void begin();

const Settings& settings();
const Stats&    stats();

void setDeviceName(const std::string& name);
void setOwner(const std::string& owner);
void setChimeOn(bool on);

void incApprovals();
void incDenials();

// Wipe owner + device name; counters are preserved (they reflect history,
// not bond state). Caller still has to wipe BLE bonds separately.
void clearIdentity();

} // namespace pager::store
