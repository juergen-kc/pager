#include "system/dial_encoder.h"

#include <Arduino.h>

namespace pager::dial_encoder {

namespace {

constexpr int PIN_A = 41;
constexpr int PIN_B = 40;

volatile int32_t g_position = 0;
volatile uint8_t g_lastAB   = 0;

// Standard 4×4 quadrature state-transition table indexed by (prevAB << 2 |
// currAB). Produces +1 per quarter-step on CW rotation, -1 on CCW; invalid
// transitions (both pins changing simultaneously, indices 0/5/10/15)
// contribute zero. The Dial's encoder reports 4 quarter-steps per detent so
// callers that want "one count per detent" can divide by 4 — but the
// commitment accumulator in the approval view consumes raw quarters for
// finer-grained arc feedback.
constexpr int8_t kTable[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0,
};

void IRAM_ATTR isr() {
  uint8_t ab = (digitalRead(PIN_A) << 1) | digitalRead(PIN_B);
  // Negated — bench-tested on hardware showed the wiring of A/B on this
  // encoder makes the canonical transition table count CCW as positive.
  // We want CW = approve = positive, so flip the sign here rather than
  // baking the inversion into every downstream consumer.
  g_position -= kTable[(g_lastAB << 2) | ab];
  g_lastAB    = ab;
}

} // namespace

void begin() {
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  g_lastAB = (digitalRead(PIN_A) << 1) | digitalRead(PIN_B);
  attachInterrupt(PIN_A, isr, CHANGE);
  attachInterrupt(PIN_B, isr, CHANGE);
}

int32_t readDelta() {
  int32_t d  = g_position;
  g_position = 0;
  return d;
}

} // namespace pager::dial_encoder
