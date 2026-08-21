#include "device/device_input.h"

#include "core/layout/page.h"

namespace diarium {
namespace device {
namespace {

// A missed release must not strand a finger down forever. Comfortably past
// swipe_timeout_ms (900) and long_press_ms (650), so it cannot cut a real
// gesture short — it only recovers from a dropped interrupt.
constexpr uint32_t kStuckTouchMs = 2500;

// The panel's short edge. The controller reports in the panel's landscape
// raster, so portrait taps have to be turned by the exact inverse of the
// rotation in device_display.cpp's blits. If the two ever disagree, every tap
// opens the story next to the one under the finger — which reads as a gesture
// bug rather than the geometry one it is.
constexpr int kPanelHeight = 758;

}  // namespace

size_t DeviceInput::poll(TouchPoint* out, size_t max) {
  if (panel_->touchscreen.available()) {
    uint16_t xs[2] = {0, 0};
    uint16_t ys[2] = {0, 0};
    // Zero fingers is a release event, not an absence of news: getData() only
    // reports at all because available() said there was something to report.
    const uint8_t n = panel_->touchscreen.getData(xs, ys);
    held_count_ = n > 2 ? 2 : n;
    const bool portrait = orientation() == Orientation::Portrait;
    for (size_t i = 0; i < held_count_; ++i) {
      if (portrait) {
        held_[i].x = kPanelHeight - 1 - static_cast<int>(ys[i]);
        held_[i].y = static_cast<int>(xs[i]);
      } else {
        held_[i].x = xs[i];
        held_[i].y = ys[i];
      }
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
}  // namespace diarium
