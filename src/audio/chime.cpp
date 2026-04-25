#include "audio/chime.h"
#include "persistence/store.h"

#include <M5Unified.h>

namespace pager::audio {

void begin() {
  // M5.begin() configures the speaker; we just nudge volume to a polite
  // desk-companion level. 0–255; 50 ≈ "audible from arm's reach but won't
  // cut through a conversation".
  M5.Speaker.setVolume(50);
}

void chime() {
  if (!store::settings().chimeOn) return;

  // Two short sine pips a major-third apart. tone(freq, ms) is queued, so
  // we don't need to delay between calls — the second one waits its turn.
  M5.Speaker.tone(880,  90);   // A5
  M5.Speaker.tone(1108, 90);   // C#6
}

} // namespace pager::audio
