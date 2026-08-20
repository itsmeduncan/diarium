#include "device/device_input.h"

namespace rsspaper {
namespace device {

size_t DeviceInput::poll(TouchPoint* out, size_t max) {
  if (max == 0 || !panel_->touchscreen.available()) return 0;
  uint16_t xs[2] = {0, 0};
  uint16_t ys[2] = {0, 0};
  const uint8_t n = panel_->touchscreen.getData(xs, ys);
  const size_t count = static_cast<size_t>(n) < max ? n : max;
  for (size_t i = 0; i < count; ++i) {
    out[i].x = xs[i];
    out[i].y = ys[i];
  }
  return count;
}

}  // namespace device
}  // namespace rsspaper
