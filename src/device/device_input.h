// IInput from the Cypress capacitive controller. Gesture recognition is
// portable and sits above this, so the HAL never has to agree about what a
// swipe is.
#pragma once

#include <cstddef>
#include <cstdint>

#include "Inkplate.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceInput final : public IInput {
 public:
  explicit DeviceInput(Inkplate* panel) : panel_(panel) {}
  bool begin() { return panel_->touchscreen.init(true); }

  size_t poll(TouchPoint* out, size_t max) override;
  uint32_t millis() const override { return ::millis(); }

 private:
  Inkplate* panel_;
};

}  // namespace device
}  // namespace rsspaper
