#include "ui/router.h"
#include "config.h"
#include "state/session.h"
#include "ui/lvgl_port.h"
#include "ui/views.h"

#include <M5Unified.h>
#include <lvgl.h>

namespace pager::ui::router {

namespace {

Hooks       g_hooks;
lv_obj_t*   g_root      = nullptr;
lv_obj_t*   g_tileview  = nullptr;
bool        g_modalOn   = false;
uint32_t    g_lastTouchMs = 0;
bool        g_dimmed   = false;

void onDecide(const char* decision, const char* id) {
  if (g_hooks.sendDecision) g_hooks.sendDecision(id, decision);
  // Optimistically hide the modal. The next snapshot will either confirm
  // (prompt cleared) or — if it didn't take — re-trigger via onPromptArrived.
  approval::hide();
  g_modalOn = false;
}

} // namespace

void begin(const Hooks& hooks) {
  g_hooks = hooks;
  approval::setDecideCallback(onDecide);
  settings::setForgetCallback([]() { if (g_hooks.forgetBonds) g_hooks.forgetBonds(); });

  g_root = lv_screen_active();
  lv_obj_set_style_bg_color(g_root, lv_color_black(), 0);

  // Tap-and-hold threshold for the Deny button is governed by the indev's
  // long-press time. Stock LVGL default is 400ms which already matches
  // PAGER_SPEC.md §6.2; set explicitly so a future LVGL version with a
  // different default doesn't silently change behaviour.
  lv_indev_t* indev = lv_indev_get_next(nullptr);
  if (indev) lv_indev_set_long_press_time(indev, DENY_HOLD_MS);

  g_tileview = lv_tileview_create(g_root);
  lv_obj_set_size(g_tileview, LV_PCT(100), LV_PCT(100));
  lv_obj_set_scroll_dir(g_tileview, LV_DIR_HOR);

  lv_obj_t* tGlance   = lv_tileview_add_tile(g_tileview, 0, 0, LV_DIR_RIGHT);
  lv_obj_t* tRecent   = lv_tileview_add_tile(g_tileview, 1, 0, LV_DIR_HOR);
  lv_obj_t* tSettings = lv_tileview_add_tile(g_tileview, 2, 0, LV_DIR_LEFT);

  glance::mount(tGlance);
  recent::mount(tRecent);
  settings::mount(tSettings);

  approval::mount(g_root);
  passkey::mount(g_root);

  // Initial paint with whatever defaults SessionState/store hold.
  glance::refresh();
  recent::refresh();
  settings::refresh();

  g_lastTouchMs = lv_tick_get();
}

void onStateChanged() {
  // Cheap to refresh all three — only the visible one renders. Doing it
  // here keeps the prompt/Recent/Settings logic out of the hot path.
  glance::refresh();
  recent::refresh();
  settings::refresh();

  const auto& s = state::current();
  if (s.prompt.active) {
    if (!g_modalOn) {
      approval::show();
      g_modalOn = true;
    } else {
      approval::refresh();
    }
  } else if (g_modalOn) {
    approval::hide();
    g_modalOn = false;
  }
}

void onPromptArrived() {
  if (g_hooks.chime) g_hooks.chime();
  // Wake the screen on a new approval — the user needs to see it.
  lvgl_port::setBacklight(220);
  g_dimmed = false;
  g_lastTouchMs = lv_tick_get();
  approval::show();
  g_modalOn = true;
}

void showPasskey(unsigned int code) {
  // Wake the screen — the user is actively pairing.
  lvgl_port::setBacklight(220);
  g_dimmed = false;
  g_lastTouchMs = lv_tick_get();
  passkey::show(code);
}

void tickPasskey() { passkey::tick(); }

void serviceIdleDimming() {
  // Any actual touch (not just a hover pass-through) resets the timer.
  auto t = M5.Touch.getDetail(0);
  if (t.isPressed()) {
    g_lastTouchMs = lv_tick_get();
    if (g_dimmed) {
      lvgl_port::setBacklight(220);
      g_dimmed = false;
    }
    return;
  }
  if (!g_dimmed && (lv_tick_get() - g_lastTouchMs) > IDLE_DIM_MS) {
    lvgl_port::setBacklight(40);
    g_dimmed = true;
  }
}

} // namespace pager::ui::router
