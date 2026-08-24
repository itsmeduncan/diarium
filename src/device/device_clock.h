// IClock from the on-board RTC.
#pragma once

#include "Inkplate.h"
#include "core/base/datetime.h"
#include "hal/hal.h"

namespace diarium {
namespace device {

class DeviceClock final : public IClock {
 public:
  explicit DeviceClock(Inkplate* panel) : panel_(panel) {}

  // From feeds.toml. The device has no network to ask, and the masthead date
  // and wake_at are both local.
  void set_utc_offset(int seconds) { offset_ = seconds; }

  // A fresh board reads 2000-01-01. With no network in this milestone,
  // nothing else can set the clock, so the composed edition's own date is the
  // best available answer and beats a masthead that is 26 years wrong.
  void seed_if_unset(Epoch when);

  // Believed over anything already set: a server's clock beats a date
  // inherited from whatever edition happened to be on the card.
  void set_now(Epoch when);

  Epoch now() const override;
  int utc_offset_seconds() const override { return offset_; }
  void set_wake_alarm(Epoch when) override;

 private:
  Inkplate* panel_;
  int offset_ = 0;
};

}  // namespace device
}  // namespace diarium
