// Everything the firmware needs from the world, assembled. Mirrors the shape
// of src/sim/sim_hal.h so the two stay readable against each other.
#pragma once

#include "Inkplate.h"
#include "device/device_clock.h"
#include "device/device_display.h"
#include "device/device_http.h"
#include "device/device_input.h"
#include "device/device_power.h"
#include "device/device_storage.h"
#include "hal/hal.h"

namespace diarium {
namespace device {

struct DeviceHal {
  explicit DeviceHal(Inkplate* panel)
      : display(panel), input(panel), clock(panel), power(panel),
        storage(panel) {
    power.set_storage(&storage);
  }

  // Declaration order is load-bearing: the display's 776 KB framebuffer is
  // claimed on construction, before the font pack and edition arrive to
  // fragment PSRAM.
  DeviceDisplay display;
  DeviceInput input;
  DeviceClock clock;
  DevicePower power;
  DeviceStorage storage;
  DeviceHttpClient http;

  Hal as_hal() {
    Hal hal;
    hal.display = &display;
    hal.input = &input;
    hal.clock = &clock;
    hal.power = &power;
    hal.storage = &storage;
    hal.http = &http;
    return hal;
  }
};

}  // namespace device
}  // namespace diarium
