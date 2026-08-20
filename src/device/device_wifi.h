// Joining a network, and getting off it again promptly: the radio is the
// dominant draw on a compose wake, and a compose wake is the only time this
// device is ever on a network at all.
#pragma once

#include <cstdint>

#include "core/config/feeds_config.h"

namespace rsspaper {
namespace device {

class DeviceWifi {
 public:
  bool connect(const WifiConfig& config, uint32_t timeout_ms = 20000);
  void disconnect();
  bool connected() const;
};

}  // namespace device
}  // namespace rsspaper
