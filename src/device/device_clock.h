// IClock from the on-board RTC.
#pragma once

#include "Inkplate.h"
#include "core/base/datetime.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceClock final : public IClock {
 public:
  explicit DeviceClock(Inkplate* panel) : panel_(panel) {}

  // From feeds.toml. The device has no network to ask, and the masthead date
  // and wake_at are both local.
  void set_utc_offset(int seconds) { offset_ = seconds; }

  Epoch now() const override;
  int utc_offset_seconds() const override { return offset_; }
  void set_wake_alarm(Epoch when) override;

 private:
  Inkplate* panel_;
  int offset_ = 0;
};

}  // namespace device
}  // namespace rsspaper
