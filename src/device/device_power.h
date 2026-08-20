// IPower: battery, external power, and the deep sleep the whole battery
// budget depends on.
#pragma once

#include "Inkplate.h"
#include "core/base/datetime.h"
#include "device/device_storage.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DevicePower final : public IPower {
 public:
  explicit DevicePower(Inkplate* panel) : panel_(panel) {}

  // So the card's SPI pins can be floated before sleeping; a powered card is
  // a real share of the deep-sleep budget.
  void set_storage(DeviceStorage* storage) { storage_ = storage; }

  void deep_sleep_until(Epoch when) override;
  int battery_millivolts() const override;

  // on_external_power() is deliberately NOT overridden. The obvious
  // candidate, isPowerGood(), reports the TPS65186's rails rather than
  // whether a cable is plugged in, and the PMIC is unpowered whenever the
  // e-paper rail is down — so it answers with an I2C timeout and a false.
  // The base class returns false, which is at least honestly unknown; a
  // wrong answer here would quietly mislead the power policy in #5.

 private:
  Inkplate* panel_;
  DeviceStorage* storage_ = nullptr;
};

}  // namespace device
}  // namespace rsspaper
