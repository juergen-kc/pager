#pragma once

#include <cstdint>

// LVGL 9.x bring-up against M5GFX. Owns the framebuffers, the flush
// callback, the touch driver, and exposes a single tick() to be called
// from the main loop. Everything else in src/ui/ talks to LVGL directly
// and assumes init has happened.

namespace pager::ui::lvgl_port {

void begin();

// Drive LVGL: process timers and run any pending flushes. Call from loop().
// Returns the number of milliseconds LVGL says are safe to wait before the
// next call — caller can use that or just delay a fixed amount.
unsigned int tick();

// Apply a brightness level to the panel. 0..255.
void setBacklight(uint8_t level);

} // namespace pager::ui::lvgl_port
