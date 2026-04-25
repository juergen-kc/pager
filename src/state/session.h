#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// In-memory mirror of the desktop's heartbeat snapshots, plus a ring buffer
// of recent turn events for the Recent view. There's exactly one consumer
// (the UI layer running on the main loop) so no locking; the producer is
// also the main loop, which dequeues lines from the BLE-task queue.

namespace pager::state {

struct Counters {
  uint16_t running = 0;
  uint16_t waiting = 0;
};

struct PromptInfo {
  bool         active = false;
  std::string  id;     // echoed back in permission decision
  std::string  tool;   // e.g. "Bash"
  std::string  hint;   // e.g. "rm -rf /tmp/foo"
};

struct EntryLine {
  std::string ts;      // wall clock as "HH:MM" or empty
  std::string text;    // single-line summary
};

struct SessionState {
  Counters                 counters;
  uint32_t                 tokensToday  = 0;
  uint32_t                 tokensRolling = 0;  // local rolling peak; visualisation reference
  std::string              msg;                // free-form status message from desktop
  std::vector<EntryLine>   entries;            // last few entries from snapshot.entries (UI uses ≤2)
  PromptInfo               prompt;
  uint32_t                 lastSnapshotMs = 0; // millis() of last snapshot, for staleness gating
};

struct TurnEvent {
  uint32_t   tEpoch  = 0;   // unix seconds when received
  std::string role;         // "assistant" | "user" | other
  std::string summary;      // first line of first text block
};

// Apply a parsed snapshot. Updates lastSnapshotMs and bumps the rolling
// token peak when tokensToday exceeds it.
void applySnapshot(const SessionState& incoming);

// Append a turn event to the ring (oldest evicted on overflow).
void recordTurn(const TurnEvent& evt);

const SessionState& current();

// Const access to the ring buffer in newest-first order. Iteration is
// snapshot-stable for the duration of the call (no concurrent producers).
size_t turnCount();
const TurnEvent& turnAt(size_t i);  // 0 = newest

void resetForReboot();

} // namespace pager::state
