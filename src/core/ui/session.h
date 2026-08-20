// How long the reader stays resident after the last touch.
//
// This is policy, and it lives in portable code rather than in src/device/ so
// that it can be tested against SimPower on a laptop. Sleep behaviour that
// only exists on the device is debuggable only on the device, which is the
// worst place to debug it.
#pragma once

#include <cstdint>

namespace rsspaper {

struct SessionThresholds {
  // Rebuilding state from the card costs ~2.25 s; a page turn with state
  // already resident costs ~0.5 s. Staying resident for a while after the
  // last touch is what makes picking the paper back up feel like paper.
  // These defaults are placeholders for a measurement that belongs to #5.
  uint32_t doze_after_ms = 30000;
  uint32_t sleep_after_ms = 300000;
};

enum class SessionIntent {
  Stay,   // fully resident; a page turn is cheap
  Doze,   // idle, but RAM intact
  Sleep,  // deep sleep; waking rebuilds from the card
};

class Session {
 public:
  explicit Session(SessionThresholds thresholds) : t_(thresholds) {}

  void touched(uint32_t now_ms) { last_ = now_ms; }
  SessionIntent intent(uint32_t now_ms) const;

 private:
  SessionThresholds t_;
  uint32_t last_ = 0;
};

}  // namespace rsspaper
