#include "persistence/store.h"

#include <Preferences.h>

namespace pager::store {

namespace {

constexpr const char* kNs        = "pager";
constexpr const char* kKeyName   = "name";
constexpr const char* kKeyOwner  = "owner";
constexpr const char* kKeyChime  = "chime";
constexpr const char* kKeyAppr   = "appr";
constexpr const char* kKeyDeny   = "deny";

Preferences g_prefs;
Settings    g_settings;
Stats       g_stats;

} // namespace

void begin() {
  g_prefs.begin(kNs, /*readOnly*/ false);

  g_settings.deviceName = std::string(g_prefs.getString(kKeyName,  "").c_str());
  g_settings.owner      = std::string(g_prefs.getString(kKeyOwner, "").c_str());
  g_settings.chimeOn    = g_prefs.getBool(kKeyChime, false);

  g_stats.appr = g_prefs.getUInt(kKeyAppr, 0);
  g_stats.deny = g_prefs.getUInt(kKeyDeny, 0);
}

const Settings& settings() { return g_settings; }
const Stats&    stats()    { return g_stats; }

void setDeviceName(const std::string& name) {
  g_settings.deviceName = name;
  g_prefs.putString(kKeyName, name.c_str());
}

void setOwner(const std::string& owner) {
  g_settings.owner = owner;
  g_prefs.putString(kKeyOwner, owner.c_str());
}

void setChimeOn(bool on) {
  g_settings.chimeOn = on;
  g_prefs.putBool(kKeyChime, on);
}

void incApprovals() {
  g_stats.appr++;
  g_prefs.putUInt(kKeyAppr, g_stats.appr);
}

void incDenials() {
  g_stats.deny++;
  g_prefs.putUInt(kKeyDeny, g_stats.deny);
}

void clearIdentity() {
  g_settings.deviceName.clear();
  g_settings.owner.clear();
  g_prefs.remove(kKeyName);
  g_prefs.remove(kKeyOwner);
}

} // namespace pager::store
