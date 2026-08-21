// IInput from the Cypress capacitive controller. Gesture recognition is
// portable and sits above this, so the HAL never has to agree about what a
// swipe is.
#pragma once

#include <cstddef>
#include <cstdint>

#include "Inkplate.h"
#include "hal/hal.h"

namespace diarium {
namespace device {

class DeviceInput final : public IInput {
 public:
  explicit DeviceInput(Inkplate* panel) : panel_(panel) {}
  bool begin() { return panel_->touchscreen.init(true); }

  size_t poll(TouchPoint* out, size_t max) override;
  uint32_t millis() const override { return ::millis(); }

 private:
  Inkplate* panel_;

  // The controller is edge-triggered: an interrupt sets a flag, getData()
  // clears it, and a finger held still produces no further events. poll() is
  // specified as "the currently-touched points" — a level — so the last
  // report is held until the controller says otherwise.
  TouchPoint held_[2];
  size_t held_count_ = 0;
  uint32_t held_since_ = 0;
};

}  // namespace device
}  // namespace diarium
