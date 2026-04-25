#pragma once

// All four mode views in one translation unit. Each view has a mount()
// that builds widgets into the supplied parent, and a refresh() that
// pulls current SessionState / settings into those widgets. Views never
// observe state directly — the Router calls refresh() at the right times.

// LVGL 9 unified the struct tag and typedef; older LVGL 8 used `_lv_obj_t`.
struct lv_obj_t;
typedef struct lv_obj_t lv_obj_t;

namespace pager::ui {

namespace glance {
  void mount(lv_obj_t* parent);
  void refresh();
}

namespace recent {
  void mount(lv_obj_t* parent);
  void refresh();
}

namespace settings {
  void mount(lv_obj_t* parent);
  void refresh();

  // Called by Router when the user taps "Forget bonds". Lets the Router
  // perform the actual NimBLE wipe + reboot.
  using ForgetCb = void (*)();
  void setForgetCallback(ForgetCb cb);
}

namespace passkey {
  // Fullscreen pairing-passkey overlay. show() also auto-hides itself
  // 30s after the most recent show() (typical pairing flow takes ~10s).
  void mount(lv_obj_t* root);
  void show(unsigned int code);
  void hide();
  void tick();   // call from main loop for the auto-hide timer
}

namespace approval {
  // The approval overlay is full-screen and modal. mount() builds it once,
  // hidden; show() makes it visible and refreshes; hide() takes it down.
  void mount(lv_obj_t* root);
  void show();
  void hide();
  void refresh();

  // Decision callback receives "once" or "deny" plus the prompt id.
  using DecideCb = void (*)(const char* decision, const char* id);
  void setDecideCallback(DecideCb cb);
}

} // namespace pager::ui
