#include "device/device_clock.h"

namespace rsspaper {
namespace device {

Epoch DeviceClock::now() const {
  // RTC is a member of the driver, not a base class.
  return static_cast<Epoch>(panel_->rtc.getEpoch());
}

void DeviceClock::set_wake_alarm(Epoch when) {
  if (when == kNoDate) return;
  // Match on second, minute, hour and day: a once-a-day alarm at a wall-clock
  // time, which is what edition.wake_at means.
  panel_->rtc.setAlarmEpoch(static_cast<uint32_t>(when),
                            RTC_ALARM_MATCH_DHHMMSS);
}

}  // namespace device
}  // namespace rsspaper
