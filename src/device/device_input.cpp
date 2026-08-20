#include "device/device_input.h"

namespace rsspaper {
namespace device {
namespace {

// A missed release must not strand a finger down forever. Comfortably past
// swipe_timeout_ms (900) and long_press_ms (650), so it cannot cut a real
// gesture short — it only recovers from a dropped interrupt.
constexpr uint32_t kStuckTouchMs = 2500;

}  // namespace

size_t DeviceInput::poll(TouchPoint* out, size_t max) {
  if (panel_->touchscreen.available()) {
    uint16_t xs[2] = {0, 0};
    uint16_t ys[2] = {0, 0};
    // Zero fingers is a release event, not an absence of news: getData() only
    // reports at all because available() said there was something to report.
    const uint8_t n = panel_->touchscreen.getData(xs, ys);
    held_count_ = n > 2 ? 2 : n;
    for (size_t i = 0; i < held_count_; ++i) {
      held_[i].x = xs[i];
      held_[i].y = ys[i];
    }
    held_since_ = ::millis();
  } else if (held_count_ > 0 && ::millis() - held_since_ > kStuckTouchMs) {
    held_count_ = 0;
  }

  const size_t count = held_count_ < max ? held_count_ : max;
  for (size_t i = 0; i < count; ++i) out[i] = held_[i];
  return count;
}

}  // namespace device
}  // namespace rsspaper
