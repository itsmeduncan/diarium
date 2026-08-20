#include "device/device_power.h"

#include <esp_sleep.h>

namespace rsspaper {
namespace device {

int DevicePower::battery_millivolts() const {
  return static_cast<int>(panel_->readBattery() * 1000.0);
}

void DevicePower::deep_sleep_until(Epoch when) {
  (void)when;  // the alarm is armed through IClock::set_wake_alarm
  panel_->einkOff();
  if (storage_ != nullptr) panel_->sdCardSleep();

  // Two wake sources, both active low: GPIO 39 is the RTC alarm interrupt (a
  // compose wake) and GPIO 36 is the wake button (a read wake). ext1 rather
  // than ext0 because ext0 takes only one pin, and a reader that can only be
  // woken by tomorrow's alarm is not a reader.
  esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_39) | (1ULL << GPIO_NUM_36),
                               ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();  // does not return; the device wakes from reset
}

}  // namespace device
}  // namespace rsspaper
