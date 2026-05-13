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
lv_obj_t* g_lblClock     = nullptr;
lv_obj_t* g_lblDate      = nullptr;
lv_obj_t* g_lblMsg       = nullptr;
lv_obj_t* g_lblCounters  = nullptr;
lv_obj_t* g_spark        = nullptr;
lv_chart_series_t* g_sparkSeries = nullptr;
lv_obj_t* g_tokenBar     = nullptr;
lv_obj_t* g_lblTokens    = nullptr;
lv_obj_t* g_lblEntry1    = nullptr;
lv_obj_t* g_lblEntry2    = nullptr;
lv_obj_t* g_lblFooter    = nullptr;

// Sparkline state — track tokens_today between heartbeats and derive a
// per-tick delta (rate). 60 samples × ~10 s heartbeat ≈ 10 min of history.
constexpr uint16_t kSparkSamples = 60;
uint32_t g_lastTokens     = 0;
bool     g_sparkPrimed    = false;

// Short ring of recent deltas, used to compute the "≈X tok/min" label.
// 6 × 10 s heartbeat ≈ 1 min window.
constexpr int      kBurnWindow      = 6;
uint32_t           g_burnRing[kBurnWindow] = {0};
int                g_burnHead       = 0;

// Time of the most recent activity we can detect — last heartbeat that
// reported running > 0, waiting > 0, or a non-zero token delta. Used to
// drive the "idle 4m" / "active now" footer.
uint32_t           g_lastActiveMs   = 0;

// Focus mode — long-press to silence the chime + dim backlight for an hour.
constexpr uint32_t kFocusDurationMs = 60u * 60u * 1000u;
uint32_t g_focusUntilMs   = 0;

void onLongPress(lv_event_t*) { toggleFocus(); }
} // namespace

bool focusActive() {
  return g_focusUntilMs > lv_tick_get();
}

void toggleFocus() {
  uint32_t now = lv_tick_get();
  if (g_focusUntilMs > now) {
    g_focusUntilMs = 0;
  } else {
    g_focusUntilMs = now + kFocusDurationMs;
  }
  refresh();
}

void mount(lv_obj_t* parent) {
  if (g_root) return;

  g_root = lv_obj_create(parent);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(g_root, 12, 0);
  lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
#ifndef PAGER_BOARD_DIAL
  // Landscape: flex column drives layout. Dial uses explicit alignment
  // below (see the PAGER_BOARD_DIAL block at the end of mount()).
  lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#endif

  g_lblStatus = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblStatus, lv_color_hex(0x808080), 0);
  lv_label_set_text(g_lblStatus, "Pager - starting");

  // Clock + date float top-right; LV_OBJ_FLAG_IGNORE_LAYOUT keeps them
  // out of the parent's flex column. Read "--:--" / "--" until the
  // desktop sends a time sync.
  g_lblClock = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblClock, lv_color_hex(0xC0C0C0), 0);
  lv_obj_add_flag(g_lblClock, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(g_lblClock, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_label_set_text(g_lblClock, "--:--");

  g_lblDate = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblDate, lv_color_hex(0x808080), 0);
  lv_obj_add_flag(g_lblDate, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(g_lblDate, LV_ALIGN_TOP_RIGHT, 0, 16);
  lv_label_set_text(g_lblDate, "--");

  g_lblMsg = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblMsg, lv_color_hex(0x707070), 0);
  lv_obj_set_width(g_lblMsg, LV_PCT(100));
  lv_label_set_long_mode(g_lblMsg, LV_LABEL_LONG_DOT);
  lv_label_set_text(g_lblMsg, "");

  g_lblCounters = lv_label_create(g_root);
  lv_obj_set_style_text_font(g_lblCounters, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(g_lblCounters, lv_color_white(), 0);
  lv_obj_set_style_pad_top(g_lblCounters, 12, 0);
  lv_label_set_text(g_lblCounters, "0 running, 0 waiting");

  // Sparkline: token-burn rate over the last ~10 min. Stripped of axes,
  // grid, point markers — we just want the trace shape.
  g_spark = lv_chart_create(g_root);
  lv_obj_set_size(g_spark, LV_PCT(100), 24);
  lv_obj_set_style_pad_top(g_spark, 16, 0);
  lv_obj_set_style_pad_all(g_spark, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_spark, 0, 0);
  lv_obj_set_style_bg_opa(g_spark, LV_OPA_TRANSP, 0);
  lv_chart_set_type(g_spark, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(g_spark, kSparkSamples);
  lv_chart_set_div_line_count(g_spark, 0, 0);
  lv_chart_set_update_mode(g_spark, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_size(g_spark, 0, 0, LV_PART_INDICATOR);   // hide markers
  lv_obj_set_style_line_width(g_spark, 2, LV_PART_ITEMS);
  g_sparkSeries = lv_chart_add_series(g_spark, kAccent, LV_CHART_AXIS_PRIMARY_Y);

  g_tokenBar = lv_bar_create(g_root);
  lv_obj_set_size(g_tokenBar, LV_PCT(100), 8);
  lv_obj_set_style_pad_top(g_tokenBar, 8, 0);
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

  g_lblFooter = lv_label_create(g_root);
  lv_obj_set_style_text_color(g_lblFooter, lv_color_hex(0x707070), 0);
  lv_obj_set_style_pad_top(g_lblFooter, 8, 0);
  lv_label_set_text(g_lblFooter, "");

  // Long-press anywhere on Glance toggles focus mode. Re-add CLICKABLE
  // because remove_style_all() above stripped the default flag set.
  lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_root, onLongPress, LV_EVENT_LONG_PRESSED, nullptr);

#ifdef PAGER_BOARD_DIAL
  // Round 240×240 doesn't fit the landscape flex column. Drop the
  // widgets that don't earn their row at this size (date, sparkline,
  // token bar, second transcript entry) and reposition the rest
  // single-column, centred, inside the inscribed circle. refresh()
  // still writes into the hidden widgets — cheaper than gating every
  // text update on a board flag.
  lv_obj_add_flag(g_lblDate,   LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_spark,     LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_tokenBar,  LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_lblEntry2, LV_OBJ_FLAG_HIDDEN);

  auto place = [](lv_obj_t* o, lv_align_t a, int x, int y) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_pad_top(o, 0, 0);   // reset flex-era spacing
    lv_obj_align(o, a, x, y);
  };
  place(g_lblClock,    LV_ALIGN_TOP_MID,     0,   6);
  place(g_lblStatus,   LV_ALIGN_TOP_MID,     0,  32);
  place(g_lblMsg,      LV_ALIGN_TOP_MID,     0,  60);
  place(g_lblCounters, LV_ALIGN_TOP_MID,     0,  92);
  place(g_lblTokens,   LV_ALIGN_TOP_MID,     0, 118);
  place(g_lblEntry1,   LV_ALIGN_TOP_MID,     0, 148);
  place(g_lblFooter,   LV_ALIGN_BOTTOM_MID,  0, -16);

  // Counter font: 24 was a chunky anchor on the 320×240 landscape;
  // shrink to match the rest of the column at 14.
  lv_obj_set_style_text_font(g_lblCounters, &lv_font_montserrat_14, 0);

  // 200px chord keeps wrapped or truncated labels inside the inscribed
  // circle at typical row heights.
  lv_obj_set_width(g_lblMsg,    200);
  lv_obj_set_width(g_lblEntry1, 200);

  lv_obj_set_style_text_align(g_lblStatus,   LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_lblMsg,      LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_lblCounters, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_lblTokens,   LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_lblEntry1,   LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(g_lblFooter,   LV_TEXT_ALIGN_CENTER, 0);
#endif
}

void refresh() {
  if (!g_root) return;
  const auto& s = state::current();

  uint32_t age = lv_tick_get() - s.lastSnapshotMs;
  bool stale = (s.lastSnapshotMs == 0) || (age > HEARTBEAT_STALE_MS);

  // Status pill colour-codes session state at a glance:
  //   violet → focus mode active (overrides everything else)
  //   gray   → no recent heartbeat
  //   red    → at least one session blocked on a permission prompt
  //   amber  → at least one session running
  //   green  → connected, all sessions idle
  char        focusBuf[24];
  const char* label;
  uint32_t    color;
  if (focusActive()) {
    uint32_t remainMin = (g_focusUntilMs - lv_tick_get() + 59'999) / 60'000;
    snprintf(focusBuf, sizeof(focusBuf), "Pager - focus %um", (unsigned)remainMin);
    label = focusBuf; color = 0xB080E0;
  } else if (stale) {
    label = "Pager - disconnected"; color = 0x808080;
  } else if (s.counters.waiting > 0) {
    label = "Pager - waiting";      color = 0xE85050;
  } else if (s.counters.running > 0) {
    label = "Pager - working";      color = 0xE0A040;
  } else {
    label = "Pager - idle";         color = 0x80E0A0;
  }
  lv_label_set_text(g_lblStatus, label);
  lv_obj_set_style_text_color(g_lblStatus, lv_color_hex(color), 0);

  // `msg` is the desktop's free-form one-line summary
  // (REFERENCE.md: "suitable for a small display"). Hide the row when
  // empty so the rest of the layout reflows tight.
  if (s.msg.empty()) {
    lv_obj_add_flag(g_lblMsg, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(g_lblMsg, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_lblMsg, s.msg.c_str());
  }

  lv_label_set_text(g_lblClock, system_clock::nowHHMM().c_str());
  lv_label_set_text(g_lblDate,  system_clock::nowDate().c_str());

  lv_label_set_text_fmt(g_lblCounters, "%u running, %u waiting",
                        (unsigned)s.counters.running, (unsigned)s.counters.waiting);

  // Sparkline sample: per-tick delta in tokens_today. The first refresh
  // primes the baseline (we don't know the prior); midnight roll-over and
  // any other backwards step gets clamped to 0 so we never draw a dip.
  uint32_t delta = 0;
  if (g_sparkPrimed && s.tokensToday > g_lastTokens) {
    delta = s.tokensToday - g_lastTokens;
  }
  g_lastTokens  = s.tokensToday;
  g_sparkPrimed = true;
  if (g_spark && g_sparkSeries) {
    lv_chart_set_next_value(g_spark, g_sparkSeries, (lv_coord_t)std::min<uint32_t>(delta, INT16_MAX));
  }

  // Burn ring (last ~1 min) + activity timestamp. Anything that suggests
  // the host actually did something — running, waiting, or a token bump —
  // counts as "active now". The footer below uses the last-active time.
  g_burnRing[g_burnHead] = delta;
  g_burnHead = (g_burnHead + 1) % kBurnWindow;
  uint32_t nowMs = lv_tick_get();
  if (s.counters.running > 0 || s.counters.waiting > 0 || delta > 0) {
    g_lastActiveMs = nowMs;
  }

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

  // Burn rate: average of the last-minute window × 6 ≈ tokens/min.
  // Shown only when there's a non-trivial signal — otherwise the line
  // is just "X tokens today" without the rate suffix.
  uint32_t burnSum = 0;
  for (uint32_t v : g_burnRing) burnSum += v;
  uint32_t burnPerMin = (burnSum * 60u) / (kBurnWindow * 10u);  // 10s heartbeat
  if (burnPerMin >= 10) {
    lv_label_set_text_fmt(g_lblTokens, "%s today (~%u/min)", tokBuf, (unsigned)burnPerMin);
  } else {
    lv_label_set_text_fmt(g_lblTokens, "%s tokens today", tokBuf);
  }

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

  // Footer: time since last activity, or "active now" if anything's
  // currently happening. Hidden until we've seen at least one event.
  if (s.counters.running > 0 || s.counters.waiting > 0) {
    lv_label_set_text(g_lblFooter, "active now");
  } else if (g_lastActiveMs == 0) {
    lv_label_set_text(g_lblFooter, "");
  } else {
    uint32_t idleSec = (nowMs - g_lastActiveMs) / 1000u;
    char buf[24];
    if (idleSec < 60)         snprintf(buf, sizeof(buf), "idle %us",   (unsigned)idleSec);
    else if (idleSec < 3600)  snprintf(buf, sizeof(buf), "idle %um",   (unsigned)(idleSec / 60));
    else                      snprintf(buf, sizeof(buf), "idle %uh%um",
                                        (unsigned)(idleSec / 3600),
                                        (unsigned)((idleSec % 3600) / 60));
    lv_label_set_text(g_lblFooter, buf);
  }
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
lv_obj_t*    g_screen      = nullptr;   // dedicated LVGL screen for the passkey
lv_obj_t*    g_lblCode     = nullptr;
lv_obj_t*    g_prevScreen  = nullptr;   // screen we came from, to restore on hide()
uint32_t     g_shownAtMs   = 0;
constexpr uint32_t kAutoHideMs = 30000;
} // namespace

void mount(lv_obj_t* /*root*/) {
  if (g_screen) return;
  // A dedicated screen avoids z-order / dirty-region quirks that bit us when
  // we tried a sibling overlay on top of the tileview. lv_screen_load() is
  // a guaranteed full repaint.
  g_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

  lv_obj_t* hdr = lv_label_create(g_screen);
  lv_label_set_text(hdr, "Pair with");
  lv_obj_set_style_text_color(hdr, kAccent, 0);
  lv_obj_set_style_text_font(hdr, &lv_font_montserrat_24, 0);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t* sub = lv_label_create(g_screen);
  lv_label_set_text(sub, "Enter this code on your Mac:");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x808080), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 56);

  g_lblCode = lv_label_create(g_screen);
  lv_obj_set_style_text_color(g_lblCode, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_lblCode, &lv_font_montserrat_24, 0);
  lv_obj_align(g_lblCode, LV_ALIGN_CENTER, 0, 20);
}

void show(unsigned int code) {
  if (!g_screen) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%06u", code);
  lv_label_set_text(g_lblCode, buf);
  if (lv_screen_active() != g_screen) {
    g_prevScreen = lv_screen_active();
    lv_screen_load(g_screen);
  }
  // Force a synchronous refresh — LVGL 9 partial-render mode otherwise
  // drops the screen swap on its first dirty-region pass.
  lv_refr_now(nullptr);
  g_shownAtMs = lv_tick_get();
}

void hide() {
  if (g_screen && lv_screen_active() == g_screen && g_prevScreen) {
    lv_screen_load(g_prevScreen);
    lv_refr_now(nullptr);
  }
}

void tick() {
  if (!g_screen || lv_screen_active() != g_screen) return;
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
lv_obj_t*  g_arcAppr   = nullptr;   // green CW-fill from top center
lv_obj_t*  g_arcDeny   = nullptr;   // red CCW-fill from top center
lv_obj_t*  g_lblPress  = nullptr;   // "press to Approve/Deny" when armed
DecideCb   g_decideCb  = nullptr;

// We snapshot the prompt id at show() so a late-arriving snapshot that
// changes the active id can't cause us to send a decision against the
// wrong prompt.
char       g_promptIdSnap[64] = {0};

// Dial commitment state. The encoder feeds quarter-ticks (~40 per full
// turn for the Dial's encoder); arming at ~⅓ turn keeps the gesture
// meaningful without requiring a full rotation. Decay nudges commitment
// back to zero after a half-second of no input so accidental brushes
// don't quietly arm a decision.
constexpr int32_t  kArmThreshold = 15;
constexpr uint32_t kDecayAfterMs = 500;
constexpr uint32_t kDecayPeriodMs = 80;   // one tick decayed per period
int32_t  g_commitment   = 0;
uint32_t g_lastInputMs  = 0;
uint32_t g_lastDecayMs  = 0;

// LVGL arc angles: 0° = 3 o'clock, increasing clockwise. Top center = 270°.
constexpr uint16_t kArcTop = 270;
constexpr uint16_t kArcMaxSweep = 180;   // arc fills up to a half-circle

void updateArcs() {
  if (!g_arcAppr || !g_arcDeny) return;
  const int32_t mag = g_commitment < 0 ? -g_commitment : g_commitment;
  const bool armed = mag >= kArmThreshold;
  const uint32_t sweep = static_cast<uint32_t>(
      armed ? kArcMaxSweep : (mag * kArcMaxSweep) / kArmThreshold);

  if (g_commitment > 0) {
    lv_obj_clear_flag(g_arcAppr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_arcDeny, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_angles(g_arcAppr, kArcTop, (kArcTop + sweep) % 360);
    // Brighten past the arm threshold so the user sees they're committed.
    lv_obj_set_style_arc_color(g_arcAppr,
        lv_color_hex(armed ? 0x30E030 : 0x205020),
        LV_PART_INDICATOR);
  } else if (g_commitment < 0) {
    lv_obj_clear_flag(g_arcDeny, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_arcAppr, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_angles(g_arcDeny, (kArcTop + 360 - sweep) % 360, kArcTop);
    lv_obj_set_style_arc_color(g_arcDeny,
        lv_color_hex(armed ? 0xE03030 : 0x502020),
        LV_PART_INDICATOR);
  } else {
    lv_obj_add_flag(g_arcAppr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_arcDeny, LV_OBJ_FLAG_HIDDEN);
  }

  // Hint label — only appears once the commitment is past the threshold,
  // so the gesture is discoverable without crowding the modal during the
  // rotation phase. Pulled in by user testing: an experienced operator
  // could rotate but didn't know to push, and fell back to touch.
  if (!g_lblPress) return;
  if (armed && g_commitment > 0) {
    lv_label_set_text(g_lblPress, "press to Approve");
    lv_obj_set_style_text_color(g_lblPress, lv_color_hex(0x30E030), 0);
    lv_obj_clear_flag(g_lblPress, LV_OBJ_FLAG_HIDDEN);
  } else if (armed && g_commitment < 0) {
    lv_label_set_text(g_lblPress, "press to Deny");
    lv_obj_set_style_text_color(g_lblPress, lv_color_hex(0xE03030), 0);
    lv_obj_clear_flag(g_lblPress, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_lblPress, LV_OBJ_FLAG_HIDDEN);
  }
}

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

bool isVisible() {
  return g_overlay && !lv_obj_has_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
}

void approve() { if (g_decideCb) g_decideCb("once", g_promptIdSnap); }
void deny()    { if (g_decideCb) g_decideCb("deny", g_promptIdSnap); }

void onEncoderDelta(int32_t ticks) {
  if (ticks == 0 || !isVisible()) return;
  g_commitment += ticks;
  g_lastInputMs = lv_tick_get();
  g_lastDecayMs = g_lastInputMs;
  updateArcs();
}

bool commitIfArmed() {
  if (!isVisible()) return false;
  if (g_commitment >= kArmThreshold) {
    if (g_decideCb) g_decideCb("once", g_promptIdSnap);
    return true;
  }
  if (g_commitment <= -kArmThreshold) {
    if (g_decideCb) g_decideCb("deny", g_promptIdSnap);
    return true;
  }
  return false;
}

void tickCommitment() {
  if (!isVisible() || g_commitment == 0) return;
  const uint32_t now = lv_tick_get();
  if (now - g_lastInputMs < kDecayAfterMs) return;
  if (now - g_lastDecayMs < kDecayPeriodMs) return;
  g_lastDecayMs = now;
  if (g_commitment > 0)      g_commitment--;
  else if (g_commitment < 0) g_commitment++;
  updateArcs();
}

void mount(lv_obj_t* root) {
  if (g_overlay) return;

  g_overlay = lv_obj_create(root);
  lv_obj_remove_style_all(g_overlay);
  lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_overlay, lv_color_hex(0x1a0a0a), 0);
  lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_overlay, 12, 0);
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);

  // Commitment arcs created first so labels and buttons render on top of
  // them. Size to the full overlay; on the Dial (240×240) the arc just
  // hugs the panel edge, on the CoreS3 SE it draws across the 320×240
  // landscape — that's fine, they only become visible on a Dial because
  // nothing feeds them onEncoderDelta() on the CoreS3 SE. Non-clickable
  // so taps still reach the on-screen Approve / Deny buttons underneath.
  auto buildArc = [&](lv_obj_t** dst) {
    lv_obj_t* arc = lv_arc_create(g_overlay);
    lv_obj_set_size(arc, LV_PCT(100), LV_PCT(100));
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 0, 0);          // hide the background ring
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);  // no draggable knob
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    *dst = arc;
  };
  buildArc(&g_arcAppr);
  buildArc(&g_arcDeny);

  // "press to Approve/Deny" — appears only when the commitment crosses
  // the arm threshold so the rotate-then-press gesture is discoverable.
  // Centred so it lands inside the round Dial bezel even though the
  // on-screen buttons below it may be partly cropped.
  g_lblPress = lv_label_create(g_overlay);
  lv_obj_set_style_text_font(g_lblPress, &lv_font_montserrat_24, 0);
  // y=12 sits just above the bottom touch buttons on the Dial (their
  // top edge is around y=160 from screen top; the hint baseline lands
  // near y=145). On the CoreS3 SE the hint is never shown — nothing
  // feeds the encoder — so position doesn't matter there.
  lv_obj_align(g_lblPress, LV_ALIGN_CENTER, 0, 12);
  lv_obj_add_flag(g_lblPress, LV_OBJ_FLAG_HIDDEN);

  // ── Layout differs per board ──────────────────────────────────────
  // CoreS3 SE has a 320×240 landscape canvas: header top-left, prompt
  // text left-aligned below it, big touch buttons in the bottom
  // corners. The Dial has a round 240×240 canvas: content stays
  // inside the inscribed circle, so everything is centred and the
  // buttons shrink to fit two-wide inside the bottom of the circle
  // (~200px available width at y≈180 from top).

  lv_obj_t* warn = lv_label_create(g_overlay);
  lv_label_set_text(warn, "!  Approval needed");
  lv_obj_set_style_text_color(warn, kAccent, 0);
#ifdef PAGER_BOARD_DIAL
  lv_obj_set_style_text_font(warn, &lv_font_montserrat_14, 0);
  lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 14);
#else
  lv_obj_set_style_text_font(warn, &lv_font_montserrat_24, 0);
  lv_obj_align(warn, LV_ALIGN_TOP_LEFT, 0, 0);
#endif

  g_lblTool = lv_label_create(g_overlay);
  lv_obj_set_style_text_color(g_lblTool, lv_color_white(), 0);
#ifdef PAGER_BOARD_DIAL
  lv_obj_set_style_text_align(g_lblTool, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_lblTool, LV_ALIGN_TOP_MID, 0, 38);
#else
  lv_obj_align(g_lblTool, LV_ALIGN_TOP_LEFT, 0, 44);
#endif

  g_lblHint = lv_label_create(g_overlay);
  lv_label_set_long_mode(g_lblHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_lblHint, lv_color_hex(0xE0E0E0), 0);
#ifdef PAGER_BOARD_DIAL
  // 180px wide keeps wrapped text inside the inscribed circle's chord
  // at typical hint-row heights (~y=70 from top).
  lv_obj_set_width(g_lblHint, 180);
  lv_obj_set_style_text_align(g_lblHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_lblHint, LV_ALIGN_TOP_MID, 0, 62);
#else
  lv_obj_set_width(g_lblHint, LV_PCT(100));
  lv_obj_align(g_lblHint, LV_ALIGN_TOP_LEFT, 0, 70);
#endif

  g_btnDeny = lv_btn_create(g_overlay);
  lv_obj_set_style_bg_color(g_btnDeny, lv_color_hex(0x402020), 0);
  lv_obj_t* dlbl = lv_label_create(g_btnDeny);
#ifdef PAGER_BOARD_DIAL
  // 92px button gets cramped at "Hold to Deny" (≈84px text). The Dial's
  // primary deny path is the encoder anyway — touch is a fallback — so
  // a shorter label is fine, the long-press behaviour is unchanged.
  lv_label_set_text(dlbl, "Deny");
#else
  lv_label_set_text(dlbl, "Hold to Deny");
#endif
  lv_obj_center(dlbl);
  lv_obj_add_event_cb(g_btnDeny, denyCb, LV_EVENT_LONG_PRESSED, nullptr);
#ifdef PAGER_BOARD_DIAL
  lv_obj_set_size(g_btnDeny, 92, 36);
  // Negative x offsets the deny button to the left of centre; y=-44
  // from BOTTOM_MID puts its baseline at y≈190 from top, where the
  // inscribed circle still gives ~200px of horizontal room.
  lv_obj_align(g_btnDeny, LV_ALIGN_BOTTOM_MID, -50, -44);
#else
  lv_obj_set_size(g_btnDeny, 130, 50);
  lv_obj_align(g_btnDeny, LV_ALIGN_BOTTOM_LEFT, 0, 0);
#endif

  g_btnAppr = lv_btn_create(g_overlay);
  lv_obj_set_style_bg_color(g_btnAppr, lv_color_hex(0x205020), 0);
  lv_obj_t* albl = lv_label_create(g_btnAppr);
  lv_label_set_text(albl, "Approve");
  lv_obj_center(albl);
  lv_obj_add_event_cb(g_btnAppr, approveCb, LV_EVENT_CLICKED, nullptr);
#ifdef PAGER_BOARD_DIAL
  lv_obj_set_size(g_btnAppr, 92, 36);
  lv_obj_align(g_btnAppr, LV_ALIGN_BOTTOM_MID, 50, -44);
#else
  lv_obj_set_size(g_btnAppr, 130, 50);
  lv_obj_align(g_btnAppr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
#endif
}

void show() {
  if (!g_overlay) return;
  refresh();
  // Fresh commitment on every show — a stale value from the previous
  // prompt must not auto-arm the new one.
  g_commitment   = 0;
  g_lastInputMs  = lv_tick_get();
  g_lastDecayMs  = g_lastInputMs;
  updateArcs();
  lv_obj_clear_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_overlay);
  // Force a synchronous repaint — same LVGL 9 partial-render edge case the
  // passkey screen hit. Without this, an overlay revealed via clear-HIDDEN
  // stays unpainted until something else triggers a full screen refresh.
  lv_refr_now(nullptr);
  // Snapshot id so the late-arrival race described above can't bite.
  const auto& p = state::current().prompt;
  std::strncpy(g_promptIdSnap, p.id.c_str(), sizeof(g_promptIdSnap) - 1);
  g_promptIdSnap[sizeof(g_promptIdSnap) - 1] = 0;
}

void hide() {
  if (!g_overlay) return;
  lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_refr_now(nullptr);
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
