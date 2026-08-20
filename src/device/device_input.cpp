#include "device/device_input.h"

namespace rsspaper {
namespace device {
namespace {

// A missed release must not strand a finger down forever. Comfortably past
// swipe_timeout_ms (900) and long_press_ms (650), so it cannot cut a real
// gesture short — it only recovers from a dropped interrupt.
constexpr uint32_t kStuckTouchMs = 2500;

#if RSSPAPER_PORTRAIT
// The controller reports in the panel's landscape raster. This is the exact
// inverse of the rotation in device_display.cpp's blits, and it has to stay
// the inverse: if the two ever disagree, every tap opens the story next to
// the one under the finger, which looks like a gesture bug rather than a
// geometry one.
constexpr int kPanelHeight = 758;
#endif

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
#if RSSPAPER_PORTRAIT
      held_[i].x = kPanelHeight - 1 - static_cast<int>(ys[i]);
      held_[i].y = static_cast<int>(xs[i]);
#else
      held_[i].x = xs[i];
      held_[i].y = ys[i];
#endif
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
