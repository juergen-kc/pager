#pragma once
#include <cstdint>

// Quadrature decoder for the M5Stack Dial's rotary encoder on GPIO 41/40
// (per m5stack/M5Dial: DIAL_ENCODER_PIN_A=41, DIAL_ENCODER_PIN_B=40).
// ISR-based: both pins are configured as INPUT_PULLUP with edge-change
// interrupts feeding a 4×4 transition table. The encoder push button is
// already surfaced by M5Unified as M5.BtnA (GPIO 42), so it is not handled
// here.
//
// Only compiled into the dial build (PAGER_BOARD_DIAL); main.cpp guards
// both begin() and readDelta() at the call sites.

namespace pager::dial_encoder {

void begin();

// Signed ticks since the last call. Resets to zero on read. CW rotation
// (looking at the front face) is positive; CCW is negative. May lose up to
// one tick under contention — the read+reset is not strictly atomic, but
// a single missed tick over a multi-tick gesture is invisible to the user.
int32_t readDelta();

} // namespace pager::dial_encoder
