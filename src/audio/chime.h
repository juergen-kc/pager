#pragma once

namespace pager::audio {

void begin();

// Play the soft double-chirp described in PAGER_SPEC.md §10. Non-blocking;
// M5.Speaker queues the tones internally. Respects the chime toggle in
// pager::store::settings().
void chime();

} // namespace pager::audio
