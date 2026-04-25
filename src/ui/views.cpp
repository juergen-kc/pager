// View widget construction. Each namespace owns its widget pointers in
// file-scope statics; mount() is idempotent on the first call and a no-op
// on subsequent calls (we use the existing widget tree).

#include "ui/views.h"
#include "config.h"
#include "persistence/store.h"
#include "state/session.h"
#include "system/clock.h"

#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pager::ui {

namespace {

constexpr lv_color_t kAccent = LV_COLOR_MAKE(0xD9, 0x77, 0x06); // amber

// Pretty-print a token count: "31.2k" / "1.4M" / "812".
void formatTokens(uint32_t n, char* out, size_t cap) {
  if (n >= 1'000'000)      snprintf(out, cap, "%.1fM", n / 1'000'000.0);
  else if (n >= 1'000)     snprintf(out, cap, "%.1fk", n / 1'000.0);
  else                     snprintf(out, cap, "%u",    (unsigned)n);
}

} // namespace

// ─────────────────────────── GLANCE ───────────────────────────

namespace glance {
namespace {
lv_obj_t* g_root         = nullptr;
lv_obj_t* g_lblStatus    = nullptr;
lv_obj_t* g_lblCounters  = nullptr;
lv_obj_t* g_tokenBar     = nullptr;
lv_obj_t* g_lblTokens    = nullptr;
lv_obj_t* g_lblEntry1    = nullptr;
lv_obj_t* g_lblEntry2    = nullptr;
} // namespace

void mount(lv_obj_t* parent) {
  if (g_root) return;

  g_root = lv_obj_create(parent);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(g_root, 12, 0);
  lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  g_lblStatus = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblStatus, lv_color_hex(0x808080), 0);
  lv_label_set_text(g_lblStatus, "Pager · starting");

  g_lblCounters = lv_label_create(g_root);
  lv_obj_set_style_text_font(g_lblCounters, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(g_lblCounters, lv_color_white(), 0);
  lv_obj_set_style_pad_top(g_lblCounters, 12, 0);
  lv_label_set_text(g_lblCounters, "0 running · 0 waiting");

  g_tokenBar = lv_bar_create(g_root);
  lv_obj_set_size(g_tokenBar, LV_PCT(100), 8);
  lv_obj_set_style_pad_top(g_tokenBar, 16, 0);
  lv_bar_set_range(g_tokenBar, 0, 1000);
  lv_bar_set_value(g_tokenBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_tokenBar, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_tokenBar, kAccent,                LV_PART_INDICATOR);

  g_lblTokens = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblTokens, lv_color_hex(0xC0C0C0), 0);
  lv_label_set_text(g_lblTokens, "0 tokens today");

  g_lblEntry1 = lv_label_create(g_root);
  lv_obj_set_style_pad_top(g_lblEntry1, 16, 0);
  lv_obj_set_style_text_color(g_lblEntry1, lv_color_hex(0xE0E0E0), 0);
  lv_obj_set_width(g_lblEntry1, LV_PCT(100));
  lv_label_set_long_mode(g_lblEntry1, LV_LABEL_LONG_DOT);
  lv_label_set_text(g_lblEntry1, "");

  g_lblEntry2 = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblEntry2, lv_color_hex(0xA0A0A0), 0);
  lv_obj_set_width(g_lblEntry2, LV_PCT(100));
  lv_label_set_long_mode(g_lblEntry2, LV_LABEL_LONG_DOT);
  lv_label_set_text(g_lblEntry2, "");
}

void refresh() {
  if (!g_root) return;
  const auto& s = state::current();

  uint32_t age = lv_tick_get() - s.lastSnapshotMs;
  bool stale = (s.lastSnapshotMs == 0) || (age > HEARTBEAT_STALE_MS);
  lv_label_set_text(g_lblStatus, stale ? "Pager · disconnected" : "Pager · connected");
  lv_obj_set_style_text_color(g_lblStatus,
                              stale ? lv_color_hex(0x808080) : lv_color_hex(0x80E0A0), 0);

  lv_label_set_text_fmt(g_lblCounters, "%u running · %u waiting",
                        (unsigned)s.counters.running, (unsigned)s.counters.waiting);

  // Logarithmic scaling: log10(1+n)/log10(1+peak) maps current → 0..1000.
  // Visually forgiving (no big jumps) and needs no persisted ceiling beyond
  // the in-memory rolling peak we already track.
  uint32_t peak = std::max<uint32_t>(s.tokensRolling, 1000);
  double frac = std::log10(1.0 + (double)s.tokensToday)
              / std::log10(1.0 + (double)peak);
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  lv_bar_set_value(g_tokenBar, (int32_t)(frac * 1000.0), LV_ANIM_OFF);

  char tokBuf[16];
  formatTokens(s.tokensToday, tokBuf, sizeof(tokBuf));
  lv_label_set_text_fmt(g_lblTokens, "%s tokens today", tokBuf);

  // Last two entries from the snapshot. Spec: most recent first.
  auto setEntry = [](lv_obj_t* lbl, const state::EntryLine* e) {
    if (!e) { lv_label_set_text(lbl, ""); return; }
    if (e->ts.empty())
      lv_label_set_text(lbl, e->text.c_str());
    else
      lv_label_set_text_fmt(lbl, "%s  %s", e->ts.c_str(), e->text.c_str());
  };
  setEntry(g_lblEntry1, s.entries.size() >= 1 ? &s.entries[0] : nullptr);
  setEntry(g_lblEntry2, s.entries.size() >= 2 ? &s.entries[1] : nullptr);
}

} // namespace glance

// ─────────────────────────── RECENT ───────────────────────────

namespace recent {
namespace {
lv_obj_t* g_root = nullptr;
lv_obj_t* g_list = nullptr;
} // namespace

void mount(lv_obj_t* parent) {
  if (g_root) return;
  g_root = lv_obj_create(parent);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_root, 8, 0);

  lv_obj_t* title = lv_label_create(g_root);
  lv_label_set_text(title, "Recent");
  lv_obj_set_style_text_color(title, lv_color_hex(0x808080), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 0);

  g_list = lv_list_create(g_root);
  lv_obj_set_size(g_list, LV_PCT(100), 210);
  lv_obj_align(g_list, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(g_list, lv_color_hex(0x101010), 0);
}

void refresh() {
  if (!g_root) return;
  lv_obj_clean(g_list);

  size_t n = state::turnCount();
  if (n == 0) {
    lv_obj_t* hint = lv_label_create(g_list);
    lv_label_set_text(hint, "No turns yet.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x606060), 0);
    return;
  }

  for (size_t i = 0; i < n; ++i) {
    const auto& e = state::turnAt(i);
    char buf[160];
    const char* role = e.role.empty() ? "?" : e.role.c_str();
    snprintf(buf, sizeof(buf), "%s  %s  %s",
             system_clock::formatHHMM(e.tEpoch).c_str(),
             role,
             e.summary.c_str());
    lv_obj_t* row = lv_list_add_btn(g_list, nullptr, buf);
    lv_obj_set_style_text_color(row, lv_color_white(), 0);
  }
}

} // namespace recent

// ─────────────────────────── SETTINGS ───────────────────────────

namespace settings {
namespace {
lv_obj_t*     g_root      = nullptr;
lv_obj_t*     g_lblName   = nullptr;
lv_obj_t*     g_lblOwner  = nullptr;
lv_obj_t*     g_swChime   = nullptr;
lv_obj_t*     g_lblFw     = nullptr;
ForgetCb      g_forgetCb  = nullptr;

void chimeChangedCb(lv_event_t* e) {
  bool on = lv_obj_has_state(g_swChime, LV_STATE_CHECKED);
  store::setChimeOn(on);
}

void forgetClickedCb(lv_event_t* e) {
  if (g_forgetCb) g_forgetCb();
}
} // namespace

void setForgetCallback(ForgetCb cb) { g_forgetCb = cb; }

void mount(lv_obj_t* parent) {
  if (g_root) return;
  g_root = lv_obj_create(parent);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_root, 12, 0);
  lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* title = lv_label_create(g_root);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_color(title, lv_color_hex(0x808080), 0);

  g_lblName = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblName, lv_color_white(), 0);
  lv_obj_set_style_pad_top(g_lblName, 8, 0);

  g_lblOwner = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblOwner, lv_color_hex(0xC0C0C0), 0);

  // Chime row.
  lv_obj_t* row = lv_obj_create(g_root);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(row, 8, 0);

  lv_obj_t* chimeLbl = lv_label_create(row);
  lv_label_set_text(chimeLbl, "Chime on approval");
  lv_obj_set_style_text_color(chimeLbl, lv_color_white(), 0);

  g_swChime = lv_switch_create(row);
  lv_obj_add_event_cb(g_swChime, chimeChangedCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Forget bonds button.
  lv_obj_t* btn = lv_btn_create(g_root);
  lv_obj_set_style_pad_top(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x401010), 0);
  lv_obj_t* btnLbl = lv_label_create(btn);
  lv_label_set_text(btnLbl, "Forget bonds (reboots)");
  lv_obj_center(btnLbl);
  lv_obj_add_event_cb(btn, forgetClickedCb, LV_EVENT_CLICKED, nullptr);

  g_lblFw = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblFw, lv_color_hex(0x606060), 0);
  lv_obj_set_style_pad_top(g_lblFw, 8, 0);
  lv_label_set_text_fmt(g_lblFw, "v%s", PAGER_VERSION);
}

void refresh() {
  if (!g_root) return;
  const auto& s = store::settings();
  const char* name  = s.deviceName.empty() ? "(default)" : s.deviceName.c_str();
  const char* owner = s.owner.empty()       ? "(unset)"   : s.owner.c_str();
  lv_label_set_text_fmt(g_lblName,  "Device: %s", name);
  lv_label_set_text_fmt(g_lblOwner, "Owner:  %s", owner);
  if (s.chimeOn) lv_obj_add_state(g_swChime, LV_STATE_CHECKED);
  else           lv_obj_remove_state(g_swChime, LV_STATE_CHECKED);
}

} // namespace settings

// ─────────────────────────── PASSKEY ───────────────────────────

namespace passkey {
namespace {
lv_obj_t*    g_overlay   = nullptr;
lv_obj_t*    g_lblCode   = nullptr;
uint32_t     g_shownAtMs = 0;
constexpr uint32_t kAutoHideMs = 30000;
} // namespace

void mount(lv_obj_t* root) {
  if (g_overlay) return;
  g_overlay = lv_obj_create(root);
  lv_obj_remove_style_all(g_overlay);
  lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* hdr = lv_label_create(g_overlay);
  lv_label_set_text(hdr, "Pair with");
  lv_obj_set_style_text_color(hdr, kAccent, 0);
  lv_obj_set_style_text_font(hdr, &lv_font_montserrat_24, 0);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t* sub = lv_label_create(g_overlay);
  lv_label_set_text(sub, "Enter this code on your Mac:");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x808080), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 56);

  g_lblCode = lv_label_create(g_overlay);
  lv_obj_set_style_text_color(g_lblCode, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_lblCode, &lv_font_montserrat_24, 0);
  lv_obj_align(g_lblCode, LV_ALIGN_CENTER, 0, 20);
}

void show(unsigned int code) {
  if (!g_overlay) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%06u", code);
  lv_label_set_text(g_lblCode, buf);
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_overlay);
  g_shownAtMs = lv_tick_get();
}

void hide() {
  if (g_overlay) lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

void tick() {
  if (!g_overlay || lv_obj_has_flag(g_overlay, LV_OBJ_FLAG_HIDDEN)) return;
  if ((lv_tick_get() - g_shownAtMs) > kAutoHideMs) hide();
}

} // namespace passkey

// ─────────────────────────── APPROVAL ───────────────────────────

namespace approval {
namespace {
lv_obj_t*  g_overlay   = nullptr;
lv_obj_t*  g_lblTool   = nullptr;
lv_obj_t*  g_lblHint   = nullptr;
lv_obj_t*  g_btnDeny   = nullptr;
lv_obj_t*  g_btnAppr   = nullptr;
DecideCb   g_decideCb  = nullptr;

// We snapshot the prompt id at show() so a late-arriving snapshot that
// changes the active id can't cause us to send a decision against the
// wrong prompt.
char       g_promptIdSnap[64] = {0};

void approveCb(lv_event_t* e) {
  if (g_decideCb) g_decideCb("once", g_promptIdSnap);
}

void denyCb(lv_event_t* e) {
  // Bound to LV_EVENT_LONG_PRESSED (≥ DENY_HOLD_MS), so a stray tap won't
  // fire. See router.cpp for the long-press time configuration.
  if (g_decideCb) g_decideCb("deny", g_promptIdSnap);
}

} // namespace

void setDecideCallback(DecideCb cb) { g_decideCb = cb; }

void mount(lv_obj_t* root) {
  if (g_overlay) return;

  g_overlay = lv_obj_create(root);
  lv_obj_remove_style_all(g_overlay);
  lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x1a0a0a), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_overlay, 12, 0);
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* warn = lv_label_create(g_overlay);
  lv_label_set_text(warn, "!  Approval needed");
  lv_obj_set_style_text_color(warn, kAccent, 0);
  lv_obj_set_style_text_font(warn, &lv_font_montserrat_24, 0);
  lv_obj_align(warn, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lblTool = lv_label_create(g_overlay);
  lv_obj_set_style_text_color(g_lblTool, lv_color_white(), 0);
  lv_obj_align(g_lblTool, LV_ALIGN_TOP_LEFT, 0, 44);

  g_lblHint = lv_label_create(g_overlay);
  lv_obj_set_width(g_lblHint, LV_PCT(100));
  lv_label_set_long_mode(g_lblHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_lblHint, lv_color_hex(0xE0E0E0), 0);
  lv_obj_align(g_lblHint, LV_ALIGN_TOP_LEFT, 0, 70);

  g_btnDeny = lv_btn_create(g_overlay);
  lv_obj_set_size(g_btnDeny, 130, 50);
  lv_obj_align(g_btnDeny, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(g_btnDeny, lv_color_hex(0x402020), 0);
  lv_obj_t* dlbl = lv_label_create(g_btnDeny);
  lv_label_set_text(dlbl, "Hold to Deny");
  lv_obj_center(dlbl);
  lv_obj_add_event_cb(g_btnDeny, denyCb, LV_EVENT_LONG_PRESSED, nullptr);

  g_btnAppr = lv_btn_create(g_overlay);
  lv_obj_set_size(g_btnAppr, 130, 50);
  lv_obj_align(g_btnAppr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(g_btnAppr, lv_color_hex(0x205020), 0);
  lv_obj_t* albl = lv_label_create(g_btnAppr);
  lv_label_set_text(albl, "Approve");
  lv_obj_center(albl);
  lv_obj_add_event_cb(g_btnAppr, approveCb, LV_EVENT_CLICKED, nullptr);
}

void show() {
  if (!g_overlay) return;
  refresh();
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_overlay);
  // Snapshot id so the late-arrival race described above can't bite.
  const auto& p = state::current().prompt;
  std::strncpy(g_promptIdSnap, p.id.c_str(), sizeof(g_promptIdSnap) - 1);
  g_promptIdSnap[sizeof(g_promptIdSnap) - 1] = 0;
}

void hide() {
  if (!g_overlay) return;
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

void refresh() {
  if (!g_overlay) return;
  const auto& p = state::current().prompt;
  lv_label_set_text_fmt(g_lblTool, "Tool: %s",
                        p.tool.empty() ? "(unknown)" : p.tool.c_str());
  lv_label_set_text(g_lblHint, p.hint.c_str());
}

} // namespace approval

} // namespace pager::ui
