#include "state/session.h"
#include "config.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>

namespace pager::state {

namespace {

SessionState g_state;

// PSRAM-backed ring of TurnEvent. We allocate the storage once with
// ps_malloc and placement-new each slot on first use; this keeps the
// internal SRAM free for LVGL framebuffers and the BLE stack. For a
// 64-slot ring at ~256 B/event worst-case we'd use ~16 KB of PSRAM.
struct Ring {
  TurnEvent* data    = nullptr;
  size_t     head    = 0;   // next write index (mod cap)
  size_t     count   = 0;   // populated entries (≤ cap)
  bool       initialised = false;

  void ensure() {
    if (initialised) return;
    void* mem = heap_caps_malloc(sizeof(TurnEvent) * TURN_RING_CAPACITY,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
      // Fall back to internal heap if PSRAM is somehow unavailable —
      // never silently disable the feature.
      mem = heap_caps_malloc(sizeof(TurnEvent) * TURN_RING_CAPACITY,
                             MALLOC_CAP_8BIT);
    }
    data = static_cast<TurnEvent*>(mem);
    for (size_t i = 0; i < TURN_RING_CAPACITY; ++i) new (&data[i]) TurnEvent();
    initialised = true;
  }

  void push(const TurnEvent& e) {
    ensure();
    data[head] = e;
    head = (head + 1) % TURN_RING_CAPACITY;
    if (count < TURN_RING_CAPACITY) count++;
  }

  // i = 0 returns the most recent event.
  const TurnEvent& at(size_t i) const {
    size_t idx = (head + TURN_RING_CAPACITY - 1 - i) % TURN_RING_CAPACITY;
    return data[idx];
  }

  void clear() { head = 0; count = 0; }
};

Ring g_ring;

} // namespace

void applySnapshot(const SessionState& incoming) {
  g_state.counters       = incoming.counters;
  g_state.tokensToday    = incoming.tokensToday;
  g_state.tokensRolling  = std::max(g_state.tokensRolling, incoming.tokensToday);
  g_state.msg            = incoming.msg;
  g_state.entries        = incoming.entries;
  g_state.prompt         = incoming.prompt;
  g_state.lastSnapshotMs = millis();
}

void recordTurn(const TurnEvent& evt) {
  g_ring.push(evt);
}

const SessionState& current() { return g_state; }

size_t turnCount() { return g_ring.count; }

const TurnEvent& turnAt(size_t i) {
  // Caller must check turnCount() > i. We don't bounds-check at runtime
  // because the UI iterates with a counter we control.
  return g_ring.at(i);
}

void resetForReboot() {
  g_state = SessionState{};
  g_ring.clear();
}

} // namespace pager::state
