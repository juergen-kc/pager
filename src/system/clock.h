#pragma once
#include <cstdint>
#include <string>

// Tiny wrapper over the BM8563 RTC + libc time. We don't try to be a full
// timezone library — the desktop hands us an epoch and an offset and we
// store both, then format wall time with the stored offset.

namespace pager::system_clock {

void begin();

// Apply a time sync from the desktop. epoch is unix seconds; tzOffsetSeconds
// is east-of-UTC (matches REFERENCE.md, e.g. -28800 for PST).
void applyTime(uint32_t epoch, int32_t tzOffsetSeconds);

// Whether we've ever received a time sync.
bool hasSync();

// Current epoch seconds. If we have RTC sync, reads from RTC; otherwise
// falls back to millis()/1000 so callers always get a monotonic-ish value.
uint32_t nowEpoch();

// Format the current local time as "HH:MM" in the synced timezone. Returns
// "--:--" if no sync has happened yet.
std::string nowHHMM();

// Format an arbitrary epoch as "HH:MM" in the synced timezone. Same fallback.
std::string formatHHMM(uint32_t epoch);

} // namespace pager::system_clock
