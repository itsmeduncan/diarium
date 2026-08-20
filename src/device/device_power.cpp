#include "device/device_power.h"

#include <esp_sleep.h>

namespace rsspaper {
namespace device {

int DevicePower::battery_millivolts() const {
  return static_cast<int>(panel_->readBattery() * 1000.0);
}

void DevicePower::deep_sleep_until(Epoch when) {
  (void)when;  // the schedule comes from set_wake_in(), see the header
  panel_->einkOff();
  if (storage_ != nullptr) panel_->sdCardSleep();

  // Touch wakes the reader. GPIO 36 is TOUCHSCREEN_INT, active low.
  //
  // It must be the only pin in the mask: ESP32's ext1 offers ALL_LOW or
  // ANY_HIGH, and both of these signals idle high and pulse low independently,
  // so ALL_LOW across two of them is a condition that essentially never
  // occurs. Asking for two active-low sources at once is how this device
  // ended up unwakeable, holding its last page like a frozen app.
  esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_36, ESP_EXT1_WAKEUP_ALL_LOW);

  // The scheduled wake is a timer rather than the RTC's alarm pin, because a
  // second active-low pin cannot share the mask above. It also means a device
  // whose RTC was never set still wakes on schedule.
  if (wake_in_seconds_ > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wake_in_seconds_) *
                                  1000000ULL);
  }

  esp_deep_sleep_start();  // does not return; the device wakes from reset
}

}  // namespace device
}  // namespace rsspaper
