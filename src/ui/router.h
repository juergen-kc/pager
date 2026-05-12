#pragma once
#include <functional>
#include <string>

// Owns the top-level UI structure: tileview for Glance/Recent/Settings
// horizontal navigation, plus a full-screen Approval overlay that takes
// over when a prompt is active. All view refreshes go through onStateChanged.

namespace pager::ui::router {

struct Hooks {
  // Send a permission decision via the protocol layer.
  std::function<void(const std::string& id, const char* decision)> sendDecision;

  // User chose "Forget bonds" in Settings — wipe NimBLE bonds, clear NVS
  // identity, and reboot. Implementation lives in main.cpp.
  std::function<void()> forgetBonds;

  // Play the approval chime (no-op if disabled in settings).
  std::function<void()> chime;
};

void begin(const Hooks& hooks);

// Called from the main loop (post-protocol-dispatch) so the visible view
// reflects the latest snapshot / ring contents.
void onStateChanged();

// Called when a *new* prompt id arrives, so Approval can take over and
// chime. onStateChanged() will keep its content fresh after that.
void onPromptArrived();

// Pulls touch activity from M5.Touch and dims the screen after IDLE_DIM_MS.
// Call from the main loop.
void serviceIdleDimming();

// Polls the device buttons and dispatches based on the current view and
// target board. On CoreS3 SE the bezel touch strip exposes A/B/C: A hold
// = deny / C tap = approve on the approval modal (mirrors the on-screen
// left-deny / right-approve layout), B = toggle focus on Glance. On the
// M5Stack Dial there's only one front-face button — the encoder push —
// which surfaces as BtnA, so it does both modal actions (tap = approve,
// hold = deny). The side button still acts as BtnB and toggles focus.
// Call from the main loop after M5.update().
void serviceButtons();

// Surface the pairing passkey full-screen. Auto-hides after the user has had
// time to enter it on the host (handled inside the passkey view).
void showPasskey(unsigned int code);

// Tear the passkey screen down — call when the link transitions to bonded
// (encrypted) so we don't sit on the passkey view after pairing succeeds.
void hidePasskey();

// Tick the passkey auto-hide timer; call from the main loop.
void tickPasskey();

} // namespace pager::ui::router
