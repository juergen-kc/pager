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

  // Focus mode: long-press the Glance tile to silence the chime and
  // force the backlight dim for 60 min. Auto-expires. Re-press exits
  // early. Other modules (audio, router) check this to know whether
  // to chime or override dimming.
  bool focusActive();

  // Same toggle the long-press gesture invokes — exposed so the bezel
  // BtnB can drive focus mode without faking a synthetic touch event.
  void toggleFocus();
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

  // True while the modal is on screen — the router consults this to gate
  // bezel-button dispatch (BtnA/C only fire approve/deny when visible).
  bool isVisible();

  // Programmatic equivalents of tapping the green Approve button and
  // hold-releasing the red Deny button. Both go through the same decision
  // callback as the touch path, so the prompt-id snapshot logic still applies.
  void approve();
  void deny();

  // Decision callback receives "once" or "deny" plus the prompt id.
  using DecideCb = void (*)(const char* decision, const char* id);
  void setDecideCallback(DecideCb cb);
}

} // namespace pager::ui
